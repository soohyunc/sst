#ifndef SIM_H
#define SIM_H

#include <QHash>
#include <QQueue>
#include <QHostAddress>

#include "host.h"

namespace SST {

class SimHost;
class SimLink;
class Simulator;

class SimTimerEngine : public TimerEngine
{
	friend class SimHost;
	friend class Simulator;

private:
	Simulator *sim;
	qint64 wake;

protected:
	SimTimerEngine(Simulator *sim, Timer *t);
	~SimTimerEngine();

	virtual void start(quint64 usecs);
	virtual void stop();
};

class SimPacket : public QObject
{
	friend class SimHost;
	Q_OBJECT

private:
	Simulator *sim;
	Endpoint src, dst;
	SimHost *dsth;
	QByteArray buf;
	Timer timer;

public:
	SimPacket(SimHost *srch, const Endpoint &src, 
			SimLink *link, const Endpoint &dst,
			const char *data, int size);
	~SimPacket();

private slots:
	void arrive();
};

class SimSocket : public Socket
{
	friend class SimPacket;
	Q_OBJECT

private:
	Simulator *const sim;
	SimHost *const host;
	quint16 port;

public:
	SimSocket(SimHost *h, QObject *parent = NULL);
	~SimSocket();

	virtual bool bind(const QHostAddress &addr = QHostAddress::Any,
		quint16 port = 0,
		QUdpSocket::BindMode mode = QUdpSocket::DefaultForPlatform);
	virtual void unbind();

	virtual bool send(const Endpoint &ep, const char *data, int size);
	virtual QList<Endpoint> localEndpoints();
	virtual quint16 localPort();
	virtual QString errorString();
};

class SimHost : public Host
{
	friend class SimTimerEngine;
	friend class SimPacket;
	friend class SimSocket;

private:
	Simulator *sim;

	// Virtual network links to which this host is attached
	QHash<QHostAddress, SimLink*> links;

	// Sockets bound on this host
	QHash<quint16, SimSocket*> socks;

	// Queue of packets to be delivered to this host
	QList<SimPacket*> pqueue;

	// Minimum network arrival time for next packet to be received
	qint64 arrival;

public:
	SimHost(Simulator *sim);
	~SimHost();

	virtual Time currentTime();
	virtual TimerEngine *newTimerEngine(Timer *timer);

	virtual Socket *newSocket(QObject *parent = NULL);


	// Return this simulated host's current set of IP addresses.
	inline QList<QHostAddress> hostAddresses() const {
		return links.keys(); }

	// Return the currently attached link at a particular address,
	// NULL if no link attached at that address.
	inline SimLink *linkAt(const QHostAddress &addr) {
		return links.value(addr); }

	// Find a neighbor host on some adjacent link
	// by the host's IP address on that link.
	// Returns the host pointer, or NULL if not found.
	// If found, also sets srcaddr to this host's address
	// on the network link on which the neighbor is found.
	SimHost *neighborAt(const QHostAddress &dstaddr, QHostAddress &srcaddr);

	// Attach to a network link at a given IP address.
	// Note that a host may be attached to the same link
	// at more than one address.
	void attach(const QHostAddress &addr, SimLink *link);

	// Detach from a network link at a given IP address.
	void detach(const QHostAddress &addr, SimLink *link);
};

class SimLink
{
	friend class SimHost;
	friend class SimPacket;

private:
	Simulator *sim;

	// Hosts attached to this link
	QHash<QHostAddress, SimHost*> hosts;

	// Network performance settings
	int netrate;		// Link bandwidth in bytes per second
	int netdelay;		// Link delay in microseconds one-way
	int netbufmul;		// Delay multiplier representing net buffering

public:

	enum LinkPreset {
		Eth100,		// 100Mbps Ethernet link
		Eth1000,	// 1000Mbps Ethernet link
	};

	SimLink(LinkPreset preset = Eth1000);

	inline int netRate() const { return netrate; }
	inline int netDelay() const { return netdelay; }
	inline int netBufferMultiplier() const { return netbufmul; }

	void setNetRate(int rate) { netrate = rate; }
	void setNetDelay(int delay) { netdelay = delay; }
	void setNetBufferMultiplier(int bufmul) { netbufmul = bufmul; }
};

class Simulator : public QObject
{
	friend class SimTimerEngine;
	friend class SimPacket;
	friend class SimSocket;
	friend class SimHost;
	Q_OBJECT

private:
	// True if we're using realtime instead of virtualized time
	const bool realtime;

	// The current virtual system time
	Time cur;

	// List of all currently active timers sorted by wake time
	QQueue<SimTimerEngine*> timers;

	// Table of all hosts in the simulation
	//QHash<QHostAddress, SimHost*> hosts;

public:
	Simulator(bool realtime = false);
	virtual ~Simulator();

	Time currentTime();

	void run();
};

} // namespace SST

#endif	// SIM_H
