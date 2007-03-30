
#include <QtDebug>

#include "xdr.h"
#include "host.h"
#include "strm/base.h"
#include "strm/dgram.h"
#include "strm/peer.h"
#include "strm/sflow.h"

using namespace SST;


////////// BaseStream //////////

BaseStream::BaseStream(Host *h)
:	AbstractStream(h),
	state(Disconnected),
	flow(NULL),
	parent(NULL),
	sid(0),
	mature(false),
	endread(false),
	endwrite(false),
	tsn(0),
	twin(0),
	ravail(0),
	rmsgavail(0),
	rwinbyte(16),		// XX
	rsn(0)
{
}

BaseStream::~BaseStream()
{
	//qDebug() << "~" << this << (parent == NULL ? "(root)" : "");

	disconnect();
	Q_ASSERT(!flow);

	// Reset any unaccepted incoming substreams too
	foreach (AbstractStream *sub, rsubs) {
		sub->shutdown(Stream::Reset);
		// should self-destruct automatically when done
	}
	rsubs.clear();
}

void BaseStream::connectTo(const QByteArray &dstid,
			const QString &service, const QString &protocol,
			const Endpoint &dstep)
{
	Q_ASSERT(!dstid.isEmpty());
	Q_ASSERT(!service.isEmpty());
	Q_ASSERT(state == Disconnected);
	Q_ASSERT(flow == NULL);

	// Find or create the Target struct for the destination ID
	peerid = dstid;
	StreamPeer *peer = h->streamPeer(dstid);

	// If we were given a location hint, record it for setting up flows.
	if (!dstep.isNull())
		peer->foundEndpoint(dstep);

	// Queue up a service connect message onto the new stream.
	// This will only go out once we actually attach to a flow,
	// but the client can immediately enqueue application data behind it.
	QByteArray msg;
	XdrStream ws(&msg, QIODevice::WriteOnly);
	ws << (qint32)ConnectRequest << service << protocol;
	writeMessage(msg.data(), msg.size());

	// If there's already an active flow, just use it.
	if (peer->flow) {
		Q_ASSERT(peer->flow->isActive());
		return connectToFlow(peer->flow);
	}

	// Get the location and flow setup process for this host ID underway.
	qDebug() << "connectTo: WaitFlow";
	state = WaitFlow;
	peer->waiting.insert(this);
	peer->connectFlow();
}

void BaseStream::connectToFlow(StreamFlow *flow)
{
	//qDebug() << "connectToFlow" << flow;
	Q_ASSERT(state == Disconnected || state == WaitFlow);
	Q_ASSERT(!peerid.isEmpty());
	Q_ASSERT(!this->flow);
	Q_ASSERT(!this->sid);
	Q_ASSERT(parent == NULL);

	if (state == WaitFlow) {
		StreamPeer *peer = h->peers.value(peerid);
		Q_ASSERT(peer && peer->waiting.contains(this));
		peer->waiting.remove(this);
	}

	// Allocate a StreamID for this stream.
	Q_ASSERT(flow->isActive());
	parent = &flow->root;
	if (!flow->attach(this))
		return fail(
			tr("No stream IDs available while connecting to %0")
				.arg(flow->remoteEndpoint().toString()));

	// Record that we're waiting for a response from the server.
	state = WaitService;

	// We should already have at least a service request
	// and possibly application data waiting to be sent.
	Q_ASSERT(!tqueue.isEmpty());
	Q_ASSERT(!flow->tstreams.contains(this));
	flow->enqueueStream(this);
	if (flow->mayTransmit())
		flow->readyTransmit();
}

void BaseStream::gotServiceReply()
{
	Q_ASSERT(state == WaitService);
	Q_ASSERT(flow);

	QByteArray msg(readMessage(maxServiceMsgSize));
	XdrStream rs(&msg, QIODevice::ReadOnly);
	qint32 code, err;
	rs >> code >> err;
	if (rs.status() != rs.Ok || code != (qint32)ConnectReply || err)
		return fail(tr("Service connect failed: %0 %1")
					.arg(code).arg(err));	// XX

	state = Connected;
	if (strm)
		strm->linkUp();
}

