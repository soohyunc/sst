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
