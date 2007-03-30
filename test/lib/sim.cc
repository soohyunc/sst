
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
static const int RATE = 1500000/8; // 1.5Mbps DSL connection
static const int DELAY = 55000/2; // uS ping - typ for my DSL connection

// 56Mbps 802.11 LAN
//static const int RATE = 56000000/8; // 56Mbps wireless LAN
//static const int DELAY = 750/2; // 750uS ping - typ for my DSL connection

#define CUTOFF	(DELAY*10)	// max delay before we drop packets
#define PKTOH	32		// Bytes of link/inet overhead per packet


static bool tracepkts = false;


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

SimPacket::SimPacket(SimHost *srch, const Endpoint &src, const Endpoint &dst,
			const char *data, int size)
:	QObject(srch->sim),
	sim(srch->sim),
	srch(srch), dsth(srch->sim->hosts.value(dst.addr)),
	src(src), dst(dst),
	buf(data, size), timer(srch, this)
{
	Q_ASSERT(dsth);
	Q_ASSERT(srch->addr == src.addr);

	// Compute the amount of wire time this packet takes to transmit,
	// including some per-packet link/inet overhead
	qint64 psize = buf.size() + PKTOH;
	qint64 ptime = psize * 1000000 / RATE;

	// Decide when this packet will/would arrive at the destination,
	// by counting from the minimum ping delay or the last arrival,
	// whichever is later.
	qint64 arrival = qMax(sim->cur.usecs + DELAY, dsth->arrival) + ptime;

	// If the computed arrival time is too late, drop this packet.
	bool drop = arrival > sim->cur.usecs + CUTOFF;

	quint32 seqno = ntohl(*(quint32*)buf.data()) & 0xffffff;
	if (tracepkts)
	if (src.addr == QHostAddress("1.2.3.4"))
		printf("%12lld:\t\t     --> %6d %4d --> %4s\n",
			sim->cur.usecs, seqno, buf.size(), drop ? "DROP" : "");
	else
		printf("%12lld:\t\t\t\t%4s <-- %6d %4d <--\n",
			sim->cur.usecs, drop ? "DROP" : "", seqno, buf.size());

	if (drop) {
		deleteLater();
		return;
	}
	dsth->arrival = arrival;

	connect(&timer, SIGNAL(timeout(bool)), this, SLOT(arrive()));
	timer.start(dsth->arrival - sim->cur.usecs);
}

void SimPacket::arrive()
{
	timer.stop();

	quint32 seqno = ntohl(*(quint32*)buf.data()) & 0xffffff;
	if (tracepkts)
	if (src.addr == QHostAddress("1.2.3.4"))
		printf("%12lld:\t\t\t\t\t\t        --> %6d %4d\n",
			sim->cur.usecs, seqno, buf.size());
	else
		printf("%12lld: %6d %4d <--\n",
			sim->cur.usecs, seqno, buf.size());

	SimSocket *dsts = dsth->socks.value(dst.port);
	Q_ASSERT(dsts);

	SocketEndpoint sep(src, dsts);
	dsts->receive(buf, sep);

	deleteLater();
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

	Endpoint src(host->addr, port);
	(void)new SimPacket(host, src, dst, data, size);
	return true;
}

QList<Endpoint> SimSocket::localEndpoints()
{
	Q_ASSERT(port);

	QList<Endpoint> l;
	l.append(Endpoint(host->addr, port));
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

SimHost::SimHost(Simulator *sim, QHostAddress addr)
:	sim(sim), addr(addr), arrival(0)
{
	Q_ASSERT(!sim->hosts.contains(addr));
	sim->hosts.insert(addr, this);

	initSocket(NULL);
	initHostIdent(NULL);
}

SimHost::~SimHost()
{
	// Unbind all sockets from this host
	foreach (quint16 port, socks.keys())
		socks[port]->unbind();

	// De-register this host from the simulator
	Q_ASSERT(sim->hosts.value(addr) == this);
	sim->hosts.remove(addr);
}

Time SimHost::currentTime()
{
	return sim->cur;
}

TimerEngine *SimHost::newTimerEngine(Timer *timer)
{
	return new SimTimerEngine(sim, timer);
}

Socket *SimHost::newSocket(QObject *parent)
{
	return new SimSocket(this, parent);
}


////////// Simulator //////////

Simulator::Simulator()
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

void Simulator::run()
{
	//qDebug() << "Simulator::run()";
	while (!timers.isEmpty()) {
		SimTimerEngine *next = timers.dequeue();
		Q_ASSERT(next->wake >= cur.usecs);

		// Move the virtual system clock forward to this event
		cur.usecs = next->wake;
		next->wake = -1;

		// Dispatch the event
		next->timeout();

		// Process any Qt events such as delayed signals
		QCoreApplication::processEvents();
	}
	//qDebug() << "Simulator::run() done";
}