void BaseStream::gotServiceRequest()
{
	Q_ASSERT(state == Accepting);

	QByteArray msg(readMessage(maxServiceMsgSize));
	XdrStream rs(&msg, QIODevice::ReadOnly);
	qint32 code;
	ServicePair svpair;
	rs >> code >> svpair.first >> svpair.second;
	if (rs.status() != rs.Ok || code != (qint32)ConnectRequest)
		return fail("Bad service request");

	// Lookup the requested service
	StreamServer *svr = h->listeners.value(svpair);
	if (svr == NULL)
		// XXX send reply with error code/message
		return fail(
			tr("Request for service %0 with unknown protocol %1")
				.arg(svpair.first).arg(svpair.second));

	// Send a service reply to the client
	msg.clear();
	XdrStream ws(&msg, QIODevice::WriteOnly);
	ws << (qint32)ConnectReply << (qint32)0;
	writeMessage(msg.data(), msg.size());

	// Hand off the new stream to the chosen service
	state = Connected;
	svr->rconns.enqueue(this);
	svr->newConnection();
}

AbstractStream *BaseStream::openSubstream()
{
	Q_ASSERT(isLinkUp());
	Q_ASSERT(flow);

	BaseStream *nstrm = new BaseStream(h);
	nstrm->parent = this;
	nstrm->peerid = peerid;
	if (!flow->attach(nstrm, 0)) {
		qDebug("rxinit: could not attach new substream to flow");
		return NULL;
	}

	nstrm->state = Connected;
	return nstrm;
}

void BaseStream::setPriority(int newpri)
{
	//qDebug() << this << "set priority" << newpri;
	AbstractStream::setPriority(newpri);

	if (flow && !tqueue.isEmpty()) {
		Q_ASSERT(flow->isActive());
		int rc = flow->dequeueStream(this);
		Q_ASSERT(rc == 1);
		flow->enqueueStream(this);
	}
}

void BaseStream::txqueue(const Packet &pkt)
{
	// Add the packet to our stream-local transmit queue.
	// Keep it in order of transmit sequence number.
	bool wasempty = tqueue.isEmpty();
	int i = tqueue.size();
	while (i > 0 && tqueue[i-1].tsn > pkt.tsn)
		i--;
	tqueue.insert(i, pkt);

	// If we don't have a flow, just leave it queued until we do.
	if (state == Disconnected || state == WaitFlow)
		return;
	Q_ASSERT(flow && flow->isActive());

	// Add our stream to our flow's transmit queue
	if (wasempty)
		flow->enqueueStream(this);

	// Prod the flow to transmit immediately if possible
	if (flow->mayTransmit())
		flow->readyTransmit();
}

void BaseStream::acked(const Packet &)
{
	//qDebug() << "BaseStream::acked packet of size" << pkt.buf.size();
}

void BaseStream::missed(const Packet &pkt)
{
	qDebug() << "Stream::missed packet at" << pkt.tsn
		<< "size" << pkt.buf.size();

	if (!pkt.dgram) {
		//qDebug() << "Retransmit packet of size" << pkt.buf.size();
		txqueue(pkt);	// Retransmit reliable segments
	}
}

