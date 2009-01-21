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
#include <netinet/in.h>

#include <QCoreApplication>
#include <QtDebug>

#include "sim.h"

using namespace SST;


// 56Kbps modem connection parameters
//static const int RATE = 53000/8; // 56Kbps = 53Kbps effective w/ overhead
//static const int DELAY = 200000/2; // uS ping

// DSL-level connection parameters
//static const int RATE = 1400000/8; // 1.5Mbps DSL connection
//static const int DELAY = 120000/2; // uS ping - typ for my DSL connection

// Low-delay DSL connection to MIT
//static const int RATE = 1500000/8; // 1.5Mbps DSL connection
//static const int DELAY = 55000/2; // uS ping - typ for my DSL connection

// 56Mbps 802.11 LAN
//static const int RATE = 56000000/8; // 56Mbps wireless LAN
//static const int DELAY = 750/2; // 750uS ping - typ for my DSL connection

//#define CUTOFF	(DELAY*10)	// max delay before we drop packets
#define PKTOH	32		// Bytes of link/inet overhead per packet


static bool tracepkts = false;


// Cheesy way to distinguish "clients" from "servers" in our tests,
// for purposes of pretty-printing a left/right packet trace:
// "clients" have IP addresses of the form 1.x.x.x.
static inline bool isclient(const QHostAddress &addr)
{
	return (addr.toIPv4Address() >> 24) == 1;
}


////////// SimTimerEngine //////////

SimTimerEngine::SimTimerEngine(Simulator *sim, Timer *t)
:	TimerEngine(t), sim(sim), wake(-1)
{
}

SimTimerEngine::~SimTimerEngine()
{
	stop();
}

void SimTimerEngine::start(quint64 usecs)
{
	stop();

	wake = sim->cur.usecs + usecs;
	//qDebug() << "start timer for" << wake;

	int pos = 0;
	while (pos < sim->timers.size() && sim->timers[pos]->wake <= wake)
		pos++;
	sim->timers.insert(pos, this);
}

void SimTimerEngine::stop()
{
	if (wake < 0)
		return;

	//qDebug() << "stop timer at" << wake;
	sim->timers.removeAll(this);
	wake = -1;
}


////////// SimPacket //////////

SimPacket::SimPacket(SimHost *srch, const Endpoint &src, 
			SimLink *lnk, const Endpoint &dst,
			const char *data, int size)
:	QObject(srch->sim),
	sim(srch->sim),
	src(src), dst(dst), dsth(NULL),
	buf(data, size), timer(srch, this)
{
	Q_ASSERT(srch->links.value(src.addr) == lnk);

	// Find the destination on the appropriate incident link;
	// drop the packet if destination host not found.
	dsth = lnk->hosts.value(dst.addr);
	if (!dsth) {
		qDebug() << this << "target host"
			<< dst.addr.toString() << "not on specified link!";
		deleteLater();
		return;
	}

	qint64 curusecs = sim->currentTime().usecs;

	// Compute the amount of wire time this packet takes to transmit,
	// including some per-packet link/inet overhead
	qint64 psize = buf.size() + PKTOH;
	qint64 ptime = psize * 1000000 / lnk->netrate;

	// Nominal arrival time based only on network delay
	qint64 nomarrival = curusecs + lnk->netdelay;

	// Decide when this packet will/would arrive at the destination,
	// by counting from the minimum ping delay or the last arrival,
	// whichever is later.
	qint64 arrival = qMax(nomarrival, dsth->arrival) + ptime;

	// If the computed arrival time is too late, drop this packet.
	qint64 mtutime = 1200 * 1000000 / lnk->netrate;
	bool drop = arrival > nomarrival + (mtutime * lnk->netbufmul);

	quint32 seqno = ntohl(*(quint32*)buf.data()) & 0xffffff;
	if (tracepkts)
	if (isclient(src.addr))
		qDebug("%12lld:\t\t     --> %6d %4d --> %4s  @%lld",
			curusecs, seqno, buf.size(), drop ? "DROP" : "",
			drop ? 0 : arrival);
	else
		qDebug("%12lld:\t\t\t\t%4s <-- %6d %4d <--  @%lld",
			curusecs, drop ? "DROP" : "", seqno, buf.size(),
			drop ? 0 : arrival);

	if (drop) {
		deleteLater();
		return;
	}
	dsth->arrival = arrival;

	// Add it to the host's receive packet queue
	dsth->pqueue.append(this);

	connect(&timer, SIGNAL(timeout(bool)), this, SLOT(arrive()));
	timer.start(dsth->arrival - curusecs);
}

