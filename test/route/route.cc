
#include <math.h>
//#include <stdlib.h>

#include <QtDebug>

#include "route.h"

using namespace SST;


static const int bucketSize = 5;


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

int Path::hopsBefore(const NodeId &nid) const
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

	//Q_ASSERT(!looping());
	//Q_ASSERT(!tail.looping());

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

	Q_ASSERT(!looping());
	return *this;
}

bool Path::looping() const
{
	for (int i = 0; i < numHops(); i++)
		for (int j = i; j < numHops(); j++)
			if (beforeHopId(i) == afterHopId(j))
				return true;
	return false;
}

QDebug operator<<(QDebug debug, const Path &p)
{
	debug << "Path hops" << p.numHops() << "weight" << p.weight;
	debug.nospace() << " (" << p.start.toBase64();
	for (int i = 0; i < p.numHops(); i++)
		debug << " [" << p.hops[i].rid << "] -> "
			<< p.hops[i].nid.toBase64();
	debug << ")";
	return debug.space();
}


////////// Bucket //////////

bool Bucket::insert(const Path &npi)
{
#if 0	// (allowing multiple paths to the same node in one bucket)
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
#else	// (allowing only one path per target node)
	// Check for an existing Path entry for this target
	NodeId ntid = npi.targetId();
	for (int i = 0; i < paths.size(); i++) {
		Path &pi = paths[i];
		if (pi.targetId() != ntid)
			continue;

		// Duplicate found - only keep the shortest path.
		if (npi.weight >= pi.weight)
			return false;	// New Path is longer: discard

		// Replace old Path with new one
		paths.removeAt(i);
		break;
	}
#endif

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

double Bucket::horizon() const
{
	if (paths.size() < bucketSize)
		return HUGE_VAL;
	return paths[bucketSize-1].weight;
}


////////// Router //////////

Router::Router(Host *h, const QByteArray &id, QObject *parent)
:	SocketReceiver(h, routerMagic, parent),
	id(id)
{
}

Path Router::pathTo(const NodeId &targid) const
{
	int bn = affinityWith(targid);
	if (bn >= buckets.size())
		return Path();	// No known path - no bucket even

	const Bucket &b = buckets[bn];
	for (int i = 0; i < b.paths.size(); i++) {
		const Path &p = b.paths[i];
		Q_ASSERT(p.originId() == id);
		if (p.targetId() == targid)
			return p;
	}

	return Path();	// No known path
}

Path Router::nearestNeighborPath(const NodeId &id) const
{
	int aff = affinityWith(id);
	if (aff >= buckets.size())
		return NodeId();
	const Bucket &b = buckets[aff];
	if (b.paths.isEmpty())
		return NodeId();
	return b.paths[0];
}

void Router::receive(QByteArray &msg, XdrStream &ds,
			const SocketEndpoint &src)
{
}


