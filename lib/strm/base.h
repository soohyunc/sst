#ifndef SST_STRM_BASE_H
#define SST_STRM_BASE_H

#include <QQueue>

#include "stream.h"
#include "strm/abs.h"

namespace SST {

class Stream;
class StreamFlow;
class StreamPeer;


/** @internal Basic internal stream control object.
 * The separation between the internal stream control object
 * and the application-visible Stream object is primarily needed
 * so that SST can hold onto a stream's state and gracefully shut it down
 * after the application deletes its Stream object representing it.
 * This separation also keeps the internal stream control variables
 * out of the public C++ API header files and thus able to change
 * without breaking binary compatibility,
 * and makes it easy to implement service/protocol negotiation
 * for top-level application streams by extending BaseStream (see AppStream).
 *
 * @see Stream, AppStream
 */
class BaseStream : public AbstractStream
{
	friend class Stream;
	friend class StreamFlow;
	friend class StreamPeer;
	Q_OBJECT

private:
	enum State {
		Disconnected = 0,
		WaitFlow,		// Initiating, waiting for flow
		WaitService,		// Initiating, waiting for svc reply
		Accepting,		// Accepting, waiting for svc request
		Connected,
	};

	struct Packet {
		BaseStream *strm;
		//qint64 txseq;			// Transmit sequence number
		qint64 tsn;			// Logical byte position
		QByteArray buf;			// Packet buffer incl. headers
		int hdrlen;			// Size of flow + stream hdrs
		bool dgram;			// Is an unreliable datagram

		inline Packet() { strm = NULL; }

		inline bool isNull() const { return strm == NULL; }
	};

	struct RxSegment {
		qint32 rsn;			// Logical byte position
		QByteArray buf;			// Packet buffer incl. headers
		int hdrlen;			// Size of flow + stream hdrs

		inline int segmentSize() const
			{ return buf.size() - hdrlen; }

		inline StreamHeader *hdr()
			{ return (StreamHeader*)(buf.data() + Flow::hdrlen); }
		inline const StreamHeader *constHdr() const
			{ return (const StreamHeader*)
					(buf.constData() + Flow::hdrlen); }

		inline quint8 flags() const
			{ return constHdr()->type & dataAllFlags; }
		inline bool hasFlags() const
			{ return flags() != 0; }
	};

	// Connection state
	State		state;
	StreamFlow	*flow;			// Flow we're attached to
	BaseStream	*parent;		// Parent if non-root
			// XXX what if child outlives?
	StreamID	sid;			// Stream ID if Connected
	bool		mature;			// Seen at least one round-trip
	bool		endread;		// Seen or forced EOF on read
	bool		endwrite;		// We've written our EOF marker

	// Transmit state
	qint32		tsn;			// Next SSN to transmit
	qint32		twin;			// Transmit window size
	QQueue<Packet>	tqueue;			// Waiting to be transmitted

	// Byte-stream receive state
	qint64		ravail;			// Received bytes available
	qint64		rmsgavail;		// Bytes avail in cur message
	quint8		rwinbyte;		// Receive window log2
	qint32		rsn;			// Next SSN expected to arrive
	QList<RxSegment> rahead;		// Received out of order
	QQueue<RxSegment> rsegs;		// Received, waiting to be read
	QQueue<qint64>	rmsgsize;		// Sizes of received messages

	// Substream receive state
	QQueue<AbstractStream*> rsubs;		// Received, waiting substreams


private:
	void connectToFlow(StreamFlow *flow);
	void gotServiceReply();
	void gotServiceRequest();

	void txqueue(const Packet &pkt);
	void txPrepare(Packet &pkt, StreamFlow *flow);

	BaseStream *rxinit(StreamID nsid);
	void rxSegment(RxSegment &rseg);

	// Packet receive methods called from StreamFlow
	void rxInitPacket(const QByteArray &pkt);
	void rxDataPacket(const QByteArray &pkt);
	void rxDatagramPacket(const QByteArray &pkt);
	void rxResetPacket(const QByteArray &pkt);

	// StreamFlow calls these to return our transmitted packets to us
	// after being held in ackwait.
	void acked(const Packet &pkt);
	void missed(const Packet &pkt);

	// Disconnect and set an error condition.
	void fail(const QString &err);


private slots:
	// We connect this to the readyReadMessage() signals
	// of any substreams queued in our rsubs list waiting to be accepted,
	// in order to forward the indication to the client
	// via the parent stream's readyReadDatagram() signal.
	void subReadMessage();


public:
	BaseStream(Host *host);
	virtual ~BaseStream();

	/** Connect to a given service on a remote host.
	 * @param dstid the endpoint identifier (EID)
	 *		of the desired remote host to connect to.
	 *		The destination may be either a cryptographic EID
	 * 		or a non-cryptographic legacy address
	 *		as defined by the Ident class.
	 * @param service the service name to connect to on the remote host.
	 *		This parameter replaces the port number
	 *		that TCP traditionally uses to differentiate services.
	 * @param dstep	an optional location hint
	 *		for SST to use in attempting to contact the host.
	 *		If the dstid parameter is a cryptographic EID,
	 *		which is inherently location-independent,
	 *		SST may need a location hint to find the remote host
	 *		if this host and the remote host are not currently
	 *		registered at a common registration server,
	 *		for example.
	 *		This parameter is not needed
	 *		if the dstid is a non-cryptographic legacy address.
	 * @see Ident
	 */
	void connectTo(const QByteArray &dstid,
			const QString &service, const QString &protocol,
			const Endpoint &dstep = Endpoint());

	/// Returns true if the underlying link is currently connected
	/// and usable for data transfer.
	inline bool isLinkUp()
		{ return state == Connected; }

	/** Set the stream's transmit priority level.
	 * This method overrides AbstractStream's default method
	 * to move the stream to the correct transmit queue if necessary.
	 */
	void setPriority(int pri);

	// Implementations of AbstractStream's data I/O methods
	virtual qint64 bytesAvailable() const { return ravail; }
	virtual int readData(char *data, int maxSize);
	virtual int writeData(const char *data, int maxSize,
				quint8 endflags);

	virtual int pendingMessages() const
		{ return rmsgsize.size(); }
	virtual qint64 pendingMessageSize() const
		{ return hasPendingMessages() ? rmsgsize.at(0) : -1; }
	virtual int readMessage(char *data, int maxSize);
	virtual QByteArray readMessage(int maxSize);

	virtual bool atEnd() const { return endread; }


	////////// Substreams //////////

	// Initiate or accept substreams
	virtual AbstractStream *openSubstream();
	virtual AbstractStream *acceptSubstream();

	// Send and receive unordered, unreliable datagrams on this stream.
	virtual int writeDatagram(const char *data, int size);
	AbstractStream *getDatagram();
	virtual int readDatagram(char *data, int maxSize);
	virtual QByteArray readDatagram(int maxSize);


	void shutdown(Stream::ShutdownMode mode);

	/// Immediately reset a stream to the disconnected state.
	/// Outstanding buffered data may be lost.
	void disconnect();

	/// Dump the state of this stream, for debugging purposes.
	void dump();


signals:
	void readyReadMessage();
};

} // namespace SST

#endif	// SST_STRM_BASE_H