void SimPacket::arrive()
{
	// Make sure we're still on the destination host's queue
	if (!dsth || !dsth->pqueue.contains(this)) {
		qDebug() << this << "no longer queued to destination host"
			<< dst.addr.toString();
		return deleteLater();
	}

	timer.stop();

	quint32 seqno = ntohl(*(quint32*)buf.data()) & 0xffffff;
	if (tracepkts)
	if (isclient(src.addr))
		qDebug("%12lld:\t\t\t\t\t\t        --> %6d %4d",
			sim->currentTime().usecs, seqno, buf.size());
	else
		qDebug("%12lld: %6d %4d <--",
			sim->currentTime().usecs, seqno, buf.size());

	SimSocket *dsts = dsth->socks.value(dst.port);
	if (!dsts) {
		qDebug() << this << "target has no listener on port"
			<< dst.port;
		return deleteLater();
	}

	// Remove this packet from the destination host's queue
	dsth->pqueue.removeAll(this);
	dsth = NULL;

	SocketEndpoint sep(src, dsts);
	dsts->receive(buf, sep);

	deleteLater();
}

SimPacket::~SimPacket()
{
	if (dsth)
		dsth->pqueue.removeAll(this);
}


////////// SimSocket //////////

SimSocket::SimSocket(SimHost *host, QObject *parent)
:	Socket(host, parent),
	sim(host->sim),
	host(host),
	port(0)
{
}

SimSocket::~SimSocket()
{
	unbind();
}

bool SimSocket::bind(const QHostAddress &addr, quint16 port,
			QUdpSocket::BindMode)
{
	Q_ASSERT(!this->port);
	Q_ASSERT(addr == QHostAddress::Any);

	if (port == 0) {
		for (port = 1; host->socks.contains(port); port++)
			Q_ASSERT(port < 65535);
	}

	Q_ASSERT(!host->socks.contains(port));
	host->socks.insert(port, this);
	this->port = port;

	//qDebug() << "Bound virtual socket on host" << host->addr.toString()
	//	<< "port" << port;

	setActive(true);
	return true;
}

void SimSocket::unbind()
{
	if (port) {
		Q_ASSERT(host->socks.value(port) == this);
		host->socks.remove(port);
		port = 0;
		setActive(false);
	}
}

bool SimSocket::send(const Endpoint &dst, const char *data, int size)
{
	Q_ASSERT(port);

	// Find the neighbor host and link given the destination address
	Endpoint src;
	src.port = port;
	SimHost *dsth = host->neighborAt(dst.addr, src.addr);
	if (!dsth) {
		qDebug() << this << "unknown or un-adjacent target host"
			<< dst.addr.toString();
		return false;
	}

	SimLink *link = host->linkAt(src.addr);
	Q_ASSERT(link);

	(void)new SimPacket(host, src, link, dst, data, size);
	return true;
}

QList<Endpoint> SimSocket::localEndpoints()
{
	Q_ASSERT(port);

	QList<Endpoint> l;
	foreach (const QHostAddress &addr, host->links.keys()) {
		l.append(Endpoint(addr, port));
	}
	return l;
}

quint16 SimSocket::localPort()
{
	return port;
}

QString SimSocket::errorString()
{
	return QString();	// XXX
}


////////// SimHost //////////

