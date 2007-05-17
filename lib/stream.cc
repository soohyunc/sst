
#include <QtDebug>

#include "regcli.h"
#include "ident.h"
#include "host.h"
#include "xdr.h"
#include "stream.h"
#include "strm/peer.h"
#include "strm/base.h"
#include "strm/sflow.h"

using namespace SST;


////////// Stream //////////

Stream::Stream(Host *h, QObject *parent)
:	QIODevice(parent),
	host(h),
	as(NULL)
{
}

Stream::Stream(AbstractStream *as, QObject *parent)
:	QIODevice(parent),
	host(as->h),
	as(as),
	statconn(false)
{
	Q_ASSERT(as->strm == NULL);
	as->strm = this;

	// Since the underlying BaseStream is already connected,
	// immediately allow the client read/write access.
	setOpenMode(ReadWrite | Unbuffered);
}

Stream::~Stream()
{
	//qDebug() << "~" << this;

	disconnect();
	Q_ASSERT(as == NULL);
}

bool Stream::connectTo(const QByteArray &dstid,
			const QString &service, const QString &protocol,
			const Endpoint &dstep)
{
	disconnect();
	Q_ASSERT(!as);

	// Create a top-level application stream object for this connection.
	typedef BaseStream ConnectStream;	// XXX
	ConnectStream *cs = new ConnectStream(host, dstid, NULL);
	cs->strm = this;
	as = cs;

	// Get our link status signal hooked up, if it needs to be.
	connectLinkStatusChanged();

	// Start the actual network connection process
	cs->connectTo(service, protocol, dstep);

	// We allow the client to start "sending" data immediately
	// even before the stream has fully connected.
	setOpenMode(ReadWrite | Unbuffered);

	return true;
}

bool Stream::connectTo(const Ident &dstid,
		const QString &service, const QString &protocol,
		const Endpoint &dstep)
{
	return connectTo(dstid.id(), service, protocol, dstep);
}

void Stream::disconnect()
{
	if (!as)
		return;		// Already disconnected

	// Disconnect our link status signal.
	StreamPeer *peer = host->streamPeer(as->peerid, false);
	if (peer)
		QObject::disconnect(peer, SIGNAL(linkStatusChanged(LinkStatus)),
				this, SIGNAL(linkStatusChanged(LinkStatus)));

	// Clear the back-link from the BaseStream.
	Q_ASSERT(as->strm == this);
	as->strm = NULL;

	// Start the graceful close process on the internal state.
	// With the back-link gone, the BaseStream self-destructs when done.
	as->shutdown(Close);

	// We're now officially closed.
	as = NULL;
	setOpenMode(NotOpen);
}

bool Stream::isConnected()
{
	return as != NULL;
}

void Stream::foundPeerEndpoint(const Endpoint &ep)
{
	if (!as) return;
	host->streamPeer(as->peerid)->foundEndpoint(ep);
}

QByteArray Stream::localHostId()
{
	if (!as) return QByteArray();
	return as->localHostId();
}

QByteArray Stream::remoteHostId()
{
	if (!as) return QByteArray();
	return as->remoteHostId();
}

bool Stream::isLinkUp()
{
	if (!as) return false;
	return as->isLinkUp();
}

void Stream::setPriority(int pri)
{
	if (!as) return;
	as->setPriority(pri);
}

int Stream::priority()
{
	if (!as) return setError(tr("Stream not connected")), 0;
	return as->priority();
}

qint64 Stream::bytesAvailable() const
{
	if (!as) return 0;
	return as->bytesAvailable();
}

qint64 Stream::readData(char *data, qint64 maxSize)
{
	if (!as) return setError(tr("Stream not connected")), -1;
	return as->readData(data, maxSize);
}

int Stream::pendingMessages() const
{
	if (!as) return 0;
	return as->pendingMessages();
}

qint64 Stream::pendingMessageSize() const
{
	if (!as) return 0;
	return as->pendingMessageSize();
}

