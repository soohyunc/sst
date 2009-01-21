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

#include "main.h"
#include "migrate.h"

using namespace SST;


#define MSGPERIOD	10

//#define MAXMSGS		1000

#define MAXMIGRS	10


MigrateTest::MigrateTest()
:	clihost(&sim),
	srvhost(&sim),
	cli(&clihost),
	srv(&srvhost),
	srvs(NULL),
	migrater(&clihost),
	narrived(0),
	nmigrates(0)
{
	curaddr = cliaddr;
	clihost.attach(cliaddr, &link);
	srvhost.attach(srvaddr, &link);

	starttime = clihost.currentTime();
	connect(&migrater, SIGNAL(timeout(bool)), this, SLOT(gotTimeout()));

	connect(&srv, SIGNAL(newConnection()),
		this, SLOT(gotConnection()));
	if (!srv.listen("regress", "SST regression test server",
			"migrate", "Migration test protocol"))
		qFatal("Can't listen on service name");

	// Open a connection to the server
	connect(&cli, SIGNAL(readyReadMessage()), this, SLOT(gotMessage()));
	cli.connectTo(srvhost.hostIdent(), "regress", "migrate");
	cli.connectAt(Endpoint(srvaddr, NETSTERIA_DEFAULT_PORT));

	// Get the ping-pong process going...
	ping(&cli);
}

void MigrateTest::gotConnection()
{
	qDebug() << this << "gotConnection";
	Q_ASSERT(srvs == NULL);

	srvs = srv.accept();
	if (!srvs) return;

	srvs->listen(Stream::Unlimited);

	connect(srvs, SIGNAL(readyReadMessage()), this, SLOT(gotMessage()));
}

void MigrateTest::ping(Stream *strm)
{
	// Send a random-size message
	int p2 = lrand48() % 16;
	QByteArray buf;
	buf.resize(1 << p2);
	//qDebug() << strm << "send msg size" << buf.size();
	strm->writeMessage(buf);
}

void MigrateTest::gotMessage()
{
	Stream *strm = (Stream*)QObject::sender();
	while (true) {
		QByteArray buf = strm->readMessage();
		if (buf.isNull())
			return;

		//qDebug() << strm << "recv msg size" << buf.size();
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
	}
}

void MigrateTest::gotTimeout()
{
	// Find the next IP address to migrate to
	quint32 newip = curaddr.toIPv4Address() + 1;
	if (newip == srvaddr.toIPv4Address())
		newip++;	// don't use same addr as server!
	QHostAddress newaddr = QHostAddress(newip);

	// Migrate!
	qDebug() << "migrating to" << newaddr;
	clihost.detach(curaddr, &link);
	clihost.attach(newaddr, &link);
	curaddr = newaddr;
	srvs->connectAt(Endpoint(newaddr, NETSTERIA_DEFAULT_PORT));

	// Start the next cycle...
	starttime = clihost.currentTime();
	narrived = 0;
	nmigrates++;
}

void MigrateTest::run()
{
	MigrateTest test;
	test.sim.run();

	qDebug() << "Migrate test completed after" << test.nmigrates;
	success = true;
	check(test.nmigrates == MAXMIGRS);
}

