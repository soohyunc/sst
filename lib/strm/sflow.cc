
#include <netinet/in.h>

#include <QtDebug>

#include "strm/base.h"
#include "strm/peer.h"
#include "strm/sflow.h"

using namespace SST;


////////// StreamFlow //////////

StreamFlow::StreamFlow(Host *h, StreamPeer *peer, const QByteArray &peerid)
:	Flow(h, peer),
	peer(peer),
	root(h),
	nextsid(1)
{
	root.state = BaseStream::Connected;
	root.sid = 0;
	root.flow = this;
	root.setParent(NULL);	// XXX
	idhash.insert(sidRoot, &root);
	idhash.insert(sidRoot^sidOrigin, &root);
	root.listen();
	root.peerid = peerid;
}

StreamFlow::~StreamFlow()
{
	//qDebug() << "~StreamFlow to" << remoteEndpoint().toString();

	stop();

	detach(&root);
	idhash.remove(sidRoot^sidOrigin);
	root.state = BaseStream::Disconnected;
}

StreamProtocol::StreamID StreamFlow::attach(BaseStream *stream, StreamID sid)
{
	//qDebug() << "Flow" << this << "attach" << stream;

	Q_ASSERT(stream->flow == NULL);
	Q_ASSERT(stream->sid == 0);

	if (sid == 0) {
		// We need to allocate a StreamID for this stream.
		sid = nextsid;

		Q_ASSERT(sid > 0 && sid < sidOrigin);
		while (idhash.contains(sid)) {
			if (++sid >= sidOrigin)
				sid = 1;
			if (sid == nextsid) {
				qDebug("StreamFlow: out of Stream IDs!");
				return 0;
			}
		}

		nextsid = sid+1;
		if (nextsid >= sidOrigin)
			nextsid = 1;
	} else {
		// The other side initiated the stream,
		// so it already has a StreamID.
		Q_ASSERT(sid >= sidOrigin);
		stream->mature = true;
	}

	stream->flow = this;
	stream->sid = sid;
	idhash.insert(sid, stream);

	return sid;
}

void StreamFlow::detach(BaseStream *stream)
{
	//qDebug() << "Flow" << this << "detach" << stream;

	Q_ASSERT(stream->flow == this);
	Q_ASSERT(idhash.value(stream->sid) == stream);

	// Break the stream's association with us and free its SID
	idhash.remove(stream->sid);
	stream->flow = NULL;
	stream->sid = 0;

	// Remove the stream from our waiting streams list
	int rc = dequeueStream(stream);
	Q_ASSERT(rc <= 1);

	// XXX clear out ackwait
	foreach (qint64 txseq, ackwait.keys()) {
		BaseStream::Packet &p = ackwait[txseq];
		Q_ASSERT(!p.isNull());
		Q_ASSERT(p.strm);
		if (p.strm != stream)
			continue;

		// Move the packet back onto the stream's transmit queue
		stream->tqueue.enqueue(p);
		ackwait.remove(txseq);
	}
}

void StreamFlow::enqueueStream(BaseStream *strm)
{
	// Find the correct position at which to enqueue this stream,
	// based on priority.
	int i = 0;
	while (i < tstreams.size()
			&& tstreams[i]->priority() >= strm->priority())
		i++;

	// Insert.
	tstreams.insert(i, strm);
}

void StreamFlow::readyTransmit()
{
	if (tstreams.isEmpty())
		return;

	// Round-robin between our streams for now.
	do {
		BaseStream *strm = tstreams.dequeue();

		// Dequeue a packet from this stream's transmit queue.
		Q_ASSERT(!strm->tqueue.isEmpty());
		BaseStream::Packet p = strm->tqueue.dequeue();

		// Prepare the packet for transmission on this flow,
		// setting up the header with the correct stream IDs etc.
		strm->txPrepare(p, this);

		// Transmit it.
		quint64 pktseq;
		flowTransmit(p.buf, pktseq);
		Q_ASSERT(txseq);	// XXX
		//qDebug() << strm << "tx " << pktseq
		//	<< "posn" << p.tsn << "size" << p.buf.size();

		// If it's a datagram, drop the buffer; we won't need it.
		if (p.dgram)
			p.buf.clear();

		// Save the packet in our global ackwait hash.
		ackwait.insert(pktseq, p);

		// If this stream still has more to transmit, re-queue it.
		if (!strm->tqueue.isEmpty())
			enqueueStream(strm);

	} while (!tstreams.isEmpty() && mayTransmit());
}

