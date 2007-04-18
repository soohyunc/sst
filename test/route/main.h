#ifndef MAIN_H
#define MAIN_H

#include <QObject>

#include "host.h"
#include "sim.h"

namespace SST {


static const quint32 routerMagic = 0x00525452;	// 'RTR'

// Total size of the simulated world
static const int worldWidth = 100;
static const int worldHeight = 100;

// Minimum and maximum radio range
static const int minRange = 10;
static const int maxRange = 20;


typedef QByteArray NodeId;


// A particular path to some node, with the last known state of that path
struct PathEntry
{
	quint32 rid;		// Local routing identifier
	NodeId nid;		// Full node identifier
};

struct Path
{
	QList<PathEntry> ents;	// List of routing hops comprising  path
	double dist;		// Measured weight/distance/latency of path

	NodeId targetId() const { return ents.last().nid; }
};

// A node's information about some other node it keeps tabs on
//class Peer
//{
//private:
//	int refcount;
//
//public:
//	Peer();
//	inline void take()
//		{ refcount++; Q_ASSERT(refcount > 0); }
//	inline void drop()
//		{ Q_ASSERT(refcount > 0); if (--refcount == 0) delete this; }
//};

class Bucket
{
public:
	QList<Path> paths;
};

class Router : public SocketReceiver
{
	Q_OBJECT

public:
	Router(Host *h, const QByteArray &id, QObject *parent = NULL);

	const QByteArray id;		// pseudorandom node label

	QList<Bucket> buckets;


	virtual void receive(QByteArray &msg, XdrStream &ds,
				const SocketEndpoint &src);
};

class Node
{
public:
	SimHost host;
	Router rtr;

	double x, y;		// current world position
	double dx, dy;		// current direction and velocity
	Time postime;		// time position was last updated

	// Loss rate for each id-indexed node that's currently reachable at all
	QHash<QByteArray, float> neighbors;


	Node(Simulator *sim, const QByteArray &id, const QHostAddress &addr);

	// Calculate our Euclidean distance from a given other node
	double distanceTo(Node *n);

	void updatePos();
	void updateNeighbors();

	inline QByteArray id() { return rtr.id; }
};

} // namespace SST

extern QHash<QByteArray, SST::Node*> nodes;

// Compute the affinity between two NodeIds - i.e., the first different bit.
int affinity(const SST::NodeId &a, const SST::NodeId &b);

#endif	// MAIN_H
