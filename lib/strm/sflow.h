#ifndef SST_STRM_SFLOW_H
#define SST_STRM_SFLOW_H

#include <QHash>
#include <QList>
#include <QQueue>

#include "../flow.h"	// XXX
#include "strm/proto.h"

namespace SST {

class BaseStream;
class StreamPeer;
class StreamResponder;

class StreamFlow : public Flow, public StreamProtocol
{
	friend class BaseStream;
	friend class StreamPeer;
	friend class StreamResponder;

	// StreamPeer this flow is associated with.
	// A StreamFlow is always either a direct child of its StreamPeer,
	// or a child of a KeyInitiator which is a child of its StreamPeer,
	// so there should be no chance of this pointer ever dangling.
	StreamPeer *peer;

	// Top-level stream used for connecting to services
	BaseStream root;

	// Hash table of active streams indexed by stream ID
	QHash<StreamID, BaseStream*> idhash;	// Stream ID -> BaseStream
	StreamID nextsid;			// Next StreamID to use

	// List of closed stream IDs waiting for close acknowledgment
	QList<StreamID> closed;

	// Round-robin queue of Streams with packets waiting to transmit
	// XX would prefer a smarter scheduling algorithm, e.g., stride.
	QQueue<BaseStream*> tstreams;

//	// Packets transmitted and waiting for acknowledgment,
//	// inserted and kept in order of txseq.
//	QQueue<BaseStream::Packet> ackqueue;

	// Packets transmitted and waiting for acknowledgment,
	// indexed by assigned transmit sequence number.
	QHash<qint64,BaseStream::Packet> ackwait;


	// Attach a stream to this flow, allocating a SID for it if necessary.
	StreamID attach(BaseStream *bs, StreamID sid = 0);
	void detach(BaseStream *bs);

	StreamFlow(Host *h, StreamPeer *peer, const QByteArray &peerid);
	~StreamFlow();

	inline StreamPeer *target() { return peer; }

	inline int dequeueStream(BaseStream *strm)
		{ return tstreams.removeAll(strm); }
	void enqueueStream(BaseStream *strm);

	virtual void readyTransmit();
	virtual void flowReceive(qint64 rxseq, QByteArray &pkt);
	virtual void acked(qint64 txseq, int npackets);
	virtual void missed(qint64 txseq, int npackets);
	virtual void failed();

	virtual void start();
	virtual void stop();
};

} // namespace SST

#endif	// SST_STRM_SFLOW_H
