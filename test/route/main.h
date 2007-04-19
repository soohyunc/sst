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


// Compute the affinity between two NodeIds - i.e., the first different bit.
int affinity(const SST::NodeId &a, const SST::NodeId &b);


// One hop on a path
struct Hop
{
	quint32 rid;		// Local routing identifier
	NodeId nid;		// Full node identifier

	inline Hop() : rid(0) { }
	inline Hop(quint32 rid, NodeId nid)
		: rid(rid), nid(nid) { }

	inline bool operator==(const Hop &other) const
		{ return rid == other.rid && nid == other.nid; }
	inline bool operator!=(const Hop &other) const
		{ return rid != other.rid || nid != other.nid; }
};

// A full routing path
typedef QList<Hop> Path;

// A particular path to some node, with the last known state of that path
struct PathInfo
{
	NodeId start;		// Node at which path begins
	Path path;		// List of routing hops comprising  path
	double weight;		// Measured weight/distance/latency of path
	Time stamp;		// Time path was discovered or last checked

	inline PathInfo(const NodeId &start) : start(start), weight(0) { }
	inline PathInfo(const NodeId &start, quint32 rid, NodeId nid,
			double weight)
		: start(start), weight(weight) { path.append(Hop(rid, nid)); }

	inline NodeId originId() const { return start; }
	inline NodeId targetId() const { return path.last().nid; }

	// Return the node ID just before or just after a given hop
	inline NodeId beforeHopId(int hopno) const
		{ return hopno == 0 ? start : path.at(hopno-1).nid; }
	inline NodeId afterHopId(int hopno) const
		{ return path.at(hopno).nid; }

	// Return the number of hops to reach a given node,
	// 0 if the node is our starting point, -1 if it is not on the path.
	int hopsBefore(const NodeId &nid);

	// Extend this path by appending another PathInfo onto the tail.
	PathInfo &operator+=(const PathInfo &tail);

	inline PathInfo operator+(const PathInfo &tail) const
		{ PathInfo pi(*this); pi += tail; return pi; }
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
	QList<PathInfo> pis;

	/// Insert a newly-discovered PathInfo into this bucket.
	/// Returns true if the new PathInfo was actually accepted:
	/// i.e., if the bucket wasn't already full of "better" paths.
	bool insert(const PathInfo &path);
};

class Router : public SocketReceiver
{
	Q_OBJECT

public:
	Router(Host *h, const QByteArray &id, QObject *parent = NULL);

	const QByteArray id;		// pseudorandom node label

	QList<Bucket> buckets;


	inline int affinityWith(const NodeId &otherId) const
		{ return affinity(id, otherId); }

	inline Bucket &bucket(int aff)
		{ while (buckets.size() <= aff) buckets.append(Bucket());
		  return buckets[aff]; }
	inline Bucket &bucket(const NodeId &nid)
		{ return bucket(affinityWith(nid)); }

	inline bool insertPath(const PathInfo &p)
		{ return bucket(p.targetId()).insert(p); }

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

	struct Neighbor {
		double	dist;	// Geographical distance ("weight")
		double	loss;	// Loss rate - from 0.0 to 1.0
	};
	QHash<NodeId,Neighbor> neighbors;


	Node(Simulator *sim, const QByteArray &id, const QHostAddress &addr);

	// Calculate our Euclidean distance from a given other node
	double distanceTo(Node *n);

	void updatePos();
	void updateNeighbors();

	inline QByteArray id() { return rtr.id; }

	/// Directly "force-fill" this router's neighbor tables
	/// based on current physical and virtual neighbors.
	/// Returns true if it found and inserted any new paths.
	bool forceFill();
};

} // namespace SST

extern QHash<QByteArray, SST::Node*> nodes;

#endif	// MAIN_H
