#ifndef RELAY_H
#define RELAY_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QSharedPointer>
#include <QString>
#include <QTcpServer>
#include <QVector>

class QTcpSocket;

class PendingConnection {
public:
	QTcpSocket * socket;
	QByteArray buffer;
	
	PendingConnection ();
};

class Transfer {
public:
	QTcpSocket * uploaderSocket;
	QTcpSocket * downloaderSocket;
	QString key;
	QByteArray buffer;
	qint64 bufferLimit;
	qint64 totalBytesWritten;
	qint64 expectedContentLength;
	
	QElapsedTimer waitForUploaderTimer;
	
	Transfer ();
	~Transfer ();
	
	qint64 maxRead () const;
	
	void readData ();
	
	void write100 ();
	void write307 ();
	void writeData ();
};

enum class EConnectionType {
	Unknown,
	Invalid,
	Uploader,
	Downloader,
	Info
};

class ParsedHeader {
public:
	EConnectionType type;
	// For uploaders
	QString fileName;
	qint64 expectedContentLength;
	// For downloaders
	QString url;
	
	ParsedHeader () {
		type = EConnectionType::Unknown;
	}
};

class RelaySettings {
public:
	qint32 port;
	
	RelaySettings ();
	RelaySettings (int, char * []);
	
	void setToDefaults ();
};

class Relay : public QObject
{
	Q_OBJECT
public:
	explicit Relay(RelaySettings, QObject *parent = 0);
	
signals:
	
public slots:
	
protected:
	RelaySettings settings;
	qint64 serialNumber;
	qint64 bufferSize;
	QTcpServer server;
	QByteArray httpHeaderEnd;
	QByteArray transferDir;
	QString serverName;
	
	QVector <PendingConnection> pendingConnections;
	QVector <QSharedPointer <Transfer> > transfers;
	
	void discardPendingConnectionBySocket (QTcpSocket *);
	void discardTransferBySocket (QTcpSocket *);
	
	int findPendingConnection (QTcpSocket *);
	int findTransfer (QTcpSocket *);
	int findTransfer (QString);
	
	ParsedHeader parseHeaders (const QByteArray);
	void cullFailedRedirections ();
	
	void writeHeaders (QTcpSocket &, qint64);
	
protected slots:
	void server_newConnection ();
	
	void socket_disconnected ();
	void socket_readyRead ();
};

#endif // RELAY_H