qint64 Stream::readMessage(char *data, int maxSize)
{
	if (!as) return setError(tr("Stream not connected")), -1;
	return as->readMessage(data, maxSize);
}

QByteArray Stream::readMessage(int maxSize)
{
	if (!as) return setError(tr("Stream not connected")), QByteArray();
	return as->readMessage(maxSize);
}

bool Stream::atEnd() const
{
	if (!as) return true;
	return as->atEnd();
}

qint64 Stream::writeData(const char *data, qint64 size)
{
	if (!as) return setError(tr("Stream not connected")), -1;
	return as->writeData(data, size, StreamProtocol::dataPushFlag);
}

qint64 Stream::writeMessage(const char *data, qint64 size)
{
	if (!as) return setError(tr("Stream not connected")), -1;
	return as->writeData(data, size, StreamProtocol::dataMessageFlag);
}

int Stream::readDatagram(char *data, int maxSize)
{
	if (!as) { setError(tr("Stream not connected")); return -1; }
	return as->readDatagram(data, maxSize);
}

QByteArray Stream::readDatagram(int maxSize)
{
	if (!as) return setError(tr("Stream not connected")), QByteArray();
	return as->readDatagram(maxSize);
}

qint32 Stream::writeDatagram(const char *data, qint32 size)
{
	if (!as) return setError(tr("Stream not connected")), -1;
	return as->writeDatagram(data, size);
}

Stream *Stream::openSubstream()
{
	if (!as) { setError(tr("Stream not connected")); return NULL; }
	AbstractStream *newas = as->openSubstream();
	Q_ASSERT(newas);
	return new Stream(newas, this);
}

void Stream::listen()
{
	if (!as) return setError(tr("Stream not connected"));
	return as->listen();
}

bool Stream::isListening() const
{
	if (!as) return false;
	return as->isListening();
}

Stream *Stream::acceptSubstream()
{
	if (!as) { setError(tr("Stream not connected")); return NULL; }
	AbstractStream *newas = as->acceptSubstream();
	if (!newas) { setError(tr("No waiting substreams")); return NULL; }
	return new Stream(newas, this);
}

void Stream::connectNotify(const char *signal)
{
	connectLinkStatusChanged();

	QIODevice::connectNotify(signal);
}

void Stream::connectLinkStatusChanged()
{
	if (statconn || !as ||
			receivers(SIGNAL(linkStatusChanged(LinkStatus)) <= 0))
		return;

	StreamPeer *peer = host->streamPeer(as->peerid);
	connect(peer, SIGNAL(linkStatusChanged(LinkStatus)),
		this, SIGNAL(linkStatusChanged(LinkStatus)));
	statconn = true;
}

void Stream::shutdown(ShutdownMode mode)
{
	if (!as) return;
	as->shutdown(mode);

	if (mode & Reset)
		setOpenMode(NotOpen);
	if (mode & Read)
		setOpenMode(isWritable() ? WriteOnly : NotOpen);
	if (mode & Write)
		setOpenMode(isReadable() ? ReadOnly : NotOpen);
}

void Stream::setError(const QString &errorString)
{
	setErrorString(errorString);
	error(errorString);
}

#ifndef QT_NO_DEBUG
void Stream::dump()
{
	as->dump();
}
#endif




////////// StreamResponder //////////

StreamResponder::StreamResponder(Host *h)
:	KeyResponder(h, StreamProtocol::magic)
{
	// Get us connected to all currently extant RegClients
	foreach (RegClient *rc, h->regClients())
		conncli(rc);

	// Watch for newly created RegClients
	RegHostState *rhs = h;
	connect(rhs, SIGNAL(regClientCreate(RegClient*)),
		this, SLOT(clientCreate(RegClient*)));
}

