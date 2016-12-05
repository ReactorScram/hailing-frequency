#include "relay.h"

#include <QDebug>
#include <QFile>
#include <QRegExp>
#include <QTcpSocket>
#include <QUrl>

#include "getopt_pp.h"

PendingConnection::PendingConnection () {
	socket = nullptr;
}

Transfer::Transfer () {
	uploaderSocket = nullptr;
	downloaderSocket = nullptr;
	bufferLimit = 1024 * 1024;
	totalBytesWritten = 0;
	expectedContentLength = 0;
}

Transfer::~Transfer () {
	if (uploaderSocket != nullptr) {
		uploaderSocket->disconnectFromHost ();
		uploaderSocket->deleteLater ();
		uploaderSocket = nullptr;
	}
	if (downloaderSocket != nullptr) {
		downloaderSocket->disconnectFromHost ();
		downloaderSocket->deleteLater ();
		downloaderSocket = nullptr;
	}
}

qint64 Transfer::maxRead () const {
	return bufferLimit - buffer.size ();
}

void Transfer::readData () {
	if (maxRead () > 0 && uploaderSocket != nullptr && downloaderSocket != nullptr) {
		QByteArray newData = uploaderSocket->read (maxRead ());
		buffer.append (newData);
	}
	writeData ();
}

void Transfer::write100 () {
	QByteArray response = ("Uploading to " + key).toUtf8 ();
	
	uploaderSocket->write ("HTTP/1.1 100 Continue\r\n");
	uploaderSocket->write ("Content-Length: " + QString::number (response.length ()).toUtf8 () + "\r\n");
	
	uploaderSocket->write ("\r\n");
}

void Transfer::write307 () {
	QByteArray response = ("Uploading to " + key).toUtf8 ();
	
	uploaderSocket->write ("HTTP/1.1 307 Temporary Redirect\r\n");
	uploaderSocket->write ("Content-Length: " + QString::number (response.length ()).toUtf8 () + "\r\n");
	uploaderSocket->write ("location: " + key.toUtf8 () + "\r\n");
	uploaderSocket->write ("\r\n");
	
	uploaderSocket->write (response);
	
	while (uploaderSocket->bytesToWrite () > 0) {
		uploaderSocket->waitForBytesWritten ();
	}
}

void Transfer::writeData () {
	if (downloaderSocket != nullptr) {
		qint64 bytesWritten = downloaderSocket->write (buffer);
		qDebug() << __LINE__ << "Wrote bytes" << bytesWritten;
		totalBytesWritten += bytesWritten;
		if (bytesWritten >= 0) {
			buffer = buffer.mid (bytesWritten);
			if (buffer.isEmpty () && uploaderSocket == nullptr) {
				while (downloaderSocket->bytesToWrite () > 0) {
					downloaderSocket->waitForBytesWritten ();
				}
				downloaderSocket->disconnectFromHost ();
				downloaderSocket->deleteLater ();
				downloaderSocket = nullptr;
			}
		}
		if (bytesWritten > 0) {
			readData ();
		}
		if (expectedContentLength > 0 && totalBytesWritten >= expectedContentLength) {
			write307 ();
			
			while (downloaderSocket != nullptr && downloaderSocket->bytesToWrite () > 0) {
				downloaderSocket->waitForBytesWritten ();
			}
			
			if (downloaderSocket != nullptr) {
				downloaderSocket->disconnectFromHost ();
				downloaderSocket->deleteLater ();
				downloaderSocket = nullptr;
			}
			
			uploaderSocket->disconnectFromHost ();
			uploaderSocket->deleteLater ();
			uploaderSocket = nullptr;
			totalBytesWritten = 0;
		}
	}
}

RelaySettings::RelaySettings () {
	setToDefaults ();
}

RelaySettings::RelaySettings (int argc, char * argv []) {
	setToDefaults ();
	
	GetOpt::GetOpt_pp opts (argc, argv);
	
	opts >> GetOpt::Option ("port", port);
}

void RelaySettings::setToDefaults () {
	port = 8080;
}

