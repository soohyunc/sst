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

#include <stdlib.h>

#include <QtDebug>

#include "flow.h"

#include "main.h"
#include "seg.h"

using namespace SST;


#define FLOW_PORT	NETSTERIA_DEFAULT_PORT


SegTest::SegTest()
:	h1(&sim), h2(&sim), h3(&sim), h4(&sim),
	fs1(&h1, QHostAddress("1.1.34.4")),
	fs4(&h4, QHostAddress("1.1.12.1")),
	fr1(&h1), fr2(&h2), fr3(&h3), fr4(&h4),
	si1(NULL), sr2(NULL), si2(NULL), sr3(NULL), si3(NULL), sr4(NULL),
	cli(&h1),
	srv(&h4),
	srvs(NULL)
{
	// Gather simulation statistics
	connect(&sim, SIGNAL(eventStep()), this, SLOT(gotEventStep()));

	// Set up the linear topology
	l12.connect(&h1, QHostAddress("1.1.12.1"),
		    &h2, QHostAddress("1.1.12.2"));
	l23.connect(&h2, QHostAddress("1.1.23.2"),
		    &h3, QHostAddress("1.1.23.3"));
	l34.connect(&h3, QHostAddress("1.1.34.3"),
		    &h4, QHostAddress("1.1.34.4"));

	// Set up the flow layer forwarding chain (XXX horrible hack)
	fr2.forwardTo(Endpoint(QHostAddress("1.1.23.3"), FLOW_PORT));
	fr3.forwardTo(Endpoint(QHostAddress("1.1.34.4"), FLOW_PORT));
	fr4.forwardUp(&fs4);

	// Initiate a flow layer connection
	si1 = fs1.initiateTo(Endpoint(QHostAddress("1.1.12.2"), FLOW_PORT));

	// Set up the application-level server
	connect(&srv, SIGNAL(newConnection()),
		this, SLOT(gotConnection()));
	if (!srv.listen("regress", "SST regression test server",
			"seg", "Flow segmentation test protocol"))
		qFatal("Can't listen on service name");

	// Open a connection to the server
	connect(&cli, SIGNAL(readyRead()), this, SLOT(gotData()));
	connect(&cli, SIGNAL(readyReadMessage()), this, SLOT(gotMessage()));
	cli.connectTo(h4.hostIdent(), "regress", "seg");
	cli.connectAt(Endpoint(QHostAddress("1.1.34.4"), NETSTERIA_DEFAULT_PORT));

	// Get the ping-pong process going...
	ping(&cli);
}

void SegTest::gotConnection()
{
	qDebug() << this << "gotConnection";
	Q_ASSERT(srvs == NULL);

	srvs = srv.accept();
	if (!srvs) return;

	srvs->listen(Stream::Unlimited);

	connect(srvs, SIGNAL(readyReadMessage()), this, SLOT(gotMessage()));

	// All the flows should now exist
	sr2 = fr2.lastiseg;	si2 = fr2.lastoseg;
	sr3 = fr3.lastiseg;	si3 = fr3.lastoseg;
	sr4 = fr4.lastiseg;
}

void SegTest::ping(Stream *strm)
{
	// Send a random-size message
	int p2 = 22; 	// lrand48() % 16;
	QByteArray buf;
	buf.resize(1 << p2);
	//qDebug() << strm << "send msg size" << buf.size();
	strm->writeMessage(buf);
}

void SegTest::gotData()
{
	Stream *strm = (Stream*)QObject::sender();
	while (true) {
		QByteArray buf = strm->readData();
		qDebug() << strm << "recv data size" << buf.size();
	}
}

void SegTest::gotMessage()
{
	Stream *strm = (Stream*)QObject::sender();
	while (true) {
		QByteArray buf = strm->readMessage();
		if (buf.isNull())
			return;

		qDebug() << strm << "recv msg size" << buf.size();

#if 0
		narrived++;

		if (narrived == MSGPERIOD) {
			timeperiod = clihost.currentTime().usecs
				- starttime.usecs;
			qDebug() << "migr" << nmigrates << "timeperiod:"
				<< (double)timeperiod / 1000000.0;

			// Wait some random addional time before migrating,
			// to ensure that migration can happen at
			// "unexpected moments"...
			migrater.start(timeperiod * drand48());
		}

		// Keep ping-ponging until we're done.
		if (nmigrates < MAXMIGRS)
			ping(strm);
#endif
	}
}

void SegTest::gotEventStep()
{
	if (!si1 || !sr4)
		return;		// flows not yet setup

	// Initiator-to-responder path
	qDebug() << sim.currentTime().usecs
		<< si1->txPacketsInFlight() << "/" << si1->txCongestionWindow()
		<< si2->txPacketsInFlight() << "/" << si2->txCongestionWindow()
		<< si3->txPacketsInFlight() << "/" << si3->txCongestionWindow();

	// Responder-to-initiator path
	//qDebug() << sim.currentTime().usecs
	//	<< sr4->txPacketsInFlight() << "/" << sr4->txCongestionWindow()
	//	<< sr3->txPacketsInFlight() << "/" << sr3->txCongestionWindow()
	//	<< sr2->txPacketsInFlight() << "/" << sr2->txCongestionWindow();
}

void SegTest::run()
{
	SegTest test;
	test.sim.run();
	success = true;
}

