
#include <math.h>
#include <stdlib.h>

#include <QList>
#include <QHash>
#include <QApplication>
#include <QtDebug>

#include "sock.h"
#include "main.h"
#include "view.h"

using namespace SST;


static const int bucketSize = 5;

QHash<QByteArray, Node*> nodes;


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


////////// PathInfo //////////

int PathInfo::hopsBefore(const NodeId &nid)
{
	if (start == nid)
		return 0;
	for (int i = 0; i < path.size(); i++) {
		if (path[i].nid == nid)
			return i+1;
	}
	return -1;
}

PathInfo &PathInfo::operator+=(const PathInfo &tail)
{
	// The first path's target must be the second path's origin.
	Q_ASSERT(targetId() == tail.originId());

	// Append the paths, avoiding creating unnecessary routing loops.
	NodeId id = start;
	for (int i = tail.path.size(); ; i--) {
		Q_ASSERT(i >= 0);
		NodeId tnid = tail.beforeHopId(i);

		int nhops = hopsBefore(tnid);
		if (nhops < 0)
			continue;
		Q_ASSERT(beforeHopId(nhops) == tnid);

		path = path.mid(0, nhops) + tail.path.mid(i);
		break;
	}

	// Pessimistically update our weight and timestamp
	weight += tail.weight;
	stamp = qMin(stamp, tail.stamp);

	return *this;
}


////////// Bucket //////////

bool Bucket::insert(const PathInfo &npi)
{
	// Check for an existing PathInfo entry for this path
	for (int i = 0; i < pis.size(); i++) {
		PathInfo &pi = pis[i];
		if (pi.path != npi.path)
			continue;

		// Duplicate found - only keep the latest one.
		if (npi.stamp <= pi.stamp)
			return false;	// New PathInfo is older: discard

		// Replace old PathInfo with new one
		pis.removeAt(i);
		break;
	}

	// Insert the new PathInfo according to distance
	int i;
	for (i = 0; i < pis.size() && pis[i].weight <= npi.weight; i++)
		;
	if (i >= bucketSize)
		return false;	// Off the end of the bucket: discard
	pis.insert(i, npi);

	// Truncate the PathInfo list to our maximum bucket size
	while (pis.size() > bucketSize)
		pis.removeLast();

	return true;	// New PathInfo is accepted!
}


////////// Router //////////

Router::Router(Host *h, const QByteArray &id, QObject *parent)
:	SocketReceiver(h, routerMagic, parent),
	id(id)
{
}

//bool Router::insertPath(const PathInfo &p)
//{
//}

void Router::receive(QByteArray &msg, XdrStream &ds,
			const SocketEndpoint &src)
{
}


////////// Node //////////

Node::Node(Simulator *sim, const QByteArray &id, const QHostAddress &addr)
:	host(sim, addr),
	rtr(&host, id)
{
	x = drand48() * worldWidth;
	y = drand48() * worldWidth;
	dx = drand48() - 0.5;
	dy = drand48() - 0.5;
	postime = host.currentTime();

	//qDebug() << this << "id" << rtr.id.toBase64() << "@" << x << y;
}

double Node::distanceTo(Node *n)
{
	double distx = n->x - x;
	double disty = n->y - y;
	return sqrt(distx * distx + disty * disty);
}

void Node::updateNeighbors()
{
	// Start with our list of current neighbors.
	QList<NodeId> nbids = neighbors.keys();

	// Search for any newly-reachable neighbors.
	// XXX this algorithm may get too slow as number of nodes grows.
	foreach (Node *n, nodes) {
		QByteArray nid = n->id();
		if (distanceTo(n) > maxRange)
			continue;	// completely out of range
		if (neighbors.contains(nid))
			continue;	// already a neighbor

		// Add this node's ID - neighbor will be inserted below
		nbids.append(nid);
	}

	// Adjust the loss rate for each of our neighbors.
	foreach (NodeId id, nbids) {
		Neighbor &nb = neighbors[id];	// create or update
		nb.dist = distanceTo(nodes.value(id));
		if (nb.dist > maxRange) {
			// Neighbor has gone out of range - delete it.
			neighbors.remove(id);
			continue;
		} else if (nb.dist >= minRange) {
			// Partially in range - compute loss rate.
			nb.loss = (nb.dist - minRange) / (maxRange - minRange);
		} else {
			// Fully in range - assume no loss.
			nb.loss = 0.0;
		}
	}
}

bool Node::forceFill()
{
	bool progress = false;

	// First generate paths to one-hop neighbors
	foreach (const NodeId &nbid, neighbors.keys()) {
		if (nbid == id())
			continue;	// don't generate paths to myself
		Neighbor &nb = neighbors[nbid];
		PathInfo p(id(), 0, nbid, nb.dist);
		progress |= rtr.insertPath(p);
	}

	// Now search through our existing neighbors' neighbor tables
	// to find paths to other potentially higher-affinity neighbors
	for (int i = 0; i < rtr.buckets.size(); i++) {
		Bucket &b = rtr.buckets[i];
		for (int j = 0; j < b.pis.size(); j++) {
			PathInfo &pi = b.pis[j];

			// Take this neighbor as an intermediate neighbor
			// from which to find other more distant
			// but higher-affinity neighbors.
			NodeId nid = pi.targetId();
			Node *n = nodes[nid];
			Q_ASSERT(nid != id());

			// Any other nodes with higher affinity to us than nid
			// will be in bucket i in node n's neighbor table.
			if (n->rtr.buckets.size() <= i)
				continue;
			Bucket &nb = n->rtr.buckets[i];
			for (int nj = 0; nj < nb.pis.size(); nj++) {
				PathInfo &npi = nb.pis[nj];
				if (npi.targetId() == id())
					continue;
				progress |= rtr.insertPath(pi + npi);
			}
		}
	}

	return progress;
}


////////// main //////////

int main(int argc, char **argv)
{
	QApplication app(argc, argv);

	Simulator sim;

	quint32 addr = QHostAddress("1.1.1.1").toIPv4Address();
	for (int i = 0; i < 100; i++) {

		// Pick a pseudorandom label for this node (32 bits for now)
		QByteArray id;
		do {
			long lab = mrand48();
			id = QByteArray((char*)&lab, sizeof(lab));
		} while (nodes.contains(id));

		// Create a node with that id
		Node *n = new Node(&sim, id, QHostAddress(addr++));
		nodes.insert(id, n);
	}

	// Update all nodes' neighbor reachability information
	foreach (Node *n, nodes)
		n->updateNeighbors();

	// Fill in nodes' neighbor buckets
	bool progress;
	int round = 0;
	do {
		qDebug() << "forceFill, round" << ++round;
		progress = false;
		foreach (Node *n, nodes)
			progress |= n->forceFill();
	} while (progress);

	// Create a visualization window
	ViewWindow *vwin = new ViewWindow();
	vwin->show();

	// Run the simulation
	app.exec();
}

