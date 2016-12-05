#include <QCoreApplication>

#include "relay.h"

int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);
	
	RelaySettings settings (argc, argv);
	
	Relay relay (settings);
	
	return a.exec();
}
