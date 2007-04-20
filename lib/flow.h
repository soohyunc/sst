#ifndef SST_FLOW_H
#define SST_FLOW_H

#include <QTime>
#include <QTimer>

#include "ident.h"
#include "sock.h"
#include "timer.h"

// XXX for specific armor methods - break into separate module
#include "chk32.h"
#include "aes.h"
#include "hmac.h"


namespace SST {

class Host;
class Ident;
class Endpoint;
class Socket;
class FlowCC;
class KeyInitiator;	// XXX


// Abstract base class for flow encryption and authentication schemes.
class FlowArmor
{
	friend class Flow;

protected:
	// The pseudo-header is a header logically prepended to each packet
	// for authentication purposes but not actually transmitted.
	// XX probably can't always be static const
	//static const int phlen = 4;

	// The encryption offset defines the offset in the transmitted packet
	// at which encrypted data begins (after authenticate-only data).
	static const int encofs = 4;

	virtual QByteArray txenc(qint64 pktseq, const QByteArray &pkt) = 0;
	virtual bool rxdec(qint64 pktseq, QByteArray &pkt) = 0;

	virtual ~FlowArmor();
};

// Abstract base class representing a flow
// between a local Socket and a remote endpoint.
class Flow : public SocketFlow
{
	friend class KeyInitiator;	// XXX
	Q_OBJECT

private:
	Host *const h;
	FlowArmor *armr;	// Encryption/authentication method
	FlowCC *cc;		// Congestion control method


public:
	// Amount of space client must leave at the beginning of a packet
	// to be transmitted with flowTransmit() or received via flowReceive().
	// XX won't always be static const.
	static const int hdrlen = 8;

	// Layout of the first header word: channel number, tx sequence
	// Transmitted in cleartext.
	static const quint32 chanBits = 8;	// 31-24: channel number
	static const quint32 chanMask = (1 << chanBits) - 1;
	static const quint32 chanMax = chanMask;
	static const quint32 chanShift = 24;
	static const quint32 seqBits = 24;	// 23-0: tx sequence number
	static const quint32 seqMask = (1 << seqBits) - 1;

	// Layout of the second header word: ACK count/sequence number
	// Encrypted for transmission.
	static const quint32 resvBits = 4;	// 31-28: reserved field
	static const quint32 ackctBits = 4;	// 27-24: ack count
	static const quint32 ackctMask = (1 << ackctBits) - 1;
	static const quint32 ackctMax = ackctMask;
	static const quint32 ackctShift = 24;
	static const quint32 ackSeqBits = 24;	// 23-0: ack sequence number
	static const quint32 ackSeqMask = (1 << ackSeqBits) - 1;


	Flow(Host *host, QObject *parent = NULL);

	inline Host *host() { return h; }

	// Set the encryption/authentication method for this flow.
	// This MUST be set before a new flow can be activated.
	inline void setArmor(FlowArmor *armor) { this->armr = armor; }
	inline FlowArmor *armor() { return armr; }

	// Set the congestion controller for this flow.
	// This must be set if the client wishes to call mayTransmit().
	inline void setCongestionController(FlowCC *cc) { this->cc = cc; }
	inline FlowCC *congestionController() { return cc; }

	// Start and stop the flow.
	virtual void start();
	virtual void stop();


protected:

	// Size of rxmask and txackmask fields in bits
	static const int maskBits = 32;

	// Transmit state
	quint64 txseq;		// Next sequence number to transmit
	quint64 txdatseq;	// Seqno of last real data packet transmitted
	quint64 txackseq;	// Highest transmit sequence number ACK'd
	quint64 recovseq;	// Sequence at which fast recovery finishes
	quint64 markseq;	// Transmit sequence number of "marked" packet
	quint64 markbase;	// Snapshot of txackseq at time mark was placed
	Time marktime;		// Time at which marked packet was sent
	quint32 txackmask;	// Mask of packets transmitted and ACK'd
	quint32 markacks;	// Number of ACK'd packets since last mark
	quint32 marksent;	// Number of ACKs expected after last mark
	quint32 cwnd;		// Current congestion window
	bool cwndlim;		// We were cwnd-limited this round-trip

	// TCP congestion control
	quint32 ssthresh;	// Slow start threshold

	// Aggressive congestion control
	quint32 ssbase;		// Slow start baseline

	// Low-delay congestion control
	int cwndinc;
	int lastrtt;		// Measured RTT of last round-trip
	float lastpps;		// Measured PPS of last round-trip
	quint32 basewnd;
	float basertt, basepps, basepwr;

	// TCP Vegas-like congestion control
	float cwndmax;

	// Retransmit state
	Timer rtxtimer;		// Retransmit timer

