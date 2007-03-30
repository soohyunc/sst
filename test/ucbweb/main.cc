
#include <string.h>
#include <stdlib.h>

#include <QHostInfo>
#include <QHostAddress>
#include <QCoreApplication>
#include <QtDebug>

#include "sock.h"
#include "host.h"

#include "main.h"
#include "cli.h"
#include "srv.h"
#include "sim.h"

using namespace SST;


TestProto SST::testproto = TESTPROTO_SST;

void usage(const char *appname)
{
	fprintf(stderr, "Usage:\n"
			"(client) %s <hostname> [<port>]\n"
			"(server) %s server [<port>]\n",
		appname, appname);
	exit(1);
}

void simulate()
{
	Simulator sim;

	QHostAddress cliaddr("1.2.3.4");
	QHostAddress srvaddr("4.3.2.1");

	SimHost clihost(&sim, cliaddr);
	SimHost srvhost(&sim, srvaddr);

	TestClient cli(&clihost, srvaddr, NETSTERIA_DEFAULT_PORT);
	TestServer srv(&srvhost, NETSTERIA_DEFAULT_PORT);

	sim.run();
	qDebug() << "Simultation completed";
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);

	int i = 1;

	// Target host name if client, or "server" keyword
	bool issim = false;
	bool isserver = false;
	QHostAddress hostaddr;
	if (argc <= i)
		usage(argv[0]);
	if (strcasecmp(argv[i], "sim") == 0) {
		issim = true;
	} else if (strcasecmp(argv[i], "server") == 0) {
		isserver = true;
	} else {	// client
		hostaddr = QHostAddress(argv[i]);
		if (hostaddr.isNull()) {
			QString hostname = argv[i];
			QHostInfo hostinfo = QHostInfo::fromName(hostname);
			QList<QHostAddress> addrs = hostinfo.addresses();
			Q_ASSERT(addrs.size() > 0);
			hostaddr = addrs[0];
		}
	}
	i++;

	// Port number
	int port = NETSTERIA_DEFAULT_PORT;
	if (argc > i) {
		bool ok;
		port = QString(argv[i]).toInt(&ok);
		if (!ok || port <= 0 || port > 65535)
			usage(argv[0]);
		i++;
	}

	// Should be no more arguments
	if (argc > i)
		usage(argv[0]);

	if (issim)
		simulate();
	else if (isserver)
		new TestServer(new Host(NULL, port), port);
	else
		new TestClient(new Host(NULL, port), hostaddr, port);

	return app.exec();
}