Relay::Relay(RelaySettings s, QObject *parent) : QObject(parent)
{
	settings = s;
	bufferSize = 1024 * 1024;
	serialNumber = 0;
	
	connect (&server, SIGNAL (newConnection ()), SLOT (server_newConnection ()));
	server.listen (QHostAddress::Any, settings.port);
	
	httpHeaderEnd = "\r\n\r\n";
	transferDir = "relays";
	serverName = "Hailing Frequency";
}

 void Relay::server_newConnection () {
	auto socket = server.nextPendingConnection ();
	
	qDebug() << "New connection" << socket;
	
	connect (socket, SIGNAL (disconnected ()), SLOT (socket_disconnected ()));
	connect (socket, SIGNAL (readyRead ()), SLOT (socket_readyRead ()));
	
	PendingConnection c;
	c.socket = socket;
	
	pendingConnections.append (c);
	
	cullFailedRedirections ();
 }
 
 void Relay::discardPendingConnectionBySocket (QTcpSocket * socket) {
	int index = findPendingConnection (socket);
	
	if (index >= 0) {
		auto c = pendingConnections [index];
		// So that the function won't recurse
		pendingConnections.remove (index);
		c.socket->disconnectFromHost ();
		c.socket->deleteLater ();
		c.socket = nullptr;
		
	}
}

int Relay::findPendingConnection (QTcpSocket * socket) {
	for (int i = 0; i < pendingConnections.size (); i++) {
		auto conn = pendingConnections [i];
		if (conn.socket == socket) {
			return i;
		}
	}
	
	return -1;
}

int Relay::findTransfer (QTcpSocket * socket) {
	for (int i = 0; i < transfers.size (); i++) {
		auto transfer = transfers [i];
		if (transfer->uploaderSocket == socket || transfer->downloaderSocket == socket) {
			return i;
		}
	}
	
	return -1;
}

int Relay::findTransfer (QString key) {
	for (int i = 0; i < transfers.size (); i++) {
		auto transfer = transfers [i];
		if (transfer->key == key) {
			return i;
		}
	}
	
	return -1;
}

ParsedHeader Relay::parseHeaders (const QByteArray buffer) {
	int headerEnd = buffer.indexOf ("\r\n\r\n");
	
	if (headerEnd == -1) {
		// Can't find the end of the HTTP headers
		return ParsedHeader ();
	}
	
	// Assuming we found the end of the HTTP headers (Hopefully these came in one packet)
	ParsedHeader result;
	
	QStringList headers = QString (buffer.left (headerEnd)).split ("\r\n");
	
	if (buffer.startsWith ("GET")) {
		QRegExp re ("^(?:GET|get) (\\S+) (?:HTTP|http)");
		
		re.indexIn (headers.first ());
		
		result.url = re.cap (1);
		
		if (result.url.startsWith ("/" + transferDir + "/")) {
			result.type = EConnectionType::Downloader;
		}
		else {
			result.type = EConnectionType::Info;
		}
	}
	else if (buffer.startsWith ("PUT") || buffer.startsWith ("POST")) {
		result.type = EConnectionType::Uploader;
		QRegExp re ("^P(?:U|OS)T (\\S+) (?:HTTP|http)");
		
		re.indexIn (headers.first ());
		result.fileName = re.cap (1);
		if (! result.fileName.startsWith ("/")) {
			result.fileName.prepend ("/");
		}
		
		foreach (auto header, headers) {
			QRegExp contentLengthRe ("^(?:C|c)ontent-(?:L|l)ength: (\\d+)");
			contentLengthRe.indexIn (header);
			
			auto contentLength = contentLengthRe.cap (1);
			if (! contentLength.isEmpty ()) {
				result.expectedContentLength = contentLength.toLong ();
			}
		}
	}
	else {
		result.type = EConnectionType::Invalid;
	}
	
	return result;
}

void Relay::cullFailedRedirections () {
	QVector <QSharedPointer <Transfer> > newTransfers;
	
	foreach (const QSharedPointer <Transfer> transfer, transfers) {
		if (transfer->uploaderSocket == nullptr && transfer->waitForUploaderTimer.elapsed () > 5 * 1000) {
			// Fail it
			qDebug() << __LINE__ << "Culled old transfer" << transfer->key;
		}
		else {
			newTransfers.append (transfer);
		}
	}
	
	transfers = newTransfers;
}

void Relay::socket_disconnected () {
	auto socket = (QTcpSocket *)sender ();
	
	foreach (QSharedPointer <Transfer> t, transfers) {
		if (t->uploaderSocket == socket) {
			while (t->downloaderSocket != nullptr) {
				t->readData ();
			}
			t->uploaderSocket->deleteLater ();
			t->uploaderSocket = nullptr;
			t->waitForUploaderTimer.start ();
		}
		if (t->downloaderSocket == socket) {
			t->downloaderSocket->deleteLater ();
			t->downloaderSocket = nullptr;
		}
	}
	
	socket->deleteLater ();
	
	discardPendingConnectionBySocket (socket);
}

void Relay::writeHeaders (QTcpSocket & socket, qint64 contentLength) {
	socket.write ("HTTP/1.1 200 OK\r\n");
	socket.write ("Server: " + serverName.toUtf8 () + "\r\n");
	
	if (contentLength >= 0) {
		socket.write ("Content-Length: " + QString::number (contentLength).toUtf8 () + "\r\n");
	}
	
	socket.write ("\r\n");
}

