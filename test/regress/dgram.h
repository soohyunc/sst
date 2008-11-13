#ifndef DGRAM_H
#define DGRAM_H

#include "stream.h"
#include "sim.h"

class QIODevice;


namespace SST {

class Host;


class DatagramTest : public QObject
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
	int narrived;

public:
	DatagramTest();

	static void run();

private slots:
	void gotLinkUp();
	void gotConnection();
	void gotDatagram();
};


} // namespace SST

#endif	// DGRAM_H