BaseStream *BaseStream::rxinit(StreamID nsid)
{
	if (!(nsid & sidOrigin)) {
		qDebug("rxInit: other side trying to create MY sid!");
		return NULL;		// XX send back reset
	}

	// See if the indicated substream already exists.
	BaseStream *nstrm = flow->idhash.value(nsid);
	if (nstrm) {
		if (nstrm->parent != this) {
			qDebug("rxInit: incorrect parent/child relationship");
			return NULL;	// XX Protocol error: close flow?
		}
		return nstrm;
	}

	// Need to create the child stream - make sure we're allowed to.
	if (!isListening()) {
		qDebug("rxInit: other side trying to create substream, "
			"we're not listening.");
		return NULL;
	}

	nstrm = new BaseStream(h);
	nstrm->parent = this;
	nstrm->peerid = peerid;
	if (!flow->attach(nstrm, nsid)) {
		qDebug("rxinit: could not attach incoming stream to flow");
		return NULL;
	}

	if (this == &flow->root)
		nstrm->state = Accepting;	// Service request expected
	else {
		nstrm->state = Connected;
		rsubs.enqueue(nstrm);
		connect(nstrm, SIGNAL(readyReadMessage()),
			this, SLOT(subReadMessage()));
		if (strm)
			strm->newSubstream();
	}
	return nstrm;
}

void BaseStream::rxSegment(RxSegment &rseg)
{
	if (endread) {
		// Ignore anything we receive past end of stream
		// (which we may have forced from our end via close()).
		qDebug() << "Ignoring segment received after end-of-stream";
		Q_ASSERT(rahead.isEmpty());
		Q_ASSERT(rsegs.isEmpty());
		return;
	}

	int segsize = rseg.segmentSize();
	//qDebug() << "Received segment at" << rseg.rsn << "size" << segsize;

	// See where this packet fits in
	int rsndiff = rseg.rsn - rsn;
	if (rsndiff <= 0) {

		// The segment is at or before our current receive position.
		// How much of its data, if any, is actually useful?
		// Note that we must process packets at our RSN with no data,
		// because they might have important flags.
		int actsize = segsize + rsndiff;
		if (actsize < 0 || (actsize == 0 && !rseg.hasFlags())) {
			// The packet is way out of date -
			// its end doesn't even come up to our current RSN.
			qDebug() << "Duplicate segment at RSN" << rseg.rsn
				<< "size" << segsize;
			return;
		}
		rseg.hdrlen -= rsndiff;	// Merge useless data into "headers"
		//qDebug() << "actsize" << actsize << "flags" << rseg.flags();

		// It gives us exactly the data we want next - quelle bonheur!
		bool wasempty = !hasBytesAvailable();
		bool wasnomsgs = !hasPendingMessages();
		bool closed = false;
		rsegs.enqueue(rseg);
		rsn += actsize;
		ravail += actsize;
		rmsgavail += actsize;
		if ((rseg.flags() & (dataMessageFlag | dataCloseFlag))
				&& (rmsgavail > 0)) {
			rmsgsize.enqueue(rmsgavail);
			rmsgavail = 0;
		}
		if (rseg.flags() & dataCloseFlag)
			closed = true;

		// Then pull anything we can from the reorder buffer
		for (; !rahead.isEmpty(); rahead.removeFirst()) {
			RxSegment &rseg = rahead.first();
			int segsize = rseg.segmentSize();

			int rsndiff = rseg.rsn - rsn;
			if (rsndiff > 0)
				break;	// There's still a gap

			//qDebug() << "Pull segment at" << rseg.rsn
			//	<< "of size" << segsize
			//	<< "from reorder buffer";

			int actsize = segsize + rsndiff;
			if (actsize < 0 || (actsize == 0 && !rseg.hasFlags()))
				continue;	// No useful data: drop
			rseg.hdrlen -= rsndiff;

			// Consume this segment too.
			rsegs.enqueue(rseg);
			rsn += actsize;
			ravail += actsize;
			rmsgavail += actsize;
			if ((rseg.flags() & (dataMessageFlag | dataCloseFlag))
					&& (rmsgavail > 0)) {
				rmsgsize.enqueue(rmsgavail);
				rmsgavail = 0;
			}
			if (rseg.flags() & dataCloseFlag)
				closed = true;
		}

		// If we're at the end of stream with no data to read,
		// go into the end-of-stream state immediately.
		// We must do this because readData() may never
		// see our queued zero-length segment if ravail == 0.
		if (closed && ravail == 0) {
			shutdown(Stream::Read);
			readyReadMessage();
			if (isLinkUp() && strm) {
				strm->readyRead();
				strm->readyReadMessage();
			}
			return;
		}

		// Notify the client if appropriate
		if (wasempty) {
			if (state == Connected && strm)
				strm->readyRead();
		}
		if (wasnomsgs && hasPendingMessages()) {
			if (state == Connected) {
				readyReadMessage();
				if (strm)
					strm->readyReadMessage();
			} else if (state == WaitService) {
				gotServiceReply();
			} else if (state == Accepting)
				gotServiceRequest();
		}

	} else if (rsndiff > 0) {

		// It's out of order beyond our current receive sequence -
		// stash it in a re-order buffer, sorted by rsn.
		//qDebug() << "Received out-of-order segment at" << rseg.rsn
		//	<< "size" << segsize;
		int lo = 0, hi = rahead.size();
		if (hi > 0 && (rahead[hi-1].rsn - rsn) < rsndiff) {
			// Common case: belongs at end of rahead list.
			rahead.append(rseg);
			return;
		}

		// Binary search for the correct position.
		while (lo < hi) {
			int mid = (hi + lo) / 2;
			if ((rahead[mid].rsn - rsn) < rsndiff)
				lo = mid+1;
			else
				hi = mid;
		}

		// Don't save duplicate segments
		// (unless the duplicate actually has more data or new flags).
		if (lo < rahead.size() && rahead[lo].rsn == rseg.rsn
				&& segsize <= rahead[lo].segmentSize()
				&& rseg.flags() == rahead[lo].flags()) {
			qDebug("rxseg duplicate out-of-order segment - RSN %d",
				rseg.rsn);
			return;
		}

		rahead.insert(lo, rseg);
	}
}