void Relay::socket_readyRead () {
	auto socket = (QTcpSocket *)sender ();
	qDebug() << __LINE__ << "Socket ready read" << socket << socket->bytesAvailable ();
	
	// Or "Pendex"
	int pendingIndex = findPendingConnection (socket);
	int transferIndex = findTransfer (socket);
	
	if (pendingIndex >= 0) {
		PendingConnection & pending = pendingConnections [pendingIndex];
		
		auto newData = socket->readAll ();
		pending.buffer.append (newData);
		
		auto parseResult = parseHeaders (pending.buffer);
		
		switch (parseResult.type) {
			case EConnectionType::Invalid:
				// Stop listening
				qDebug() << __LINE__ << "Invalid" << socket;
				discardPendingConnectionBySocket (socket);
				return;
			case EConnectionType::Unknown:
				// Continue listening
				return;
			case EConnectionType::Downloader:
				qDebug() << __LINE__ << "Downloader detected" << socket;
				// See if we have the matching key
				{
				int transferIndex = findTransfer (parseResult.url);
				if (transferIndex >= 0) {
					Transfer & transfer = *transfers [transferIndex];
					transfer.downloaderSocket = socket;
					
					writeHeaders (*transfer.downloaderSocket, transfer.expectedContentLength);
					
					if (transfer.uploaderSocket != nullptr) {
						//transfer.uploaderSocket->setSocketOption (QAbstractSocket::ReceiveBufferSizeSocketOption, 1061696);
						transfer.uploaderSocket->setReadBufferSize (0);
						
						transfer.buffer.append (transfer.uploaderSocket->readAll ());
						transfer.writeData ();
					}
					
					//pendingConnections.remove (pendingIndex);
				}
				else {
					// We don't have the right key, give up
					QByteArray message ("<html><head><title>404</title></head><body>404 Not Found</body></html>");
					QString response ("HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: " + QString::number (message.length ()) + "\r\n\r\n" + message);
					socket->write (response.toUtf8 ());
					socket->flush ();
					discardPendingConnectionBySocket (socket);
				}
				}
				break;
			case EConnectionType::Uploader:
				qDebug() << __LINE__ << "Uploader detected" << socket;
				if (parseResult.fileName.indexOf ("/", 1) == -1) {
					// No slashes in the filename after the first letter -
					// this is a new upload
					
					QSharedPointer <Transfer> t (new Transfer ());
					t->uploaderSocket = socket;
					t->key = "/" + transferDir + "/" + QString::number (serialNumber) + parseResult.fileName;
					serialNumber++;
					
					int endIndex = pending.buffer.indexOf (httpHeaderEnd);
					t->buffer.append (pending.buffer.mid (endIndex + httpHeaderEnd.length ()));
					
					pendingConnections.remove (pendingIndex);
					
					t->write307 ();
					
					qDebug() << __LINE__ << "Sent 307 for" << t->key << "to" << t->uploaderSocket;
					
					t->expectedContentLength = parseResult.expectedContentLength;
					
					transfers.append (t);
				}
				else {
					pendingConnections.remove (pendingIndex);
					
					// There are already slashes - This is from a 307 redirection
					for (int i = 0; i < transfers.size (); i++) {
						Transfer & t = *transfers [i];
						
						if (t.key == parseResult.fileName && t.uploaderSocket == nullptr) {
							t.uploaderSocket = socket;
							
							t.write100 ();
							
							int endIndex = pending.buffer.indexOf (httpHeaderEnd);
							t.buffer.append (pending.buffer.mid (endIndex + httpHeaderEnd.length ()));
							
							t.expectedContentLength = parseResult.expectedContentLength;
							
							if (t.downloaderSocket == nullptr) {
								//socket->setSocketOption (QAbstractSocket::ReceiveBufferSizeSocketOption, 0);
								socket->setReadBufferSize (1000);
							}
						}
					}
				}
				
				break;
			case EConnectionType::Info:
				QFile f ("index.html");
				f.open (QFile::ReadOnly);
				
				auto response = f.readAll ();
				writeHeaders (*socket, response.length ());
				socket->write (response);
				
				socket->flush ();
				discardPendingConnectionBySocket (socket);
				break;
		}
	}
	else if (transferIndex >= 0) {
		Transfer & transfer = *transfers [transferIndex];
		
		if (socket == transfer.uploaderSocket) {
			transfer.readData ();
		}
	}
	else {
		qDebug() << __LINE__ << "Got data from unknown socket";
	}
}
