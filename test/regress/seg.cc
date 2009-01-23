/*
 * Structured Stream Transport
 * Copyright (C) 2006-2008 Massachusetts Institute of Technology
 * Author: Bryan Ford
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>

#include <QTextStream>
#include <QtDebug>

#include "flow.h"

#include "main.h"
#include "seg.h"

using namespace SST;


#define FLOW_PORT	NETSTERIA_DEFAULT_PORT

#define SEGS 2


QTextStream out(stdout);


SegTest::SegTest()
:	h1(&sim), h2(&sim), h3(&sim), h4(&sim),
	fs1(&h1, QHostAddress("1.1.34.4")),
	fs4(&h4, QHostAddress("1.1.12.1")),
	fr1(&h1), fr2(&h2), fr3(&h3), fr4(&h4),
	si1(NULL), sr2(NULL), si2(NULL), sr3(NULL), si3(NULL), sr4(NULL),
	cli(&h1),
	srv(&h4),
	srvs(NULL),
	sendcnt(0), recvcnt(0), sendtot(0), recvtot(0), delaytot(0),
	recvtotlast(0), recvtimelast(0), recvrate(0)
{
	// Gather simulation statistics
	connect(&sim, SIGNAL(eventStep()), this, SLOT(gotEventStep()));

	// Set link parameters
	l12.setPreset(DSL15);
	//l23.setPreset(Eth10);
	//l23.setLinkRate(50*1024*1024/8);

	// Set up the linear topology and flow layer forwarding chain
	switch (SEGS) {
	case 1:
		// Link topology
		l12.connect(&h1, QHostAddress("1.1.12.1"),
			    &h4, QHostAddress("1.1.34.4"));
		//l12.connect(&h4, QHostAddress("1.1.34.4"),	// inverted
		//	    &h1, QHostAddress("1.1.12.1"));

		// Set up the flow layer forwarding chain (XXX horrible hack)
		fr4.forwardUp(&fs4);

		// Initiate a flow layer connection
		si1 = fs1.initiateTo(
			Endpoint(QHostAddress("1.1.34.4"), FLOW_PORT));

		break;

	case 2:
		// Link topology
		l12.connect(&h1, QHostAddress("1.1.12.1"),
			    &h2, QHostAddress("1.1.12.2"));
		l23.connect(&h2, QHostAddress("1.1.23.2"),
			    &h4, QHostAddress("1.1.34.4"));

		// Set up the flow layer forwarding chain (XXX horrible hack)
		fr2.forwardTo(Endpoint(QHostAddress("1.1.34.4"), FLOW_PORT));
		fr4.forwardUp(&fs4);

		// Initiate a flow layer connection
		si1 = fs1.initiateTo(
			Endpoint(QHostAddress("1.1.12.2"), FLOW_PORT));

		break;

	case 3:
		// Link topology
		l12.connect(&h1, QHostAddress("1.1.12.1"),
			    &h2, QHostAddress("1.1.12.2"));
		l23.connect(&h2, QHostAddress("1.1.23.2"),
			    &h3, QHostAddress("1.1.23.3"));
		l34.connect(&h3, QHostAddress("1.1.34.3"),
			    &h4, QHostAddress("1.1.34.4"));

		// Set up the flow layer forwarding chain (XXX horrible hack)
		fr2.forwardTo(Endpoint(QHostAddress("1.1.23.3"), FLOW_PORT));
		fr2.forwardTo(Endpoint(QHostAddress("1.1.23.3"), FLOW_PORT));
		fr3.forwardTo(Endpoint(QHostAddress("1.1.34.4"), FLOW_PORT));
		fr4.forwardUp(&fs4);

		// Initiate a flow layer connection
		si1 = fs1.initiateTo(
			Endpoint(QHostAddress("1.1.12.2"), FLOW_PORT));

		break;
	default:
		Q_ASSERT(0);
	}

	// Set up the application-level server
	connect(&srv, SIGNAL(newConnection()),
		this, SLOT(srvConnection()));
	if (!srv.listen("regress", "SST regression test server",
			"seg", "Flow segmentation test protocol"))
		qFatal("Can't listen on service name");

	// Open a connection to the server
	cli.connectTo(h4.hostIdent(), "regress", "seg");
	cli.connectAt(Endpoint(QHostAddress("1.1.34.4"), NETSTERIA_DEFAULT_PORT));
}

void SegTest::cliReadyWrite()
{
	qint64 data[2] = { 0x12345678, sim.currentTime().usecs };

	// Send one MTU-size message at a time
	QByteArray buf((char*)&data, sizeof(data));
	buf.resize(StreamProtocol::mtu);	// XXX

	//qDebug() << &cli << "send msg size" << buf.size();
	cli.writeMessage(buf);
	sendcnt++;
	sendtot += buf.size();
}

void SegTest::srvConnection()
{
	qDebug() << this << "srvConnection";
	Q_ASSERT(srvs == NULL);

	// All the flows should now exist
	sr2 = fr2.lastiseg;	si2 = fr2.lastoseg;
	sr3 = fr3.lastiseg;	si3 = fr3.lastoseg;
	sr4 = fr4.lastiseg;
	Q_ASSERT(sr4);

	//si1->setCCMode(CC_VEGAS);
	//si2->setCCMode(CC_VEGAS);
	//si3->setCCMode(CC_VEGAS);

	srvs = srv.accept();
	if (!srvs) return;

	srvs->listen(Stream::Unlimited);

	connect(srvs, SIGNAL(readyReadMessage()), this, SLOT(srvMessage()));


	// Now get the client going (XXX horrible hack)
	connect(&cli, SIGNAL(readyWrite()), this, SLOT(cliReadyWrite()));
	cliReadyWrite();
}

void SegTest::srvMessage()
{
	while (true) {
		QByteArray buf = srvs->readMessage();
		if (buf.isNull())
			return;
		recvcnt++;
		recvtot += buf.size();

		qDebug() << srvs << "recv msg size" << buf.size();

		Q_ASSERT(buf.size() == StreamProtocol::mtu);	// XXX
		qint64 *data = (qint64*)buf.data();
		Q_ASSERT(data[0] == 0x12345678);

		qint64 sendtime = data[1];
		qint64 recvtime = sim.currentTime().usecs;
		qint64 delay = recvtime - sendtime;
		delaytot += delay;
		double avgdelay = delaytot / recvcnt;

		//printf("%lld %lld %d\n", sendtime, recvtime, buf.size());
#if 0
		out << sendtime/1000000.0 << " " << recvtime/1000000.0 << " "
			<< delay << " " << avgdelay << " "
			<< recvtot << " "
			<< buf.size() << endl;
#endif

		//recvlast = sim.currentTime();
	}
}

void SegTest::gotEventStep()
{
	if (!si1 || !sr4)
		return;		// flows not yet setup

#if 0
	// Bandwidth monitoring
	qint64 curtime = sim.currentTime().usecs;
	if (recvtot > recvtotlast && curtime > recvtimelast) {
		double instrate = (double)(recvtot - recvtotlast) /
					((curtime - recvtimelast)/1000000.0);
		recvrate = (recvrate*99.0 + instrate)/100.0;

		recvtotlast = recvtot;
		recvtimelast = curtime;

		out << curtime/1000000.0 << " " << recvtot << " "
			<< instrate << " " << recvrate << " "
			<< endl;
	}
#endif

#if 1
	// Windows and queues on initiator-to-responder path
	out << (double)sim.currentTime().usecs/1000000.0 << " "
		<< sendtot << " " << recvtot << " "
		<< si1->txPacketsInFlight() << " "
		<< sr2->rxQueuedPackets() << " "
		<< si2->txPacketsInFlight() << " "
		//<< sr3->rxQueuedPackets() << " "
		//<< si3->txPacketsInFlight() << " "
		<< sr4->rxQueuedPackets() << endl;
#endif

#if 0
	// Responder-to-initiator path
	qDebug() << sim.currentTime().usecs
		<< sr4->txPacketsInFlight() << "/" << sr4->txCongestionWindow()
		<< sr3->txPacketsInFlight() << "/" << sr3->txCongestionWindow()
		<< sr2->txPacketsInFlight() << "/" << sr2->txCongestionWindow();
#endif
}

void SegTest::run()
{
	SegTest test;
	test.sim.run();
	success = true;
}

