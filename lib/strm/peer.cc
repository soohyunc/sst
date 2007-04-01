
// XXX bug in g++ 4.1.2?  This must be declared before including QHash!?
#include <QtGlobal>
namespace SST { class Endpoint; }
uint qHash(const SST::Endpoint &ep);

#include <QtDebug>

#include "host.h"
#include "stream.h"
#include "strm/base.h"
#include "strm/peer.h"
#include "strm/sflow.h"

using namespace SST;


////////// StreamPeer //////////

StreamPeer::StreamPeer(Host *h, const QByteArray &id)
:	h(h), id(id), flow(NULL), recontimer(h)
{
	Q_ASSERT(!id.isEmpty());

	// If the EID is just an encapsulated IP endpoint,
	// then also use it as a destination address hint.
	Ident ident(id);
	if (ident.scheme() == ident.IP) {
		Endpoint ep(ident.ipAddress(), ident.ipPort());
		if (ep.port == 0)
			ep.port = NETSTERIA_DEFAULT_PORT;
		addrs.insert(ep);
	}
}

void StreamPeer::connectFlow()
{
	Q_ASSERT(!id.isEmpty());
	Q_ASSERT(!flow);

	//qDebug() << "Lookup target" << id.toBase64();

	// Send a lookup request to each known registration server.
	foreach (RegClient *rc, h->regClients()) {
		if (!rc->registered())
			continue;	// Can't poll an inactive regserver
		if (lookups.contains(rc))
			continue;	// Already polling this regserver

		// Make sure we're hooked up to this client's signals
		conncli(rc);

		// Start the lookup, with hole punching
		lookups.insert(rc);
		rc->lookup(id, true);
	}

	// Initiate key exchange attempts to any already-known endpoints
	// using each of the network sockets we have available.
	foreach (Socket *sock, h->activeSockets())
		foreach (const Endpoint &ep, addrs)
			initiate(sock, ep);

	// Keep firing off connection attempts periodically
	recontimer.start((qint64)connectRetry * 1000000);

	// Check waiting flows already in case no RegClients are active.
	Q_ASSERT(!flow);
	checkWaiting();
}

void StreamPeer::conncli(RegClient *rc)
{
	if (connrcs.contains(rc))
		return;
	connrcs.insert(rc);

	// Listen for the lookup response
	connect(rc, SIGNAL(lookupDone(const QByteArray &,
			const Endpoint &, const RegInfo &)),
		this, SLOT(lookupDone(const QByteArray &,
			const Endpoint &, const RegInfo &)));

	// Also make sure we hear if this regclient disappears
	connect(rc, SIGNAL(destroyed(QObject*)),
		this, SLOT(regClientDestroyed(QObject*)));
}

void StreamPeer::lookupDone(const QByteArray &id, const Endpoint &loc,
				const RegInfo &info)
{
	// Mark this outstanding lookup as completed.
	RegClient *rc = (RegClient*)sender();
	if (!lookups.contains(rc)) {
		//qDebug() << "StreamPeer: unexpected lookupDone signal";
		return;
	}
	lookups.remove(rc);

	// If the lookup failed, notify waiting streams as appropriate.
	if (loc.isNull()) {
		qDebug() << this << "Lookup on" << id.toBase64() << "failed";
		if (!flow)
			checkWaiting();
		return;
	}

	qDebug() << "StreamResponder::lookupDone: primary" << loc.toString()
		<< "secondaries" << info.endpoints().size();

	// Add the endpoint information we've received to our address list,
	// and initiate flow setup attempts to those endpoints.
	StreamPeer *peer = h->streamPeer(id);
	peer->foundEndpoint(loc);
	foreach (const Endpoint &ep, info.endpoints())
		peer->foundEndpoint(ep);
}

void StreamPeer::regClientDestroyed(QObject *obj)
{
	qDebug() << "StreamPeer: RegClient destroyed before lookupDone";

	RegClient *rc = (RegClient*)obj;
	lookups.remove(rc);
	connrcs.remove(rc);

	// If there are waiting streams and no more RegClients,
	// notify them next time we get back to the main loop.
	if (lookups.isEmpty() && initors.isEmpty() && !waiting.isEmpty())
		recontimer.start(0);
}

void StreamPeer::foundEndpoint(const Endpoint &ep)
{
	Q_ASSERT(!id.isEmpty());

	if (addrs.contains(ep))
		return;	// We know; sit down...

	qDebug() << "Found endpoint" << ep.toString()
		<< "for target" << id.toBase64();

	// Add this endpoint to our set
	addrs.insert(ep);

	// Attempt a connection to this endpoint
	foreach (Socket *sock, h->activeSockets())
		initiate(sock, ep);
}

