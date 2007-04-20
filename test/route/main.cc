
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


QHash<QByteArray, Node*> nodes;


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

bool Node::gotAnnounce(QSet<NodeId> &visited, int aff, const Path &revpath)
{
	// Got a self-announcement from some other node.
	Q_ASSERT(revpath.originId() == id());

	// Nodes ignore duplicate copies of announcements they receive.
	if (visited.contains(id()))
		return false;	// already visited
	visited.insert(id());
	Q_ASSERT(affinity(id(), revpath.targetId()) >= aff);

	// Reverse path shouldn't have loops in it...
	if (revpath.looping()) qDebug() << revpath;
	Q_ASSERT(!revpath.looping());

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
				NodeId tid = p.targetId();
				Q_ASSERT(p.originId() == id());
				Q_ASSERT(p.numHops() > 0);
				if (visited.contains(tid))
					continue;	// already visited
				if (revpath.hopsBefore(tid) >= 0)
					continue;	// already in path
				Path newrev = reversePath(p) + revpath;
				progress |= nodes[tid]->gotAnnounce(
						visited, aff, newrev);
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
				NodeId tid = p.targetId();
				Q_ASSERT(p.originId() == id());
				Q_ASSERT(p.numHops() > 0);
				Q_ASSERT(rtr.affinityWith(tid) == i);
				if (visited.contains(tid))
					continue;	// already visited
				if (revpath.hopsBefore(tid) >= 0)
					continue;	// already visited
				Path newrev = reversePath(p) + revpath;
				progress |= nodes[tid]->gotAnnounce(
						visited, aff+1, newrev);
			}
		}
	}

	return progress;
}

bool Node::sendAnnounce()
{
	bool progress = false;

	// Send an announcement to our immediate neighbors;
	// they'll forward it on to more distant nodes as appropriate...
	QSet<NodeId> visited;
	foreach (const NodeId &nbid, neighbors.keys()) {
		if (nbid == id())
			continue;	// don't generate paths to myself
		Neighbor &nb = neighbors[nbid];
		Path revpath(nbid, 0, id(), nb.dist);
		progress |= nodes[nbid]->gotAnnounce(visited, 0, revpath);
	}
	//qDebug() << this << "announced to" << visited.size() << "nodes";

	return progress;
}

#if 0
bool Node::scan()
{
	if (scanq.isEmpty())
		return false;
	NodeId scanid = scanq.takeFirst();

	// Find a path to the neighbor to scan
	Path p(id());
	if (scanid != id())
		p = pathTo(scanid);
	if (p.isNull()) {
		qDebug() << this << "supposed to scan" << scanid.toBase64()
			<< "but no longer in neighbor table";
		return true;	// we at least removed it from scanq
	}

	return true;
}
#endif

Path Node::reversePath(const Path &p)
{
	//qDebug() << "before:" << p;

	Path np(p.targetId());
	for (int i = p.numHops()-1; i >= 0; i--)
		np.append(0/*XXX*/, p.beforeHopId(i), 0);

	np.weight = p.weight;
	np.stamp = p.stamp;

	//qDebug() << "after:" << np;

	Q_ASSERT(np.numHops() == p.numHops());
	Q_ASSERT(np.originId() == p.targetId());
	Q_ASSERT(np.targetId() == p.originId());

	return np;
}

bool Node::optimizePath(const Path &oldpath)
{
	const NodeId &targid = oldpath.targetId();
	Q_ASSERT(targid != id());

	Node *hn = this;
	Node *tn = nodes[targid];
	Path head(id());
	Path tail(targid);

	// Move hn and tn toward each other, building the head and tail paths,
	// until we reach some common midpoint node.
	while (hn != tn) {
		int aff = affinity(hn->id(), tn->id());

		Path hnp = hn->rtr.nearestNeighborPath(tn->id());
		NodeId hnn = hnp.targetId();
		Q_ASSERT(!hnn.isEmpty() && affinity(hnn, tn->id()) > aff);

		Path tnp = tn->rtr.nearestNeighborPath(hn->id());
		NodeId tnn = tnp.targetId();
		Q_ASSERT(!tnn.isEmpty() && affinity(tnn, hn->id()) > aff);

		if (hnp.weight <= tnp.weight) {
			// Head hop is lighter - extend the head path.
			hn = nodes[hnn];
			head += hnp;
		} else {
			// Tail hop is lighter - prepend to the tail path.
			tn = nodes[tnn];
			tail = (reversePath(tnp) + tail);
		}
	}
	Q_ASSERT(head.originId() == id());
	Q_ASSERT(head.targetId() == tail.originId());
	Q_ASSERT(tail.targetId() == targid);

	Path newpath = head + tail;

	if (newpath.weight >= oldpath.weight)
		return false;	// no improvement

	qDebug() << "optimized path: old weight" << oldpath.weight
			<< "hops" << oldpath.numHops()
			<< "new weight" << newpath.weight
			<< "hops" << newpath.numHops();

	rtr.insertPath(newpath);
	return true;
}

bool Node::optimize()
{
	bool progress = false;
	for (int i = 0; i < rtr.buckets.size(); i++) {
		Bucket &b = rtr.buckets[i];
		for (int j = 0; j < b.paths.size(); j++) {
			Path &p = b.paths[j];
			progress |= optimizePath(p);
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
	foreach (Node *n, nodes)
		n->scanq.append(n->id());
	bool progress;
	int round = 0;
	do {
		qDebug() << "announce, round" << ++round;
		progress = false;
		foreach (Node *n, nodes)
			progress |= n->sendAnnounce();
	} while (progress);

	// Progressively optimize nodes' neighbor tables
	round = 0;
	do {
		qDebug() << "optimize, round" << ++round;
		progress = false;
		foreach (Node *n, nodes)
			progress |= n->optimize();
	} while (progress);

	// Create a visualization window
	ViewWindow *vwin = new ViewWindow();
	vwin->show();

	// Run the simulation
	app.exec();
}

