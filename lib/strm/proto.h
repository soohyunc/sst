#ifndef SST_STRM_PROTO_H
#define SST_STRM_PROTO_H

#include <QPair>

#include "flow.h"


namespace SST {


/** @internal SST stream protocol definitions.
 * This class simply provides SST protcol definition constants
 * for use in the other Stream classes below.
 */
class StreamProtocol
{
public:
	// Control chunk magic value for the structured stream transport.
	// 0x535354 = 'SST': 'Structured Stream Transport'
	static const quint32 magic = 0x00535354;

	// Maximum transmission unit. XX should be dynamic.
	static const int mtu = 1200;

	// Maximum size of datagram to send using the stateless optimization.
	// XX should be dynamic.
	static const int maxStatelessDatagram = mtu * 4;

	// Sizes of various stream header types
	static const unsigned hdrlenInit		= Flow::hdrlen + 8;
	static const unsigned hdrlenReply		= Flow::hdrlen + 8;
	static const unsigned hdrlenData		= Flow::hdrlen + 8;
	static const unsigned hdrlenDatagram		= Flow::hdrlen + 4;
	static const unsigned hdrlenReset		= Flow::hdrlen + 4;

	// Header layouts
	struct StreamHeader {
		quint16	sid;
		quint8	type;
		quint8	win;
	};

	// The Type field is divided into a 4-bit major type
	// and a 4-bit subtype/flags field.
	static const unsigned typeBits		= 4;
	static const unsigned typeMask		= (1 << typeBits) - 1;
	static const unsigned typeShift		= 4;
	static const unsigned subtypeBits	= 4;
	static const unsigned subtypeMask	= (1 << subtypeBits) - 1;
	static const unsigned subtypeShift	= 0;

	// Major packet type codes (4 bits)
	enum PacketType {
		InvalidPacket	= 0x0,		// Always invalid
		InitPacket	= 0x1,		// Initiate new stream
		ReplyPacket	= 0x2,		// Reply to new stream
		DataPacket	= 0x3,		// Regular data packet
		DatagramPacket	= 0x4,		// Best-effort datagram
		ResetPacket	= 0x5,		// Reset stream
		AttachPacket	= 0x6,		// Attach stream
		DetachPacket	= 0x7,		// Detach stream
	};

	// The Window field consists of some flags and a 5-bit exponent.
	static const unsigned winSubstreamFlag	= 0x80;	// Substream window
	static const unsigned winInheritFlag	= 0x40;	// Inherited window
	static const unsigned winExpBits	= 5;
	static const unsigned winExpMask	= (1 << winExpBits) - 1;
	static const unsigned winExpMax		= winExpMask;
	static const unsigned winExpShift	= 0;

	struct InitHeader : public StreamHeader {
		quint16 nsid;			// New Stream ID
		quint16 tsn;			// 16-bit transmit seq no
	};
	typedef InitHeader ReplyHeader;

	struct DataHeader : public StreamHeader {
		quint32 tsn;			// 32-bit transmit seq no
	};

	typedef StreamHeader DatagramHeader;
	typedef StreamHeader ResetHeader;


	// Subtype/flag bits for Init, Reply, and Data packets
	static const quint8 dataPushFlag	= 0x4;	// Push to application
	static const quint8 dataMessageFlag	= 0x2;	// End of message
	static const quint8 dataCloseFlag	= 0x1;	// End of stream
	static const quint8 dataAllFlags	= 0x7;	// All signal flags

	// Flag bits for Datagram packets
	static const quint8 dgramBeginFlag	= 0x2;	// First fragment
	static const quint8 dgramEndFlag	= 0x1;	// Last fragment


	// Other types
	typedef quint16 StreamID;		// Stream ID
	typedef quint32 StreamSeq;		// Stream byte sequence number

	struct UniqueStreamID {
		quint64 chanId;			// Unique channel+direction ID
		quint64 streamCtr;		// Stream counter in channel
	};

	// The topmost bit in a StreamID indicates its origin:
	// 0 means "originated from me", 1 means "originated from you".
	static const StreamID sidOrigin = 0x8000;

	// Stream ID 0 and 0^sidOrigin always refer to the root stream.
	static const StreamID sidRoot = 0x0000;


	// Index values for [dir] dimension in attach array
	enum AttachDir {
		Local = 0,		// my SID space
		Remote = 1,		// peer's SID space
	};

	// Number of redundant attachment points per stream
	static const int maxAttach = 2;

	// Maximum number of in-use SIDs to skip while trying to allocate one,
	// before we just give up and detach an existing one in this range.
	static const int maxSidSkip = 16;


	// Service message codes
	enum ServiceCode {
		ConnectRequest	= 0x101,	// Connect to named service
		ConnectReply	= 0x201,	// Response to connect request
	};

	// Maximum size of a service request or response message
	static const int maxServiceMsgSize = 1024;

	// Service/protocol pairs used to index registered StreamServers.
	typedef QPair<QString,QString> ServicePair;


private:
	friend class Stream;
	friend class StreamFlow;
	friend class StreamResponder;
};

} // namespace SST

inline uint qHash(const SST::StreamProtocol::ServicePair &svpair)
	{ return qHash(svpair.first) + qHash(svpair.second); }

#endif	// SST_STRM_PROTO_H