void BaseStream::rxInitPacket(const QByteArray &pkt)
{
	const InitHeader *hdr = (const InitHeader*)(pkt.data() + Flow::hdrlen);
	int size = pkt.size() - hdrlenInit;
	if (size < 0) {
		qDebug("rxInitPacket: runt packet");
		return;	// XX Protocol error: close flow?
	}

	// We're the parent stream.  Find or create the new child substream.
	StreamID nsid = ntohs(hdr->nsid) ^ sidOrigin;
	BaseStream *nstrm = rxinit(nsid);
	if (!nstrm)
		return;

	// Build the packet descriptor.
	RxSegment rseg;
	rseg.rsn = ntohs(hdr->tsn);	// Note: 16-bit TSN
	rseg.buf = pkt;
	rseg.hdrlen = hdrlenInit;
	nstrm->rxSegment(rseg);
}

void BaseStream::rxDataPacket(const QByteArray &pkt)
{
	const DataHeader *hdr = (const DataHeader*)(pkt.data() + Flow::hdrlen);
	int size = pkt.size() - hdrlenData;
	if (size < 0) {
		qDebug("rxDataPacket: runt packet");
		return;	// XX Protocol error: close flow?
	}

	// Build the packet descriptor.
	RxSegment rseg;
	rseg.rsn = ntohl(hdr->tsn);	// Note: 32-bit TSN
	rseg.buf = pkt;
	rseg.hdrlen = hdrlenData;
	rxSegment(rseg);
}