void StreamFlow::acked(qint64 txseq, int npackets)
{
	for (; npackets > 0; txseq++, npackets--) {
		BaseStream::Packet p = ackwait.take(txseq);
		if (p.isNull())
			continue;

		//qDebug() << "Got ack for packet" << txseq
		//	<< "of size" << p.buf.size();
		Q_ASSERT(p.strm->flow == this);
		p.strm->acked(p);
	}
}

void StreamFlow::missed(qint64 txseq, int npackets)
{
	for (; npackets > 0; txseq++, npackets--) {
		BaseStream::Packet p = ackwait.take(txseq);
		if (p.isNull()) {
			//qDebug() << "Missed packet" << txseq
			//	<< "but can't find it!";
			continue;
		}

		//qDebug() << "Missed packet" << txseq
		//	<< "of size" << p.buf.size();
		Q_ASSERT(p.strm->flow == this);
		p.strm->missed(p);
	}
}

void StreamFlow::flowReceive(qint64, QByteArray &pkt)
{
	if (pkt.size() < 4) {
		qDebug("flowReceive: got runt packet");
		return;	// XX Protocol error: close flow?
	}

	// Lookup the stream
	StreamHeader *hdr = (StreamHeader*)(pkt.data() + Flow::hdrlen);
	StreamID psid = ntohs(hdr->sid) ^ sidOrigin;
	BaseStream *strm = idhash.value(psid);
	if (!strm) {
		qDebug("flowReceive: packet for unknown stream %d", psid);
		return;	// XX Protocol error: close flow?
	}

	//qDebug() << "Received packet" << pktseq;

	// Handle the packet according to its type
	switch ((PacketType)(hdr->type >> typeShift)) {
	case InitPacket:	strm->rxInitPacket(pkt); return;
	case DataPacket:	strm->rxDataPacket(pkt); return;
	case DatagramPacket:	strm->rxDatagramPacket(pkt); return;
	case ResetPacket:	strm->rxResetPacket(pkt); return;
	default:
		qDebug("flowReceive: unknown packet type %d", hdr->type);
		return;	// XX Protocol error: close flow?
	};
}

void StreamFlow::failed()
{
	Q_ASSERT(isActive());

	StreamPeer *peer = target();
	Q_ASSERT(peer);

	// If we were our target's primary flow, disconnect us.
	if (peer->flow == this) {
		qDebug() << "Primary flow to host ID" << peer->id.toBase64()
			<< "at endpoint" << remoteEndpoint().toString()
			<< "failed";
		peer->flow = NULL;
	}

	// Stop and destroy this flow.
	stop();
	deleteLater();
}

void StreamFlow::start()
{
	Flow::start();
	Q_ASSERT(isActive());

	// If our target doesn't yet have an active flow, use this one.
	StreamPeer *peer = target();
	if (peer->flow == NULL)
		peer->setPrimary(this);
}

void StreamFlow::stop()
{
	//qDebug() << "StreamFlow::stop";

	Flow::stop();

	// XXX clean up tstreams, ackwait

	// Detach and notify all affected streams.
	foreach (BaseStream *strm, idhash) {
		if (strm == &root)
			continue;	// Don't detach root streams
		Q_ASSERT(strm->sid != 0);
		Q_ASSERT(strm->flow == this);

		// XXX only detach persistent streams
		strm->fail(tr("Connection to host ID %0 at %1 failed")
			.arg(QString(target()->remoteHostId().toBase64()))
			.arg(remoteEndpoint().toString()));
		Q_ASSERT(strm->flow == NULL);
		Q_ASSERT(strm->sid == 0);
	}
}


