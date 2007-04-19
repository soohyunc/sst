
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


static const int bucketSize = 2;

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

int visittag;

bool Node::gotAnnounce(int aff, Path fwpath, Path revpath)
{
	Q_ASSERT(fwpath.originId() == id());

	// Got a self-announcement from some other node.

	// Add ourselves to the reverse path if not done already.
	NodeId previd = revpath.originId();
	if (previd != id()) {
		Q_ASSERT(neighbors.contains(previd));
		revpath.prepend(id(), 0/*XXX*/, neighbors[previd].dist);
	}

	// Forward the announcement as specified by the forward-path.
	if (!fwpath.isEmpty()) {
		NodeId nextid = fwpath.afterHopId(0);
		Q_ASSERT(neighbors.contains(nextid));
		fwpath.removeFirst();
		return nodes[nextid]->gotAnnounce(aff, fwpath, revpath);
	}

	// The announcement has reached its immediate target.
	if (vtag == visittag)
		return false;	// already visited
	vtag = visittag;
	Q_ASSERT(affinity(id(), revpath.targetId()) >= aff);

	// Insert the return path into our routing table if it's useful to us.
	bool progress = rtr.insertPath(revpath);
	if (progress) {

		// The announcement proved useful to us,
		// so it's apparently still "in range" at this affinity.
		// Rebroadcast it to all neighbors we know about
		// that have at least the same affinity to the source,
		// keeping the announcement's affinity level the same.
		for (int i = aff; i < rtr.buckets.size(); i++) {
			Bucket &b = rtr.buckets[i];
			for (int j = 0; j < b.paths.size(); j++) {
				Path p = b.paths[j];
				Q_ASSERT(p.originId() == id());
				Q_ASSERT(p.numHops() > 0);
				if (revpath.hopsBefore(p.targetId()) >= 0)
					continue;	// already visited
				progress |= gotAnnounce(aff, p, revpath);
			}
		}

	} else {
		// The announcement has exhausted its useful range
		// at its current origin-affinity level.
		// Rebroadcast the announcement with the next higher affinity,
		// expanding its range to more distant nodes.
		// All the appropriate nodes are in our bucket number 'aff',
		// unless our affinity to the source is already higher...
		int minaff = aff, maxaff = aff;
		if (affinity(id(), revpath.targetId()) > aff) {
			minaff = aff+1;
			maxaff = affinity(id(), revpath.targetId());
		}
		maxaff = qMin(maxaff, rtr.buckets.size());
		for (int i = minaff; i <= maxaff; i++) {
			Bucket &b = rtr.buckets[i];
			for (int j = 0; j < b.paths.size(); j++) {
				Path p = b.paths[j];
				Q_ASSERT(p.originId() == id());
				Q_ASSERT(p.numHops() > 0);
				Q_ASSERT(rtr.affinityWith(p.targetId()) == i);
				if (revpath.hopsBefore(p.targetId()) >= 0)
					continue;	// already visited
				progress |= gotAnnounce(aff+1, p, revpath);
			}
		}
	}

	return progress;
}

bool Node::sendAnnounce()
{
	bool progress = false;
	visittag++;
	qDebug() << "visittag" << visittag;

	// Send an announcement to our immediate neighbors;
	// they'll forward it on to more distant nodes as appropriate...
	foreach (const NodeId &nbid, neighbors.keys()) {
		if (nbid == id())
			continue;	// don't generate paths to myself
		Neighbor &nb = neighbors[nbid];
		Path fwpath(id(), 0, nbid, nb.dist);
		Path revpath(id());
		progress |= gotAnnounce(0, fwpath, revpath);
	}

	return progress;
}


////////// main //////////

int main(int argc, char **argv)
{
	QApplication app(argc, argv);

	Simulator sim;

	quint32 addr = QHostAddress("1.1.1.1").toIPv4Address();
	for (int i = 0; i < 150; i++) {

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
		qDebug() << "announce, round" << ++round;
		progress = false;
		foreach (Node *n, nodes)
			progress |= n->sendAnnounce();
	} while (progress);

	// Create a visualization window
	ViewWindow *vwin = new ViewWindow();
	vwin->show();

	// Run the simulation
	app.exec();
}

