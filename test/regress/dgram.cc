/*
 * XX other things this should test:
 *	- queueing datagrams immediately without waiting for linkUp()
 *	- sending datagrams on multiple streams at once,
 *	  to make sure fragmented dgrams interleave properly.
 *	- response to different line error rates
 */

#include <QtDebug>

#include "main.h"
#include "dgram.h"

using namespace SST;


#define NDGRAMS	100


static const int maxDgramP2 = 15;	// XXX Max dgram size: 2^20 = 1MB
static const int maxDgramSize = 1 << maxDgramP2;


DatagramTest::DatagramTest()
:	clihost(&sim, cliaddr),
	srvhost(&sim, srvaddr),
	cli(&clihost),
	srv(&srvhost),
	srvs(NULL),
	narrived(0)
{
	connect(&srv, SIGNAL(newConnection()),
		this, SLOT(gotConnection()));
	if (!srv.listen("regress", "SST regression test server",
			"dgram", "Datagram test protocol"))
		qFatal("Can't listen on service name");

	//connect(&cli, SIGNAL(linkUp()), this, SLOT(gotLinkUp()));
	cli.connectTo(Ident::fromIpAddress(
				srvaddr, NETSTERIA_DEFAULT_PORT).id(),
			"regress", "dgram");

	// Push the protocol by starting to stuff datagrams in
	// before the stream has even connected...
	gotLinkUp();
}

void DatagramTest::gotLinkUp()
{
	qDebug() << this << "linkUp";

	// Fire off a bunch of datagrams
	int p2 = 4;	// Min dgram size: 2^4 = 16 bytes
	QByteArray buf;
	for (int i = 0; i < NDGRAMS; i++) {
		buf.resize(1 << p2);
		cli.writeDatagram(buf, false);
		if (++p2 > maxDgramP2)
			p2 = 4;
	}
}

void DatagramTest::gotConnection()
{
	qDebug() << this << "gotConnection";
	Q_ASSERT(srvs == NULL);

	srvs = srv.accept();
	if (!srvs) return;

	srvs->setChildReceiveBuffer(maxDgramSize);
	srvs->listen(Stream::BufLimit);

	connect(srvs, SIGNAL(readyReadDatagram()),
		this, SLOT(gotDatagram()));
	gotDatagram();
}

void DatagramTest::gotDatagram()
{
	QByteArray dg = srvs->readDatagram();
	if (dg.isEmpty())
		return;

	qDebug() << "Got datagram size" << dg.size();
	narrived++;
}

void DatagramTest::run()
{
	DatagramTest test;
	test.sim.run();

	qDebug() << "Datagram test complete:" << test.narrived
		<< "of" << NDGRAMS << "delivered";
	success = true;
	check(test.narrived >= NDGRAMS*90/100);
}

