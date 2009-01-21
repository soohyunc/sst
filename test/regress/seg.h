#ifndef SEG_H
#define SEG_H

#include "stream.h"
#include "sim.h"
#include "../lib/seg.h"	// XXX

class QIODevice;


namespace SST {

class Host;


class SegTest : public QObject
{
	Q_OBJECT

private:
	Simulator sim;
	SimHost h1, h2, h3, h4;		// four hosts in a linear topology
	SimLink l12, l23, l34;		// links joining the hosts

	// Flow layer objects (XXX hack hack)
	FlowSocket fs1, fs4;
	FlowResponder fr1, fr2, fr3, fr4;

	Stream cli;
	StreamServer srv;
	Stream *srvs;

public:
	SegTest();

	static void run();

private:
	void ping(Stream *strm);

private slots:
	void gotConnection();
	void gotMessage();
};


} // namespace SST

#endif	// SEG_H
