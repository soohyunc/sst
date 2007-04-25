
#include <math.h>
#include <stdlib.h>

#include <QList>
#include <QHash>
#include <QMultiMap>
#include <QApplication>
#include <QtDebug>

#include "sock.h"
#include "main.h"
#include "view.h"
#include "stats.h"

using namespace SST;


#if 1	// Small network
static const int numNodes = 100;	// Number of nodes to simulate
static const int minRange = 10;		// Minimum and maximum radio range
static const int maxRange = 20;
#elif 0	// Slightly larger network
static const int numNodes = 300;	// Number of nodes to simulate
static const int minRange = 6;		// Minimum and maximum radio range
static const int maxRange = 12;
#elif 0	// Larger, fairly dense network
static const int numNodes = 1000;	// Number of nodes to simulate
static const int minRange = 5;		// Minimum and maximum radio range
static const int maxRange = 7;
#endif


Simulator sim;

QList<Node*> nodelist;
QHash<QByteArray, Node*> nodes;


static inline Node *randomNode()
{
	return nodelist[lrand48() % nodelist.size()];
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

bool Node::gotAnnounce(QSet<NodeId> &visited, int range,
			int aff, const Path &revpath)
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
	if (range-- == 0)
		return progress;	// Don't recurse beyond range
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
						visited, range,
						aff, newrev);
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
						visited, range,
						aff+1, newrev);
			}
		}
	}

	return progress;
}

bool Node::sendAnnounce(int range)
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
		progress |= nodes[nbid]->gotAnnounce(visited, range,
							0, revpath);
	}
	//qDebug() << this << "announced to" << visited.size() << "nodes";

	return progress;
}

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

struct ShortestPathInfo {
	double weight;
	Node *prev;

	inline ShortestPathInfo() { weight = HUGE_VAL; prev = NULL; }
};

Path Node::shortestPath(const NodeId &a, const NodeId &b)
{
	Node *na = nodes[a];
	Node *nb = nodes[b];

	// Maintain a priority queue of possible paths
	QHash<Node*,ShortestPathInfo> spi;
	QMultiMap<double,Node*> nq;	// Priority queue of unvisited nodes

	spi[na].weight = 0;
	nq.insert(0, na);

	while (true) {
		// If we run out of nodes, there is no path!
		if (nq.isEmpty()) {
			qDebug() << "shortestPath: no path from"
				<< a.toBase64() << "to" << b.toBase64();
			return Path();
		}

		// Grab the closest unvisited node from the priority queue
		QMultiMap<double,Node*>::iterator i(nq.constBegin());
		Node *n = i.value();
		ShortestPathInfo &nspi = spi[n];
		double nweight = i.key();
		nq.erase(i);

		// Once we hit a path that leads to the desired target,
		// we know it must be the shortest.
		if (n == nb)
			break;

		// If the weight at this priority queue entry
		// is larger than our best known weight for that node,
		// it means we've already found a better path to that node:
		// this is just a stale leftover nq entry, so skip.
		// (We leave stale entries becase it's a pain to remove them.)
		if (nweight != nspi.weight) {
			Q_ASSERT(nweight > nspi.weight);
			continue;
		}

		// Add new paths leading from this node
		foreach (const NodeId &nbid, n->neighbors.keys()) {
			Neighbor &nb = n->neighbors[nbid];
			Node *nn = nodes[nbid];
			ShortestPathInfo &nnspi = spi[nn];

			double nnweight = nweight + nb.dist;
			if (nnweight >= nnspi.weight)
				continue;

			// Adjust ShortestPathInfo for new path,
			// and add a new priority queue entry for it.
			nnspi.weight = nnweight;
			nnspi.prev = n;
			nq.insert(nnweight, nn);
		}
	}

	// Build a path from b to a, then reverse it
	Path p(b);
	Node *n = nb;
	while (n != na) {
		n = spi.value(n).prev;
		p.append(0/*XXX*/, n->id(), 0);
	}
	p.weight = spi.value(nb).weight;
	return reversePath(p);
}