SimHost::SimHost(Simulator *sim)
:	sim(sim), arrival(0)
{
	initSocket(NULL);

	// expensive, and can be done lazily if we need a cryptographic ID...
	//initHostIdent(NULL);
}

SimHost::~SimHost()
{
	// Unbind all sockets from this host
	foreach (quint16 port, socks.keys())
		socks[port]->unbind();

	// Detach this host from all network links
	foreach (const QHostAddress addr, links.keys()) {
		detach(addr, links.value(addr));
	}
	Q_ASSERT(links.empty());

	// Detach and delete all packets queued to this host
	foreach (SimPacket *pkt, pqueue) {
		Q_ASSERT(pkt->dsth == this);
		pkt->dsth = NULL;
		pkt->deleteLater();
	}
}

Time SimHost::currentTime()
{
	return sim->realtime ? Host::currentTime() : sim->cur;
}

TimerEngine *SimHost::newTimerEngine(Timer *timer)
{
	return sim->realtime
		? Host::newTimerEngine(timer)
		: new SimTimerEngine(sim, timer);
}

Socket *SimHost::newSocket(QObject *parent)
{
	return new SimSocket(this, parent);
}

void SimHost::attach(const QHostAddress &addr, SimLink *link)
{
	Q_ASSERT(!links.contains(addr));
	Q_ASSERT(!link->hosts.contains(addr));

	links.insert(addr, link);
	link->hosts.insert(addr, this);
}

void SimHost::detach(const QHostAddress &addr, SimLink *link)
{
	Q_ASSERT(link);
	Q_ASSERT(links.value(addr) == link);
	Q_ASSERT(link->hosts.value(addr) == this);

	links.remove(addr);
	link->hosts.remove(addr);
}

SimHost *SimHost::neighborAt(const QHostAddress &dstaddr, QHostAddress &srcaddr)
{
	QHashIterator<QHostAddress, SimLink*> i(links);
	while (i.hasNext()) {
		i.next();
		SimLink *l = i.value();
		SimHost *h = l->hosts.value(dstaddr);
		if (h) {
			srcaddr = i.key();
			return h;
		}
	}
	return NULL;	// not found
}


////////// SimLink //////////

SimLink::SimLink(LinkPreset preset)
{
	netbufmul = 10;	// XXX ???

	switch (preset) {
	default:
		Q_ASSERT(0);
	case Eth100:
		netrate = 100000/8;
		netdelay = 1000/2;
		break;
	case Eth1000:
		netrate = 100000/8;
		netdelay = 650/2;
		break;
	}
}

SimLink::~SimLink()
{
	// Detach this link from all hosts
	foreach (const QHostAddress addr, hosts.keys()) {
		hosts.value(addr)->detach(addr, this);
	}
	Q_ASSERT(hosts.empty());
}


////////// Simulator //////////

Simulator::Simulator(bool realtime)
:	realtime(realtime)
{
	cur.usecs = 0;
}

Simulator::~Simulator()
{
	//qDebug() << "~" << this;
	// Note that there may still be packets in the simulated network,
	// and simulated timers representing their delivery time -
	// but they should all get garbage collected at this point.
}

Time Simulator::currentTime()
{
	if (realtime)
		return Time::fromQDateTime(QDateTime::currentDateTime());
	else
		return cur;
}

void Simulator::run()
{
	//qDebug() << "Simulator::run()";
	if (realtime)
		qFatal("Simulator::run() is only for use with virtual time:\n"
			"for real time, use QCoreApplication::exec() instead.");

	while (!timers.isEmpty()) {
		SimTimerEngine *next = timers.dequeue();
		Q_ASSERT(next->wake >= cur.usecs);

		// Move the virtual system clock forward to this event
		cur.usecs = next->wake;
		next->wake = -1;

		// Dispatch the event
		next->timeout();

		// Process any Qt events such as delayed signals
		QCoreApplication::processEvents(QEventLoop::DeferredDeletion);

		//qDebug() << "";	// print blank lines at time increments
	}
	//qDebug() << "Simulator::run() done";
}