void BaseStream::rxDatagramPacket(const QByteArray &pkt)
{
	if (state != Connected)
		return;		// Only accept datagrams while connected

	const DatagramHeader *hdr = (const DatagramHeader*)
					(pkt.data() + Flow::hdrlen);
	int size = pkt.size() - hdrlenDatagram;
	if (size < 0) {
		qDebug("rxDatagramPacket: runt packet");
		return;	// XX Protocol error: close flow?
	}

	int flags = hdr->type;
	//qDebug() << "rxDatagramSegment" << segsize << "type" << type;

	if (!(flags & dgramBeginFlag) || !(flags & dgramEndFlag)) {
		qWarning("OOPS, don't yet know how to reassemble datagrams");
		return;
	}

	// Build a pseudo-Stream object encapsulating the datagram.
	DatagramStream *dg = new DatagramStream(h, pkt, hdrlenDatagram);
	rsubs.enqueue(dg);
	// Don't need to connect to the sub's readyReadMessage() signal
	// because we already know the sub is completely received...
	if (strm) {
		strm->newSubstream();
		strm->readyReadDatagram();
	}
}

void BaseStream::rxResetPacket(const QByteArray &)
{
	Q_ASSERT(0);	// XXX
}

int BaseStream::readData(char *data, int maxSize)
{
	int actSize = 0;
	while (maxSize > 0 && ravail > 0) {
		Q_ASSERT(!endread);
		Q_ASSERT(!rsegs.isEmpty());
		RxSegment rseg = rsegs.dequeue();

		int size = rseg.segmentSize();
		Q_ASSERT(size >= 0);

		// XXX BUG: this breaks if we try to read a partial segment!
		Q_ASSERT(maxSize >= size);

		// Copy the data (or just drop it if data == NULL).
		if (data != NULL) {
			memcpy(data, rseg.buf.data() + rseg.hdrlen, size);
			data += size;
		}
		actSize += size;
		maxSize -= size;

		// Adjust the receive stats
		ravail -= size;
		Q_ASSERT(ravail >= 0);
		if (hasPendingMessages()) {

			// We're reading data from a queued message.
			qint64 &headsize = rmsgsize.head();
			headsize -= size;
			Q_ASSERT(headsize >= 0);

			// Always stop at the next message boundary.
			if (headsize == 0) {
				rmsgsize.removeFirst();
				break;
			}
		} else {

			// No queued messages - just read raw data.
			rmsgavail -= size;
			Q_ASSERT(rmsgavail >= 0);
		}

		// If this segment has the end-marker set, that's it...
		if (rseg.flags() & dataCloseFlag)
			shutdown(Stream::Read);
	}
	return actSize;
}

int BaseStream::readMessage(char *data, int maxSize)
{
	if (!hasPendingMessages())
		return -1;	// No complete messages available
	// XXX don't deadlock if a way-too-large message comes in...

	// Read as much of the next queued message as we have room for
	int oldrmsgs = rmsgsize.size();
	int actsize = BaseStream::readData(data, maxSize);
	Q_ASSERT(actsize > 0);

	// If the message is longer than the supplied buffer, drop the rest.
	if (rmsgsize.size() == oldrmsgs) {
		int skipsize = BaseStream::readData(NULL, 1 << 30);
		Q_ASSERT(skipsize > 0);
	}
	Q_ASSERT(rmsgsize.size() == oldrmsgs - 1);

	return actsize;
}

QByteArray BaseStream::readMessage(int maxSize)
{
	int msgsize = pendingMessageSize(); 
	if (msgsize <= 0)
		return QByteArray();	// No complete messages available

	// Read the next message into a new QByteArray
	QByteArray buf;
	int bufsize = qMin(msgsize, maxSize);
	buf.resize(bufsize);
	int actsize = readMessage(buf.data(), bufsize);
	Q_ASSERT(actsize == bufsize);
	return buf;
}