Path Node::squeezePath(const NodeId &origid, const NodeId &targid,
			int prerecurse, bool postrecurse)
{
	Node *hn = nodes[origid];
	Node *tn = nodes[targid];
	Path head(origid);
	Path tail(targid);

	// If prerecurse allows, explore several alternate paths.
	if (hn != tn && prerecurse > 0) {
		int aff = affinity(hn->id(), tn->id());

#if 1
		// Pick the closest neighbor to the target from the head.
		Path hnp = hn->rtr.nearestNeighborPath(tn->id());
		NodeId hnn = hnp.targetId();
		if (hnn.isEmpty())
			return Path();	// no path found
		Q_ASSERT(affinity(hnn, tn->id()) > aff);

		// Pick the closest neighbor from the head to the target.
		Path tnp = tn->rtr.nearestNeighborPath(hn->id());
		NodeId tnn = tnp.targetId();
		if (tnn.isEmpty())
			return Path();	// no path found
		Q_ASSERT(affinity(tnn, hn->id()) > aff);

		// Recursively optimize based on either choice.
		Path hp = hnp + squeezePath(hnn, targid,
						prerecurse-1, postrecurse);
		Path tp = squeezePath(origid, tnn, prerecurse-1, postrecurse)
				+ reversePath(tnp);

#if 1
		// Pick the best.
		if (tp.isNull() || hp.weight <= tp.weight)
			return hp;
		else
			return tp;
#else
		// Pick the best.
		if (tp.isNull() || hp.weight <= tp.weight) {
			return hnp + squeezePath(hnn, targid,
						prerecurse, postrecurse);
		} else {
			return squeezePath(origid, tnn,
						prerecurse, postrecurse)
					+ reversePath(tnp);
		}
#endif
#else
		// Pick the best 'tries' paths inward from either endpoint
		static const int tries = 5;
		QList<Path> paths;
		hn->rtr.nearestNeighborPaths(tn->id(), paths, tries);
		tn->rtr.nearestNeighborPaths(hn->id(), paths, tries);
		if (paths.isEmpty())
			return Path();	// no path found

		// Recursively optimize based on each choice, picking the best.
		Path best;
		for (int i = 0; i < paths.size(); i++) {
			Path &np = paths[i];
			//qDebug() << "Squeeze" << i << np;
			Path p;
			if (np.originId() == origid)
				p = np + squeezePath(np.targetId(), targid,
						prerecurse-1, postrecurse);
			else
				p = squeezePath(origid, np.targetId(),
						prerecurse-1, postrecurse)
					+ reversePath(np);
			if (best.isNull() || best.weight > p.weight)
				best = p;
		}
		//qDebug() << "Best" << best;
		return best;
#endif
	}

	// Move hn and tn toward each other, building the head and tail paths,
	// until we reach some common midpoint node.
	while (hn != tn) {
		int aff = affinity(hn->id(), tn->id());

		Path hnp = hn->rtr.nearestNeighborPath(tn->id());
		NodeId hnn = hnp.targetId();
		if (hnn.isEmpty())
			return Path();	// no path found
		Q_ASSERT(affinity(hnn, tn->id()) > aff);

		Path tnp = tn->rtr.nearestNeighborPath(hn->id());
		NodeId tnn = tnp.targetId();
		if (tnn.isEmpty())
			return Path();	// no path found
		Q_ASSERT(affinity(tnn, hn->id()) > aff);

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

	if (postrecurse && !head.isEmpty() && !tail.isEmpty()) {
		// Recursively squeeze the two sub-paths we ended up with

		Path nhead = squeezePath(head.originId(), head.targetId(),
					prerecurse, postrecurse);
		if (!nhead.isNull() && nhead.weight < head.weight)
			head = nhead;

		Path ntail = squeezePath(tail.originId(), tail.targetId(),
					prerecurse, postrecurse);
		if (!ntail.isNull() && ntail.weight < tail.weight)
			tail = ntail;
	}

	Q_ASSERT(head.originId() == origid);
	Q_ASSERT(head.targetId() == tail.originId());
	Q_ASSERT(tail.targetId() == targid);

	return head + tail;
}

bool Node::optimizePath(const Path &oldpath)
{
	const NodeId &targid = oldpath.targetId();
	Q_ASSERT(targid != id());

	Path newpath = squeezePath(id(), targid);
	//Path newpath = shortestPath(oldpath.originId(), oldpath.targetId());

	if (newpath.isNull()) {
		qWarning("optimizePath() failed!  Disconnected graph?");
		return false;	// no path found!
	}

	Q_ASSERT(newpath.originId() == id());
	Q_ASSERT(newpath.targetId() == targid);

	if (newpath.weight >= oldpath.weight)
		return false;	// no improvement

//	qDebug() << "optimized path: old weight" << oldpath.weight
//			<< "hops" << oldpath.numHops()
//			<< "new weight" << newpath.weight
//			<< "hops" << newpath.numHops();

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

// Pick a random node ID that isn't already in use by another node.
// (Always creates 32-bit node IDs for now.)
static QByteArray randomNodeId()
{
	QByteArray id;
	do {
		long lab = mrand48();
		id = QByteArray((char*)&lab, sizeof(lab));
	} while (nodes.contains(id));
	return id;
}

// Create a random 2D mesh - a standard "easy" graph type,
// and one that would work well with geographic forwarding.
void setupRandomMesh()
{
	quint32 addr = QHostAddress("1.1.1.1").toIPv4Address();
	for (int i = 0; i < numNodes; i++) {
		QByteArray id = randomNodeId();
		Node *n = new Node(&sim, id, QHostAddress(addr++));
		nodes.insert(id, n);
		nodelist.append(n);
	}

	// Update all nodes' neighbor reachability information
	foreach (Node *n, nodes)
		n->updateNeighbors();
}

// Create a basic high-dimensional random graph...
// Each newly-added node connects to at least one random existing neighbor,
// and chooses more neighbors with exponentially decreasing probability.
void setupRandomGraph()
{
	PopStats degreestats;
	quint32 addr = QHostAddress("1.1.1.1").toIPv4Address();
	for (int i = 0; i < numNodes; i++) {
		QByteArray id = randomNodeId();
		Node *n = new Node(&sim, id, QHostAddress(addr++));
		nodes.insert(id, n);
		nodelist.append(n);

		do {
			Node *nn = randomNode();
			if (nn == n || n->neighbors.contains(nn->id()))
				continue;	// already have this neighbor

			// Insert bidirectional neighbor link
			Node::Neighbor newn = { 1.0, 0.0 };
			n->neighbors.insert(nn->id(), newn);
			nn->neighbors.insert(n->id(), newn);

		} while ((lrand48() & 3) != 0);
		degreestats.insert(n->neighbors.size());
	}
	qDebug() << "graph degree:" << degreestats;
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);

	//setupRandomMesh();
	setupRandomGraph();

#if 1
	// Fill in nodes' neighbor buckets
	foreach (Node *n, nodes)
		n->scanq.append(n->id());
	bool progress;
	int round = 0;
	do {
		qDebug() << "announce, round" << ++round;
		progress = false;
		foreach (Node *n, nodes)
			progress |= n->sendAnnounce(round*2);
	} while (progress);

	// Progressively optimize nodes' neighbor tables
	round = 0;
	do {
		qDebug() << "optimize, round" << ++round;
		progress = false;
		foreach (Node *n, nodes)
			progress |= n->optimize();
	} while (progress);

	SampleStats stretchstats;
	for (int i = 0; i < 1000; i++) {
		// Pick two random nodes and route between them
		Node *a = randomNode();
		Node *b;
		do {
			b = randomNode();
		} while (b == a);

		Path best = Node::shortestPath(a->id(), b->id());
		if (best.isNull()) {
			qDebug() << "No path from" << a->id().toBase64()
				<< "to" << b->id().toBase64();
			continue;
		}

		Path test = Node::squeezePath(a->id(), b->id());
		if (test.isNull()) {
			qDebug() << "Failed to find path"
				<< "from" << a->id().toBase64()
				<< "to" << b->id().toBase64();
			continue;
		}

		double stretch = test.weight / best.weight;

	//	qDebug() << "Path" << a->id().toBase64()
	//		<< "->" << b->id().toBase64()
	//		<< "found" << test.weight << test.numHops()
	//		<< "best" << best.weight << best.numHops()
	//		<< "stretch" << stretch;

		stretchstats.insert(stretch);
	}
	qDebug() << "stretch:" << stretchstats;

	//exit(0);
#endif

	// Create a visualization window
	ViewWindow *vwin = new ViewWindow();
	vwin->show();

	// Run the simulation
	app.exec();
}