void StreamResponder::conncli(RegClient *rc)
{
	//qDebug() << "StreamResponder: RegClient" << rc->serverName();
	if (connrcs.contains(rc))
		return;

	connrcs.insert(rc);
	connect(rc, SIGNAL(stateChanged()), this, SLOT(clientStateChanged()));
	connect(rc, SIGNAL(lookupNotify(const QByteArray &,
			const Endpoint &, const RegInfo &)),
		this, SLOT(lookupNotify(const QByteArray &,
			const Endpoint &, const RegInfo &)));
}

void StreamResponder::clientCreate(RegClient *rc)
{
	qDebug() << "StreamResponder::clientCreate" << rc->serverName();
	conncli(rc);
}

Flow *StreamResponder::newFlow(const SocketEndpoint &epi, const QByteArray &idi,
				const QByteArray &, QByteArray &)
{
	StreamPeer *peer = host()->streamPeer(idi);
	Q_ASSERT(peer->id == idi);

	StreamFlow *flow = new StreamFlow(host(), peer, idi);
	if (!flow->bind(epi)) {
		qDebug("StreamResponder: could not bind new flow");
		delete flow;
		return NULL;
	}

	return flow;
}

void StreamResponder::clientStateChanged()
{
	//qDebug() << "StreamResponder::clientStateChanged";

	// A RegClient changed state, potentially connected.
	// (XX make the signal more specific.)
	// Retry all outstanding lookups in case they might succeed now.
	foreach (StreamPeer *peer, host()->peers)
		peer->connectFlow();
}

void StreamResponder::lookupNotify(const QByteArray &,const Endpoint &loc,
				 const RegInfo &)
{
	qDebug() << "StreamResponder::lookupNotify";

	// Someone at endpoint 'loc' is apparently trying to reach us -
	// send them an R0 hole punching packet to his public endpoint.
	// XX perhaps make sure we might want to talk with them first?
	sendR0(loc);
}


////////// StreamServer //////////

StreamServer::StreamServer(Host *h, QObject *parent)
:	QObject(parent),
	h(h),
	active(false)
{
}

bool StreamServer::listen(
	const QString &serviceName, const QString &serviceDesc,
	const QString &protocolName, const QString &protocolDesc)
{
	qDebug() << "StreamServer: registering service" << serviceName
		<< "protocol" << protocolName;

	Q_ASSERT(!serviceName.isEmpty());
	Q_ASSERT(!serviceDesc.isEmpty());
	Q_ASSERT(!protocolName.isEmpty());
	Q_ASSERT(!protocolDesc.isEmpty());
	Q_ASSERT(!isListening());

	// Make sure the StreamResponder is initialized and listening.
	(void)h->streamResponder();

	// Register us to handle the indicated service name
	ServicePair svpair(serviceName, protocolName);
	if (h->listeners.contains(svpair)) {
		err = tr("Service '%0' with protocol '%1' already registered")
			.arg(serviceName).arg(protocolName);
		qDebug() << "StreamServer::listen: listener collision on"
				<< serviceName << protocolName;
		return false;
	}

	this->svname = serviceName;
	this->svdesc = serviceDesc;
	this->prname = protocolName;
	this->prdesc = protocolDesc;
	h->listeners.insert(svpair, this);
	this->active = true;
	return true;
}

Stream *StreamServer::accept()
{
	if (rconns.isEmpty())
		return NULL;
	BaseStream *newbs = rconns.dequeue();
	return new Stream(newbs, this);
}


////////// StreamHostState //////////

StreamHostState::~StreamHostState()
{
	if (rpndr) {
		delete rpndr;
		rpndr = NULL;
	}

	// Delete all the StreamPeers we created
	foreach (StreamPeer *peer, peers)
		delete peer;
	peers.clear();
}

StreamResponder *StreamHostState::streamResponder()
{
	if (!rpndr)
		rpndr = new StreamResponder(host());
	return rpndr;
}

StreamPeer *StreamHostState::streamPeer(const QByteArray &id, bool create)
{
	StreamPeer *&peer = peers[id];
	if (!peer && create)
		peer = new StreamPeer(host(), id);
	return peer;
}