	// Receive state
	quint64 rxseq;		// Last sequence number received
	quint32 rxmask;		// Mask of packets recently received
	quint8 rxackct;		// # contiguous packets received before rxseq
	quint8 rxunacked;	// # contiguous packets not yet ACKed

	// Delayed ACK state
	bool delayack;		// Enable delayed acknowledgments
	Timer acktimer;		// Delayed ACK timer

	// Statistics gathering
	float cumrtt;		// Cumulative measured RTT in milliseconds
	float cumrttvar;	// Cumulative variation in RTT
	float cumpps;		// Cumulative measured packets per second
	float cumppsvar;	// Cumulative variation in PPS
	float cumpwr;		// Cumulative measured network power (pps/rtt)
	float cumbps;		// Cumulative measured bytes per second
	float cumloss;		// Cumulative measured packet loss ratio
	Timer statstimer;


	// Transmit a packet across the flow.
	// Caller must leave hdrlen bytes at the beginning for the header.
	// The packet is armored in-place in the provided QByteArray.
	// It is the caller's responsibility to transmit
	// only when flow control says it's OK (mayTransmit())
	// or upon getting a readyTransmit() callback.
	// Provides in 'pktseq' the transmit sequence number
	// that was assigned to the packet.
	// Returns true if the transmit was successful,
	// or false if it failed (e.g., due to lack of buffer space);
	// a sequence number is assigned even on failure however.
	bool flowTransmit(const QByteArray &pkt, quint64 &pktseq);

	// Check congestion control state and return the number of new packets,
	// if any, that flow control says we may transmit now.
	int mayTransmit();

	// Compute current number of transmitted but un-acknowledged packets.
	// This count may include raw ACK packets,
	// for which we expect no acknowledgments
	// unless they happen to be piggybacked on data coming back.
	inline qint64 unackedPackets()
		{ return txseq - txackseq; }

	// Compute current number of un-acknowledged data packets.
	// Note that txackseq might be greater than txdatseq
	// if we have sent ACKs since our last real data transmission
	// and the other side has ACKed some of those ACKs
	// (e.g., piggybacked on data packets it is sending us).
	inline qint64 unackedDataPackets()
		{ return (txdatseq > txackseq) ? (txdatseq - txackseq) : 0; }

	// Compute the time elapsed since the mark in microseconds.
	qint64 markElapsed();

public:
	inline bool deleyedAcks() const { return delayack; }
	inline void setDelayedAcks(bool enabled) { delayack = enabled; }

protected:
	// Main method for upper-layer subclass to receive a packet on a flow.
	virtual void flowReceive(qint64 pktseq, QByteArray &pkt) = 0;

	virtual void readyTransmit();
	virtual void acked(qint64 txseq, int npackets);
	virtual void missed(qint64 txseq, int npackets);
	virtual void failed();


private:
	// Called by Socket to dispatch a received packet to this flow.
	virtual void receive(QByteArray &msg, const SocketEndpoint &src);

	// Internal transmit methods.
	bool tx(const QByteArray &pkt, quint32 packseq,
			quint64 &pktseq);
	bool txack(quint32 seq, unsigned ackct);
	inline void flushack()
		{ if (rxunacked) { rxunacked = 0; txack(rxseq, rxackct); }
		  acktimer.stop(); }

	inline void rtxstart()
		{ rtxtimer.start((int)(cumrtt * 2.0)); }


private slots:
	void rtxTimeout(bool failed);	// Retransmission timeout
	void ackTimeout();	// Delayed ACK timeout
	void statsTimeout();
};



// XX break this stuff into separate module

// Simple 32-bit keyed checksum protection with no encryption,
// to defend only against off-the-path attackers
// who can inject forged packets but not monitor the flow.
class ChecksumArmor : public FlowArmor
{
	const uint32_t txkey, rxkey;

public:
	ChecksumArmor(uint32_t txkey, uint32_t rxkey);

	virtual QByteArray txenc(qint64 pktseq, const QByteArray &pkt);
	virtual bool rxdec(qint64 pktseq, QByteArray &pkt);
};


class AESArmor : public FlowArmor
{
	const AES txaes, rxaes;
	const HMAC txmac, rxmac;

	union ivec {
		quint8 b[AES_BLOCK_SIZE];
		quint32 l[4];
	} ivec;

public:
	AESArmor(const QByteArray &txenckey, const QByteArray &txmackey,
		const QByteArray &rxenckey, const QByteArray &rxmackey);

	virtual QByteArray txenc(qint64 pktseq, const QByteArray &pkt);
	virtual bool rxdec(qint64 pktseq, QByteArray &pkt);
};

} // namespace SST

#endif	// SST_FLOW_H