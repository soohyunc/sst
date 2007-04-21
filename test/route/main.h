#ifndef MAIN_H
#define MAIN_H

#include <QObject>

#include "host.h"
#include "sim.h"
#include "route.h"

namespace SST {


// Total size of the simulated world
static const int worldWidth = 100;
static const int worldHeight = 100;


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

	// Queue of neighbors whose neighbor tables we need to scan
	QList<NodeId> scanq;


	Node(Simulator *sim, const QByteArray &id, const QHostAddress &addr);

	// Calculate our Euclidean distance from a given other node
	double distanceTo(Node *n);

	void updatePos();
	void updateNeighbors();

	inline QByteArray id() { return rtr.id; }

	/// Reverse a path.
	/// Usable for testing purposes only and not in the actual router,
	/// because it doesn't handle routing ids properly.
	static Path reversePath(const Path &p);

	/// Compute the shortest possible path from node a to node b.
	static Path shortestPath(const NodeId &a, const NodeId &b);

	/// Directly "force-fill" this router's neighbor tables
	/// based on current physical and virtual neighbors.
	/// Returns true if it found and inserted any new paths.
	//bool forceFill();

	bool gotAnnounce(QSet<NodeId> &visited, int aff, const Path &revpath);
	bool sendAnnounce();

	bool optimizePath(const Path &oldpath);
	bool optimize();
};

} // namespace SST

extern QHash<QByteArray, SST::Node*> nodes;

#endif	// MAIN_H
