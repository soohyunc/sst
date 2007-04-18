
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


int affinity(const NodeId &a, const NodeId &b)
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
	// Search for any newly-reachable neighbors.
	// XXX this algorithm may get too slow as number of nodes grows.
	foreach (Node *n, nodes) {
		QByteArray nid = n->id();
		if (distanceTo(n) > maxRange)
			continue;	// completely out of range
		if (neighbors.contains(nid))
			continue;	// already a neighbor

		// Insert placeholder loss rate - will get recalculated below
		neighbors.insert(nid, 0);
	}

	// Adjust the loss rate for each of our neighbors.
	foreach (QByteArray id, neighbors.keys()) {
		double dist = distanceTo(nodes.value(id));
		if (dist > maxRange) {
			// Neighbor has gone out of range - delete it.
			neighbors.remove(id);
		} if (dist >= minRange) {
			// Partially in range - compute loss rate.
			double loss = (dist - minRange) / (maxRange - minRange);
			neighbors.insert(id, loss);
		} else {
			// Fully in range - assume no loss.
			neighbors.insert(id, 0.0);
		}
	}
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

	// Create a visualization window
	ViewWindow *vwin = new ViewWindow();
	vwin->show();

	// Run the simulation
	app.exec();
}

