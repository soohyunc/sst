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

// Minimum and maximum radio range
static const int minRange = 10;
static const int maxRange = 20;


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

	// Visit tag, for use in cheesy traversal algorithms...
	int vtag;


	Node(Simulator *sim, const QByteArray &id, const QHostAddress &addr);

	// Calculate our Euclidean distance from a given other node
	double distanceTo(Node *n);

	void updatePos();
	void updateNeighbors();

	inline QByteArray id() { return rtr.id; }

	/// Directly "force-fill" this router's neighbor tables
	/// based on current physical and virtual neighbors.
	/// Returns true if it found and inserted any new paths.
	//bool forceFill();

	bool gotAnnounce(int aff, Path fwpath, Path revpath);
	bool sendAnnounce();
};

} // namespace SST

extern QHash<QByteArray, SST::Node*> nodes;

#endif	// MAIN_H