void StreamPeer::initiate(Socket *sock, const Endpoint &ep)
{
	// No need to initiate new flows if we already have one...
	if (flow) {
		Q_ASSERT(flow->isActive());
		return;
	}

	// Don't simultaneously initiate multiple flows to the same endpoint.
	// XXX should be keyed on sock too
	if (initors.contains(ep)) {
		//qDebug() << "Already initiated connection attempt to"
		//	<< ep.toString();
		return;
	}

	// Make sure our StreamResponder exists
	// to receive and dispatch incoming key exchange control packets.
	(void)h->streamResponder();

	// Create and bind a new flow
	Flow *fl = new StreamFlow(h, this, id);
	if (!fl->bind(sock, ep)) {
		qDebug() << "StreamProtocol: could not bind new flow to target"
			<< ep.toString();
		delete fl;
		return checkWaiting();
	}

	// Start the key exchange process for the flow.
	// The KeyInitiator will re-parent the new flow under itself
	// for the duration of the key exchange.
	KeyInitiator *ini = new KeyInitiator(fl, magic, id);
	connect(ini, SIGNAL(completed(bool)), this, SLOT(completed(bool)));
	initors.insert(ep, ini);
	Q_ASSERT(fl->parent() == ini);
}

void StreamPeer::completed(bool success)
{
	if (flow)
		return;

	KeyInitiator *ki = (KeyInitiator*)sender();
	Q_ASSERT(ki && ki->isDone());

	// Remove and schedule the key initiator for deletion,
	// in case it wasn't removed already by setPrimary()
	// (e.g., if key agreement failed).
	// If the new flow hasn't been reparented by setPrimary(),
	// then it will be deleted automatically as well
	// because it is still a child of the KeyInitiator.
	Endpoint ep = ki->remoteEndpoint();
	Q_ASSERT(!initors.contains(ep) || initors.value(ep) == ki);
	initors.remove(ep);
	ki->deleteLater();
	ki = NULL;

	// If unsuccessful, notify waiting streams.
	if (!success) {
		qDebug() << "Connection attempt for ID" << id.toBase64()
			<< "to" << ep.toString() << "failed";
		return checkWaiting();
	}

	// We should have an active primary flow at this point,
	// since StreamFlow::start() attaches the flow if there isn't one.
	Q_ASSERT(flow != NULL);
}

void StreamPeer::setPrimary(StreamFlow *fl)
{
	Q_ASSERT(flow == NULL);
	Q_ASSERT(fl->isActive());
	Q_ASSERT(fl->target() == this);

	qDebug() << "Set primary flow for" << id.toBase64()
		<< "to" << fl->remoteEndpoint().toString();

	// Use this flow as our primary flow for this target.
	flow = fl;

	// Re-parent it directly underneath us,
	// so it won't be deleted when its KeyInitiator disappears.
	fl->setParent(this);

	// Delete all outstanding KeyInitiators now that we have a primary flow
	foreach (KeyInitiator *ki, initors.values()) {
		//qDebug() << "Deleting KeyInitiator for"
		//	<< id.toBase64()
		//	<< "to" << fl->remoteEndpoint().toString();
		ki->deleteLater();
	}
	initors.clear();

	// Connect all waiting streams to the new primary flow
	foreach (BaseStream *bs, waiting) {
		Q_ASSERT(bs->state == BaseStream::WaitFlow);
		bs->connectToFlow(flow);
	}
	Q_ASSERT(waiting.isEmpty());
}

void StreamPeer::checkWaiting()
{
	Q_ASSERT(!flow);
	if (!lookups.isEmpty() || !initors.isEmpty())
		return;		// Still stuff going on

#if 0
	foreach (BaseStream *strm, waiting) {
		Q_ASSERT(strm->state == BaseStream::WaitFlow);

		// Don't kill persistent streams.
		if (strm->persist)
			continue;

		// Notify this stream of connection failure.
		strm->fail(tr("Cannot establish connection to host ID %0")
				.arg(QString(id.toBase64())));
		Q_ASSERT(!waiting.contains(strm));
	}
#endif
}

void StreamPeer::retryTimeout()
{
	// If we actually have an active flow now, do nothing.
	if (flow)
		return;

	// Re-check and notify non-persistent streams of failure
	checkWaiting();

#if 0
	// Update our persist flag depending on still-waiting streams.
	persist = false;
	foreach (BaseStream *strm, waiting)
		if (strm->persist)
			persist = true;

	// If still persistent, fire off a new batch of connection attempts.
	if (persist)
#endif
		connectFlow();
}

