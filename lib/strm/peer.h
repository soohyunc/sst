#ifndef SST_STRM_PEER_H
#define SST_STRM_PEER_H

#include <QSet>
#include <QHash>
#include <QByteArray>
#include <QPointer>

#include "timer.h"
#include "strm/proto.h"

namespace SST {

class Host;
class Endpoint;
class KeyInitiator;
class RegInfo;
class RegClient;
class BaseStream;
class StreamFlow;
class StreamResponder;
class StreamHostState;


// Private helper class:
// contains information about target host ID we're trying to reach,
// potentially at a number of alternative network addresses.
class StreamPeer : public QObject, public StreamProtocol
{
	friend class BaseStream;
	friend class StreamFlow;
	friend class StreamResponder;
	friend class StreamHostState;
	Q_OBJECT

	// Retry connection attempts for persistent streams once every minute.
	static const int connectRetry = 1*60; 

	Host *const h;			// Our per-host state
	const QByteArray id;		// Host ID of target
	StreamFlow *flow;		// Current primary flow
	QSet<RegClient*> lookups;	// Outstanding lookups in progress
	QSet<BaseStream*> waiting;	// Streams waiting for a flow
	Timer recontimer;		// For persistent lookup requests

	// Set of RegClients we've connected to so far
	QPointerSet<RegClient> connrcs;

	// Flows under construction, by target endpoint
	QSet<Endpoint> addrs;		// Potential locations known
	QHash<Endpoint,KeyInitiator*> initors;


	StreamPeer(Host *h, const QByteArray &id);

	inline QByteArray remoteHostId() { return id; }

	// Initiate a connection attempt to target host by any means possible,
	// hopefully at some point resulting in an active primary flow.
	void connectFlow();

	// Connect to a given RegClient's signals
	void conncli(RegClient *cli);

	// Initiate a key exchange attempt to a given endpoint,
	// if such an attempt isn't already in progress.
	void initiate(Socket *sock, const Endpoint &ep);

	void foundEndpoint(const Endpoint &ep);

	void setPrimary(StreamFlow *flow);

	// Check for waiting streams that should be notified of failure.
	void checkWaiting();

private slots:
	void completed(bool success);
	void lookupDone(const QByteArray &id, const Endpoint &loc,
			const RegInfo &info);
	void regClientDestroyed(QObject *obj);
	void retryTimeout();
};

} // namespace SST

#endif	// SST_STRM_PEER_H
