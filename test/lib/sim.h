#ifndef SIM_H
#define SIM_H

#include <QHash>
#include <QQueue>
#include <QHostAddress>

#include "host.h"

namespace SST {

class SimHost;
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
	Q_OBJECT

private:
	Simulator *sim;
	SimHost *srch, *dsth;
	Endpoint src, dst;
	QByteArray buf;
	Timer timer;

public:
	SimPacket(SimHost *srch, const Endpoint &src, const Endpoint &dst,
			const char *data, int size);

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
	QHostAddress addr;

	// Sockets bound on this host
	QHash<quint16, SimSocket*> socks;

	// Minimum network arrival time for next packet to be received
	qint64 arrival;

public:
	SimHost(Simulator *sim, QHostAddress addr);
	~SimHost();

	virtual Time currentTime();
	virtual TimerEngine *newTimerEngine(Timer *timer);

	virtual Socket *newSocket(QObject *parent = NULL);
};

class Simulator : public QObject
{
	friend class SimTimerEngine;
	friend class SimPacket;
	friend class SimSocket;
	friend class SimHost;
	Q_OBJECT

private:
	// The current virtual system time
	Time cur;

	// List of all currently active timers sorted by wake time
	QQueue<SimTimerEngine*> timers;

	// Table of all hosts in the simulation
	QHash<QHostAddress, SimHost*> hosts;

public:
	Simulator();
	virtual ~Simulator();

	void run();
};

} // namespace SST

#endif	// SIM_H
