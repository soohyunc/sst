#ifndef MIGRATE_H
#define MIGRATE_H

#include "stream.h"
#include "sim.h"

class QIODevice;


namespace SST {

class Host;


class MigrateTest : public QObject
{
	Q_OBJECT

private:
	Simulator sim;
	SimLink link;
	SimHost clihost;
	SimHost srvhost;
	Stream cli;
	StreamServer srv;
	Stream *srvs;
	QHostAddress curaddr;

	Time starttime;
	quint64 timeperiod;
	Timer migrater;

	int narrived;
	int p2;
	int nmigrates;

public:
	MigrateTest();

	static void run();

private:
	void ping(Stream *strm);

private slots:
	void gotConnection();
	void gotMessage();
	void gotTimeout();
};


} // namespace SST

#endif	// MIGRATE_H