// Called by StreamFlow::readyTransmit()
// to prepare a data segment's packet headers
// for transmission (or retrasmission) on a given flow.
// We need to do this afresh on each retransmission
// because for example a segment first sent as an Init packet
// might later need to get retransmitted as a regular Data packet.
void BaseStream::txPrepare(Packet &p, StreamFlow *)
{
	if (p.dgram) {
		return;	// Everything already filled in
	}

	if (mature) {

		// Build the DataHeader.
		Q_ASSERT(hdrlenData == Flow::hdrlen + sizeof(DataHeader));
		DataHeader *hdr = (DataHeader*)(p.buf.data() + Flow::hdrlen);
		hdr->sid = htons(sid);
		hdr->type = (DataPacket << typeShift) |
				(hdr->type & dataAllFlags);
			// (flags already set - preserve)
		hdr->win = rwinbyte;
		hdr->tsn = htonl(p.tsn);		// Note: 32-bit TSN

	} else {

		// Build the InitHeader.
		Q_ASSERT(hdrlenInit == Flow::hdrlen + sizeof(InitHeader));
		InitHeader *hdr = (InitHeader*)(p.buf.data() + Flow::hdrlen);
		hdr->sid = htons(parent->sid);
		hdr->type = (InitPacket << typeShift) |
				(hdr->type & dataAllFlags);
			// (flags already set - preserve)
		hdr->win = rwinbyte;
		hdr->nsid = htons(sid);
		hdr->tsn = htons(p.tsn);		// Note: 16-bit TSN
		Q_ASSERT(tsn <= 0xffff);
	}
}

int BaseStream::writeData(const char *data, int totsize, quint8 endflags)
{
	Q_ASSERT(!endwrite);
	qint64 actsize = 0;
	do {
		// Choose the size of this segment.
		int size = mtu;
		quint8 flags = 0;
		if (totsize <= size) {
			flags = dataPushFlag | endflags;
			size = totsize;
		}
		//qDebug() << "Transmit segment at" << tsn << "size" << size;

		// Build the appropriate packet header.
		Packet p;
		p.strm = this;
		p.tsn = tsn;
		p.buf.resize(hdrlenData + size);

		// Prepare the header
		DataHeader *hdr = (DataHeader*)(p.buf.data() + Flow::hdrlen);
		// hdr->sid - later
		hdr->type = flags;	// Major type filled in later
		// hdr->win - later
		// hdr->tsn - later
		p.hdrlen = hdrlenData;

		// Advance the TSN to account for this data.
		// We must "grow up" when we advance beyond 16 bits.
		tsn += size;
		if (tsn > 0xffff)
			mature = true;

		// Copy in the application payload
		char *payload = (char*)(hdr+1);
		memcpy(payload, data, size);

		// Queue up the packet
		p.dgram = false;
		txqueue(p);

		// On to the next segment...
		data += size;
		totsize -= size;
		actsize += size;
	} while (totsize > 0);

	if (endflags & dataCloseFlag)
		endwrite = true;

	return actsize;
}

qint32 BaseStream::writeDatagram(const char *data, qint32 totsize)
{
	if (totsize > mtu /* XXX maxStatelessDatagram */ )
	{
		// Datagram too large to send using the stateless optimization:
		// just send it as a regular substream.
		qDebug() << this << "sending large datagram, size" << totsize;
		AbstractStream *sub = openSubstream();
		if (sub == NULL)
			return -1;
		sub->writeData(data, totsize, dataCloseFlag);
		// sub will self-destruct when sent and acked.
		return totsize;
	}

	qint32 remain = totsize;
	quint8 flags = dgramBeginFlag;
	do {
		// Choose the size of this fragment.
		int size = mtu;
		if (remain <= size) {
			flags |= dgramEndFlag;
			size = remain;
		}

		// Build the appropriate packet header.
		Packet p;
		p.strm = this;
		char *payload;

		// Build the DatagramHeader.
		p.buf.resize(hdrlenDatagram + size);
		Q_ASSERT(hdrlenDatagram == Flow::hdrlen
					+ sizeof(DatagramHeader));
		DatagramHeader *hdr = (DatagramHeader*)
					(p.buf.data() + Flow::hdrlen);
		hdr->sid = htons(sid);
		hdr->type = (DatagramPacket << typeShift) | flags;
		hdr->win = rwinbyte;

		p.hdrlen = hdrlenDatagram;
		payload = (char*)(hdr+1);

		// Copy in the application payload
		memcpy(payload, data, size);

		// Queue up the packet
		// XXX ensure that all fregments get consecutive seqnos!
		p.dgram = true;
		txqueue(p);

		// On to the next fragment...
		data += size;
		remain -= size;
		flags &= ~dgramBeginFlag;

	} while (remain > 0);
	Q_ASSERT(flags & dgramEndFlag);
	return totsize;
}

