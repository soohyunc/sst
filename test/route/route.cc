
//#include <math.h>
//#include <stdlib.h>

#include <QtDebug>

#include "route.h"

using namespace SST;


static const int bucketSize = 2;


int SST::affinity(const NodeId &a, const NodeId &b)
{
	int sz = qMin(a.size(), b.size());
	for (int i = 0; i < sz; i++) {
		char x = a.at(i) ^ b.at(i);
		if (x == 0)
			continue;

		// Byte difference detected - find first bit difference
		for (int j = 0; ; j++) {
			if (x & (0x80 >> j))
				return i*8 + j;
			Q_ASSERT(j < 8);
		}
	}
	return sz * 8;	// NodeIds are identical
}


////////// Path //////////

int Path::hopsBefore(const NodeId &nid)
{
	if (start == nid)
		return 0;
	for (int i = 0; i < hops.size(); i++) {
		if (hops[i].nid == nid)
			return i+1;
	}
	return -1;
}

Path &Path::operator+=(const Path &tail)
{
	// The first path's target must be the second path's origin.
	Q_ASSERT(targetId() == tail.originId());

	// Append the paths, avoiding creating unnecessary routing loops.
	NodeId id = start;
	for (int i = tail.hops.size(); ; i--) {
		Q_ASSERT(i >= 0);
		NodeId tnid = tail.beforeHopId(i);

		int nhops = hopsBefore(tnid);
		if (nhops < 0)
			continue;
		Q_ASSERT(beforeHopId(nhops) == tnid);

		hops = hops.mid(0, nhops) + tail.hops.mid(i);
		break;
	}

	// Pessimistically update our weight and timestamp
	weight += tail.weight;
	stamp = qMin(stamp, tail.stamp);

	return *this;
}


////////// Bucket //////////

bool Bucket::insert(const Path &npi)
{
	// Check for an existing Path entry for this path
	for (int i = 0; i < paths.size(); i++) {
		Path &pi = paths[i];
		if (pi.hops != npi.hops)
			continue;

		// Duplicate found - only keep the latest one.
		if (npi.stamp <= pi.stamp)
			return false;	// New Path is older: discard

		// Replace old Path with new one
		paths.removeAt(i);
		break;
	}

	// Insert the new Path according to distance
	int i;
	for (i = 0; i < paths.size() && paths[i].weight <= npi.weight; i++)
		;
	if (i >= bucketSize)
		return false;	// Off the end of the bucket: discard
	paths.insert(i, npi);

	// Truncate the Path list to our maximum bucket size
	while (paths.size() > bucketSize)
		paths.removeLast();

	return true;	// New Path is accepted!
}


////////// Router //////////

Router::Router(Host *h, const QByteArray &id, QObject *parent)
:	SocketReceiver(h, routerMagic, parent),
	id(id)
{
}

void Router::receive(QByteArray &msg, XdrStream &ds,
			const SocketEndpoint &src)
{
}