AbstractStream *BaseStream::acceptSubstream()
{
	if (rsubs.isEmpty())
		return NULL;

	AbstractStream *sub = rsubs.dequeue();
	QObject::disconnect(sub, SIGNAL(readyReadMessage()),
				this, SLOT(subReadMessage()));
	return sub;
}

AbstractStream *BaseStream::getDatagram()
{
	// Scan through the list of queued substreams
	// for one with a complete record waiting to be read.
	for (int i = 0; i < rsubs.size(); i++) {
		AbstractStream *sub = rsubs[i];
		if (!sub->hasPendingMessages())
			continue;
		rsubs.removeAt(i);
		return sub;
	}

	setError(tr("No datagrams available for reading"));
	return NULL;
}

int BaseStream::readDatagram(char *data, int maxSize)
{
	AbstractStream *sub = getDatagram();
	if (!sub)
		return -1;

	int act = sub->readData(data, maxSize);
	sub->shutdown(Stream::Reset);	// sub will self-destruct
	return act;
}

QByteArray BaseStream::readDatagram(int maxSize)
{
	AbstractStream *sub = getDatagram();
	if (!sub)
		return QByteArray();

	QByteArray data = sub->readMessage(maxSize);
	sub->shutdown(Stream::Reset);	// sub will self-destruct
	return data;
}

void BaseStream::subReadMessage()
{
	// When one of our queued subs emits a readyReadMessage() signal,
	// we have to forward that via our readyReadDatagram() signal.
	if (strm)
		strm->readyReadDatagram();
}

void BaseStream::shutdown(Stream::ShutdownMode mode)
{
	// XXX self-destruct when done, if appropriate

	if (mode & Stream::Reset)
		return disconnect();	// No graceful close necessary

	if (isLinkUp() && !endread && (mode & Stream::Read)) {
		// Shutdown for reading
		ravail = 0;
		rmsgavail = 0;
		rahead.clear();
		rsegs.clear();
		rmsgsize.clear();
		endread = true;
	}

	if (isLinkUp() && !endwrite && (mode & Stream::Write)) {
		// Shutdown for writing
		writeData(NULL, 0, dataCloseFlag);
	}
}

void BaseStream::fail(const QString &err)
{
	disconnect();
	setError(err);
}

void BaseStream::disconnect()
{
	//qDebug() << "Stream" << this << "disconnected: state" << state;

	switch (state) {
	case Disconnected:
		break;

	case WaitFlow: {
		Q_ASSERT(!peerid.isEmpty());
		StreamPeer *peer = h->peers.value(peerid);
		Q_ASSERT(peer != NULL);
		Q_ASSERT(peer->waiting.contains(this));
		peer->waiting.remove(this);
		break; }

	case WaitService:
	case Accepting:
	case Connected:
		Q_ASSERT(flow != NULL);
		flow->detach(this);
		break;
	}
	Q_ASSERT(flow == NULL);

	state = Disconnected;
	if (strm) {
		strm->linkDown();
		// XXX strm->reset?
	}
}

#ifndef QT_NO_DEBUG
void BaseStream::dump()
{
	qDebug() << "Stream" << this << "state" << state << "sid" << sid;
	qDebug() << "  TSN" << tsn << "tqueue" << tqueue.size();
	qDebug() << "  RSN" << rsn << "ravail" << ravail
		<< "rahead" << rahead.size() << "rsegs" << rsegs.size()
		<< "rmsgavail" << rmsgavail << "rmsgs" << rmsgsize.size();
}
#endif


