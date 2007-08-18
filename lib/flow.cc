
#include <cmath>

#include <QDataStream>
#include <QtDebug>

#include "flow.h"
#include "sock.h"
#include "host.h"
#include "xdr.h"

using namespace SST;


#define RTT_INIT	(500*1000)	// Initial RTT estimate: 1/2 second
#define RTT_MAX		(10*1000*1000)	// Max round-trip time: ten seconds

#define CWND_MIN	((unsigned)2)	// Min congestion window (packets/RTT)
#define CWND_MAX	((unsigned)1<<20)// Max congestion window (packets/RTT)

#define ACKDELAY	(10*1000)	// 10 milliseconds (1/100 sec)
#define ACKPACKETS	2		// Max outstanding packets to be ACKed
#define ACKACKPACKETS	4		// Delay before for ACKing only ACKs


enum CCMode {
	CC_TCP,
	CC_AGGRESSIVE,
	CC_DELAY,
	CC_VEGAS,
} ccmode = CC_TCP;


////////// FlowArmor //////////

FlowArmor::~FlowArmor()
{
}


////////// Flow //////////

Flow::Flow(Host *host, QObject *parent)
:	SocketFlow(parent),
	h(host),
	armr(NULL),
	cc(NULL),
	rtxtimer(host),
	linkstat(LinkDown),
	delayack(true),
	acktimer(host),
	statstimer(host)
{
	Q_ASSERT(sizeof(txackmask)*8 == maskBits);

	// Initialize transmit congestion control state
	txseq = 1;
	txdatseq = 0;
	txackseq = 0;
	txackmask = 1;	// Ficticious packet 0 already "received"
	recovseq = 1;
	markseq = 1;
	cwnd = CWND_MIN;
	cwndlim = true;
	ssthresh = CWND_MAX;
	ssbase = 0;
	cwndinc = 1;
	cwndmax = CWND_MIN;
	lastrtt = 0;
	lastpps = 0;
	basertt = 0;
	basepps = 0;
	cumrtt = RTT_INIT;
	cumrttvar = 0;
	cumpps = 0;
	cumppsvar = 0;
	cumpwr = 0;
	cumbps = 0;
	cumloss = 0;

	// Initialize retransmit state
	connect(&rtxtimer, SIGNAL(timeout(bool)),
		this, SLOT(rtxTimeout(bool)));

	// Delayed ACK state
	connect(&acktimer, SIGNAL(timeout(bool)),
		this, SLOT(ackTimeout()));

	// Initialize receive congestion control state
	rxseq = 0;
	rxmask = 1;	// Ficticious packet 0
	rxackct = 0;
	rxunacked = 0;

	// Statistics gathering state
	connect(&statstimer, SIGNAL(timeout(bool)),
		this, SLOT(statsTimeout()));
	//statstimer.start(5*1000);
}

void Flow::start(bool initiator)
{
	Q_ASSERT(armr);

	SocketFlow::start(initiator);

	// We're ready to go!
	rtxstart();
	readyTransmit();

	setLinkStatus(LinkUp);
}

void Flow::stop()
{
	rtxtimer.stop();
	acktimer.stop();
	statstimer.stop();

	SocketFlow::stop();

	setLinkStatus(LinkDown);
}

inline qint64 Flow::markElapsed()
{
	return host()->currentTime().since(marktime).usecs;
}

// Private low-level transmit routine:
// encrypt, authenticate, and transmit a packet
// whose cleartext header and data are already fully set up,
// with a specified ACK sequence/count word.
// Returns true on success, false on error
// (e.g., no output buffer space for packet)
bool Flow::tx(QByteArray &pkt, quint32 packseq, quint64 &pktseq)
{
	Q_ASSERT(isActive());

	// Don't allow txseq counter to wrap (XXX re-key before it does!)
	pktseq = txseq;
	Q_ASSERT(txseq < maxPacketSeq);
	quint32 ptxseq = ((quint32)pktseq & seqMask) |
			(quint32)remoteChannel() << chanShift;

	// Fill in the transmit and ACK sequence number fields.
	Q_ASSERT(pkt.size() >= 8);
	quint32 *pkt32 = (quint32*)pkt.data();
	pkt32[0] = htonl(ptxseq);
	pkt32[1] = htonl(packseq);

	// Encrypt and compute the MAC for the packet
	QByteArray epkt = armr->txenc(txseq, pkt);

	// Bump transmit sequence number,
	// and timestamp if this packet is marked for RTT measurement
	// This is the "Point of no return" -
	// a failure after this still consumes sequence number space.
	if (txseq == markseq) {
		marktime = host()->currentTime();
		markacks = 0;
		markbase = txackseq;
		marksent = txseq - txackseq;
	}
	txseq++;

	//qDebug() << this << "tx seq" << txseq << "size" << epkt.size();

	// Ship it out
	return udpSend(epkt);
}

// Send a standalone ACK packet
bool Flow::transmitAck(QByteArray &pkt, quint64 ackseq, unsigned ackct)
{
	//qDebug() << this << "transmitAck" << ackseq << ackct;

	Q_ASSERT(ackct <= ackctMax);

	if (pkt.size() < hdrlen)
		pkt.resize(hdrlen);
	quint32 packseq = (ackct << ackctShift) | (ackseq & ackSeqMask);
	quint64 pktseq;
	return tx(pkt, packseq, pktseq);
}

// High-level public transmit function.
bool Flow::flowTransmit(QByteArray &pkt, quint64 &pktseq)
{
	Q_ASSERT(pkt.size() > hdrlen);	// should be a nonempty data packet

	// Record the fact that this is "real data" for which we want an ACK.
	txdatseq = txseq;

	// Include implicit acknowledgment of the latest packet(s) we've seen
	quint32 packseq = (rxackct << ackctShift) | (rxseq & seqMask);
	if (rxunacked) {
		rxunacked = 0;
		acktimer.stop();
	}

	// Send the packet
	bool success = tx(pkt, packseq, pktseq);

	// If the retransmission timer is inactive, start it afresh.
	// (If this was a retransmission, rtxTimeout() would have restarted it.)
	if (!rtxtimer.isActive()) {
		//qDebug() << "flowTransmit: rtxstart at time"
		//	<< QDateTime::currentDateTime()
		//		.toString("h:mm:ss:zzz");
		rtxstart();
	}

	return success;
}

int Flow::mayTransmit()
{
	unsigned onthewire = unackedDataPackets();
	if (cwnd > onthewire) {
		return cwnd - onthewire;
	} else {
		cwndlim = true;
		return 0;
	}
}

// Flow::rtxtimer invokes this slot when the retransmission timer expires.
// XX to be really compliant with TCP do we need to have
// a retransmission timer per packet?
void Flow::rtxTimeout(bool fail)
{
	qDebug() << this << "rtxTimeout" << (fail ? "- FAILED" : "")
		<< "period" << rtxtimer.interval();

	// Restart the retransmission timer
	// with an exponentially increased backoff delay.
	rtxtimer.restart();

	// Reset cwnd and go back to slow start
	ssthresh = (txseq - txackseq) / 2;
	ssthresh = qMax(ssthresh, CWND_MIN);
	cwnd = CWND_MIN;
	//qDebug("rtxTimeout: ssthresh = %d, cwnd = %d", ssthresh, cwnd);

	// Notify the upper layer of the un-acked data packets.
	int ackdiff = txdatseq - txackseq;
	if (ackdiff > 0) {
		txackseq = txdatseq;
		if (ackdiff < maskBits)
			txackmask <<= ackdiff;
		else
			txackmask = 0;

		// Must do this last, since it may transmit new packets
		missed(txackseq-ackdiff+1, ackdiff);

		// Finally, notice packets going out-of-window
		expire(txackseq-maskBits+1-ackdiff, ackdiff);
	}

	// Force at least one new packet transmission regardless of cwnd.
	// This might not actually send a packet
	// if there's nothing on our transmit queue -
	// i.e., if no reliable sessions have outstanding data.
	// In that case, rtxtimer stays disarmed until the next transmit.
	readyTransmit();

	// If we exceed a threshold timeout, signal a failed connection.
	// The subclass has no obligation to do anything about this, however.
	setLinkStatus(fail ? LinkDown : LinkStalled);
}

void Flow::receive(QByteArray &pkt, const SocketEndpoint &)
{
	if (!isActive()) {
		qDebug() << this << "receive: inactive flow";
		return;
	}
	if (pkt.size() < hdrlen) {
		qDebug() << this << "receive: runt packet";
		return;
	}

	// Determine the full 64-bit packet sequence number
	quint32 *pkt32 = (quint32*)pkt.data();
	quint32 ptxseq = ntohl(pkt32[0]);
	quint8 pktchan = ptxseq >> chanShift;
	Q_ASSERT(pktchan == localChannel());	// enforced by sock.cc
	qint32 seqdiff = ((qint32)(ptxseq << chanBits)
					- ((qint32)rxseq << chanBits))
				>> chanBits;
	quint64 pktseq = rxseq + seqdiff;
	//qDebug() << this << "rx seq" << pktseq << "size" << pkt.size();

	// Immediately drop too-old or already-received packets
	Q_ASSERT(sizeof(rxmask)*8 == maskBits);
	if (seqdiff > 0) {
		if (pktseq < rxseq) {
			qDebug("Flow receive: 64-bit wraparound detected!");
			return;
		}
	} else if (seqdiff <= -maskBits) {
		qDebug("Flow receive: too-old packet dropped");
		return;
	} else if (seqdiff <= 0) {
		if (rxmask & (1 << -seqdiff)) {
			qDebug("Flow receive: duplicate packet dropped");
			return;
		}
	}

	// Authenticate and decrypt the packet
	if (!armr->rxdec(pktseq, pkt)) {
		qDebug() << this << "receive: auth failed on rx" << pktseq;
		return;
	}

	// Decode the rest of the flow header
	pkt32 = (quint32*)pkt.data(); // might have changed in armr->rxdec()!
	quint32 packseq = ntohl(pkt32[1]);

	// Update our transmit state with the ack info in this packet
	unsigned ackct = (packseq >> ackctShift) & ackctMask;
	qint32 ackdiff = ((qint32)(packseq << chanBits)
					- ((qint32)txackseq << chanBits))
				>> chanBits;
	quint64 ackseq = txackseq + ackdiff;
	//qDebug("Flow: recv seq %llu ack %llu(%d) len %d",
	//	pktseq, ackseq, ackct, pkt.size());
	if (ackseq >= txseq) {
		qDebug() << "Flow receive: got ACK for packet " << ackseq
			<< "not transmitted yet";
		return;
	}

	// Account for newly acknowledged packets
	unsigned newpackets = 0;
	if (ackdiff > 0) {

		// Received acknowledgment for one or more new packets.
		// Roll forward txackseq and txackmask.
		txackseq = ackseq;
		if (ackdiff < maskBits)
			txackmask <<= ackdiff;
		else
			txackmask = 0;

		// Determine the number of actual newly-acknowledged packets,
		// and detect packet loss/delay.
		// XXX wait some dupthresh before fast retransmit!
		newpackets = (unsigned)ackdiff;
		if (newpackets > ackct+1) {
			int nmissed = newpackets - (ackct+1);
			newpackets = ackct+1;
			//qDebug("Missed %d packets %lld-%lld", nmissed,
			//	txackseq-ackdiff+1, txackseq-newpackets);

			// Notify congestion control
			switch (ccmode) {
			case CC_TCP: 
			case CC_DELAY:
			case CC_VEGAS: {
				// Packet loss detected -
				// perform standard TCP congestion control
				if (txackseq-newpackets <= recovseq) {
					// We're in a fast recovery window:
					// this isn't a new loss event.
					break;
				}

				// new loss event: cut ssthresh and cwnd
				//ssthresh = (txseq - txackseq) / 2;	XXX
				ssthresh = cwnd / 2;
				ssthresh = qMax(ssthresh, CWND_MIN);
				//qDebug("%d PACKETS LOST: cwnd %d -> %d",
				//	ackdiff - newpackets, cwnd, ssthresh);
				cwnd = ssthresh;

				// fast recovery for the rest of this window
				recovseq = txseq;

				break; }

			case CC_AGGRESSIVE: {
				// Number of packets we think have been lost
				// so far during this round-trip.
				int lost = (txackseq - markbase) - markacks;
				lost = qMax(0, lost);

				// Number of packets we expect to receive,
				// assuming the lost ones are really lost
				// and we don't lose any more this round-trip.
				unsigned expected = marksent - lost;

				// Clamp the congestion window to this value.
				if (expected < cwnd) {
					qDebug("%d PACKETS LOST: cwnd %d -> %d",
						ackdiff - newpackets,
						cwnd, expected);
					cwnd = ssbase = expected;
					cwnd = qMax(CWND_MIN, cwnd);
				}
				break; }
			}

			// Notify the upper layer of the missed packets
			missed(txackseq - ackdiff + 1, nmissed);
		}

		// Record the new in-sequence packets in txackmask as received.
		// (But note: ackct+1 may also include out-of-sequence pkts.)
		txackmask |= (1 << newpackets) - 1;

		// Notify the upper layer of the acknowledged packets
		acked(txackseq - newpackets + 1, newpackets, pktseq);

		// Finally, notice packets going out-of-window
		expire(txackseq-maskBits+1-ackdiff, ackdiff);

		// Reset the retransmission timer, since we've made progress.
		// Only re-arm it if there's still outstanding unACKed data.
		setLinkStatus(LinkUp);
		if (txdatseq > txackseq) {
			//qDebug() << this << "receive: rtxstart at time"
			//	<< QDateTime::currentDateTime()
			//		.toString("h:mm:ss:zzz");
			rtxstart();
		} else {
			rtxtimer.stop();
		}

		ackdiff = 0;
	}

	// Handle acknowledgments for any straggling out-of-order packets
	// (or an out-of-order acknowledgment for in-order packets).
	// Set the appropriate bits in our txackmask,
	// and count newly acknowledged packets within our window.
	quint32 newmask = (1 << ackct) - 1;
	if ((txackmask & newmask) != newmask) {
		for (unsigned i = 0; i <= ackct; i++) {
			int bit = -ackdiff + i;
			if (bit >= maskBits)
				break;
			if (txackmask & (1 << bit))
				continue;	// already ACKed
			txackmask |= (1 << bit);
			acked(txackseq - bit, 1, pktseq);
			newpackets++;
		}
	}

	// Count the total number of acknowledged packets since the last mark.
	markacks += newpackets;

	switch (ccmode) {
	case CC_TCP: 
	case CC_VEGAS: {
		// During standard TCP slow start procedure,
		// increment cwnd for each newly-ACKed packet.
		// XX TCP spec allows this to be <=,
		// which puts us in slow start briefly after each loss...
		if (newpackets && cwndlim && cwnd < ssthresh) {
			cwnd = qMin(cwnd + newpackets, ssthresh);
			qDebug("Slow start: %d new ACKs; boost cwnd to %d "
				"(ssthresh %d)",
				newpackets, cwnd, ssthresh);
		}
		break; }

	case CC_DELAY:
		if (cwndinc < 0)	// Only slow start during up-phase
			break;
		// fall through...

	case CC_AGGRESSIVE: {
		// We're always in slow start, but we only count ACKs received
		// on schedule and after a per-roundtrip baseline.
		if (markacks > ssbase && markElapsed() <= lastrtt) {
			cwnd += qMin(newpackets, markacks - ssbase);
		//	qDebug("Slow start: %d new ACKs; boost cwnd to %d",
		//		newpackets, cwnd);
		}
		break; }
	}

	// When ackseq passes markseq, we've observed a round-trip,
	// so update our round-trip statistics.
	if (ackseq >= markseq) {

		// 'rtt' is the total round-trip delay in microseconds before
		// we receive an ACK for a packet at or beyond the mark.
		// Fold this into 'rtt' to determine avg round-trip time,
		// and restart the timer to measure the next round-trip.
		int rtt = markElapsed();
		rtt = qMax(1, qMin(RTT_MAX, rtt));
		cumrtt = ((cumrtt * 7.0) + rtt) / 8.0;

		// Compute an RTT variance measure
		float rttvar = fabsf(rtt - cumrtt);
		cumrttvar = ((cumrttvar * 7.0) + rttvar) / 8.0;

		// 'markacks' is the number of unique packets ACKed
		// by the receiver during the time since the last mark.
		// Use this to guage throughput during this round-trip.
		float pps = (float)markacks * 1000000.0 / rtt;
		cumpps = ((cumpps * 7.0) + pps) / 8.0;

		// "Power" measures network efficiency
		// in the sense of both minimizing rtt and maximizing pps.
		float pwr = pps / rtt;
		cumpwr = ((cumpwr * 7.0) + pwr) / 8.0;

		// Compute a PPS variance measure
		float ppsvar = fabsf(pps - cumpps);
		cumppsvar = ((cumppsvar * 7.0) + ppsvar) / 8.0;

		// Calculate loss rate during this last round-trip,
		// and a cumulative loss ratio.
		// Could go out of (0.0,1.0) range due to out-of-order acks.
		float loss = (float)(marksent - markacks) / (float)marksent;
		loss = qMax(0.0f, qMin(1.0f, loss));
		cumloss = ((cumloss * 7.0) + loss) / 8.0;

		// Reset markseq to be the next packet transmitted.
		// The new timestamp will be taken when that packet is sent.
		markseq = txseq;

		switch (ccmode) {
		case CC_TCP:
			// Normal TCP congestion control:
			// during congestion avoidance,
			// increment cwnd once each RTT,
			// but only on round-trips that were cwnd-limited.
			if (cwndlim) {
				cwnd++;
				//qDebug("cwnd %d ssthresh %d",
				//	cwnd, ssthresh);
			}
			cwndlim = false;
			break;

		case CC_AGGRESSIVE:
			break;

		case CC_DELAY:
			if (pwr > basepwr) {
				basepwr = pwr;
				basertt = rtt;
				basepps = pps;
				basewnd = markacks;
			} else if (markacks <= basewnd && rtt > basertt) {
				basertt = rtt;
				basepwr = basepps / basertt;
			} else if (markacks >= basewnd && pps < basepps) {
				basepps = pps;
				basepwr = basepps / basertt;
			}

			if (cwndinc > 0) {
				// Window going up.
				// If RTT makes a significant jump, reverse.
				if (rtt > basertt || cwnd >= CWND_MAX) {
					cwndinc = -1;
				} else {
					// Additively increase the window
					cwnd += cwndinc;
				}
			} else {
				// Window going down.
				// If PPS makes a significant dive, reverse.
				if (pps < basepps || cwnd <= CWND_MIN) {
					ssbase = cwnd++;
					cwndinc = +1;
				} else {
					// Additively decrease the window
					cwnd += cwndinc;
				}
			}
			cwnd = qMax(CWND_MIN, cwnd);
			cwnd = qMin(CWND_MAX, cwnd);
			qDebug("RT: pwr %.0f[%.0f/%.0f]@%d "
				"base %.0f[%.0f/%.0f]@%d "
				"cwnd %d%+d",
				pwr*1000.0, pps, (float)rtt, markacks,
				basepwr*1000.0, basepps, basertt, basewnd,
				cwnd, cwndinc);
			break;

		case CC_VEGAS: {
			if (rtt < basertt)
				basertt = rtt;
			else
				basertt = (basertt * 255.0 + rtt) / 256.0;

			float expect = (float)marksent / basertt;
			float actual = (float)marksent / rtt;
			float diffpps = expect - actual;
			Q_ASSERT(diffpps >= 0.0);
			float diffpprt = diffpps * rtt;

			if (diffpprt < 1.0 && cwnd < CWND_MAX)
				cwnd++;
			else if (diffpprt > 3.0 && cwnd > CWND_MIN)
				cwnd--;

		//	qDebug("Round-trip: basertt %.3f rtt %d "
		//		"exp %.3f act %.3f diff %.3f cwnd %d",
		//		basertt, rtt, expect, actual, diffpprt, cwnd);
			break; }
		}

		//qDebug("Cumulative: rtt %.3f[%.3f] pps %.3f[%.3f] pwr %.3f "
		//	"loss %.3f",
		//	cumrtt, cumrttvar, cumpps, cumppsvar, cumpwr, cumloss);

		lastrtt = rtt;
		lastpps = pps;
	}

	// Always clamp cwnd against CWND_MAX.
	cwnd = qMin(cwnd, CWND_MAX);

	// Pass the received packet to the upper layer for processing.
	// It'll return true if it wants us to ack the packet, false otherwise.
	if (flowReceive(pktseq, pkt))
		received(pktseq, true);
		// XX should still replay-protect even if no ack!

	// Signal upper layer that we can transmit more, if appropriate
	if (newpackets > 0 && mayTransmit())
		readyTransmit();
}

void Flow::received(quint16 pktseq, bool sendack)
{
	//qDebug() << this << "acknowledging" << pktseq
	//	<< (sendack ? "(sending)" : "(not sending)");

	// Update our receive state to account for this packet
	qint32 seqdiff = pktseq - rxseq;
	if (seqdiff == 1) {

		// Received packet is in-order and contiguous.
		// Roll rxseq and rxmask forward appropriately.
		rxseq = pktseq;
		rxmask = (rxmask << 1) + 1;
		rxackct++;
		if (rxackct > ackctMax)
			rxackct = ackctMax;

		// ACK the received packet if appropriate.
		// Delay our ACK for up to ACKPACKETS
		// received non-ACK-only packets,
		// or up to ACKACKPACKETS continuous ack-only packets.
		++rxunacked;
		if (!sendack && rxunacked < ACKACKPACKETS) {
			// Only ack acks occasionally,
			// and don't start the ack timer for them.
			return;
		}
		if (rxunacked < ACKACKPACKETS) {
			// Schedule an ack for transmission
			// by starting the ack timer.
			// We normally do this even in for non-delayed acks,
			// so that we can process any other
			// already-received packets first
			// and have a chance to combine multiple acks into one.
			if (delayack && rxunacked < ACKPACKETS) {
				// Data packet - start delayed ack timer.
				if (!acktimer.isActive())
					acktimer.start(ACKDELAY);
			} else {
				// Start with zero timeout -
				// immediate callback from event loop
				acktimer.start(0);
			}
		} else {
			// But make sure we send an ack every 4 packets
			// no matter what...
			flushack();
		}

	} else if (seqdiff > 1) {

		// Received packet is in-order but discontiguous.
		// One or more packets probably were lost.
		// Flush any delayed ACK immediately,
		// before updating our receive state.
		flushack();

		// Roll rxseq and rxmask forward appropriately.
		rxseq = pktseq;
		if (seqdiff < maskBits)
			rxmask = (rxmask << seqdiff) + 1;
		else
			rxmask = 1;	// bit 0 = packet just received

		// Reset the contiguous packet counter
		rxackct = 0;	// (0 means 1 packet received)

		// ACK this discontiguous packet immediately
		// so that the sender is informed of lost packets ASAP.
		if (sendack)
			txack(rxseq, 0);

	} else if (seqdiff < 0) {

		// Old packet recieved out of order.
		// Flush any delayed ACK immediately.
		flushack();

		// Set appropriate bit in rxmask for replay protection
		Q_ASSERT(seqdiff < 0 && seqdiff > -maskBits);
		rxmask |= (1 << -seqdiff);

		// ACK this out-of-order packet immediately.
		if (sendack)
			txack(pktseq, 0);
	}
}

void Flow::ackTimeout()
{
	flushack();
}

void Flow::statsTimeout()
{
	qDebug("Stats: txseq %llu txdatseq %llu 5xackseq %llu "
		"rxseq %llu "
		"cwnd %d ssthresh %d\n\t"
		"cumrtt %.3f cumpps %.3f cumloss %.3f",
		txseq, txdatseq, txackseq, rxseq, cwnd, ssthresh,
		cumrtt, cumpps, cumloss);
}

void Flow::readyTransmit()
{
}

void Flow::acked(quint64, int, quint64)
{
}

void Flow::missed(quint64, int)
{
}

void Flow::expire(quint64, int)
{
}


////////// ChecksumArmor //////////

ChecksumArmor::ChecksumArmor(uint32_t txkey, uint32_t rxkey)
:	txkey(txkey), rxkey(rxkey)
{
}

QByteArray ChecksumArmor::txenc(qint64 pktseq, const QByteArray &pkt)
{
	// Copy the packet so we can append the checksum
	QByteArray epkt = pkt;
	int size = epkt.size();

	// Compute the checksum for the packet,
	// including the full 64-bit packet sequence number as a pseudo-header.
	Chk32 chk;
	quint32 ivec[2] = { htonl(pktseq >> 32), htonl(pktseq) };
	chk.update(ivec, 8);
	chk.update(epkt.data(), size);
	quint32 sum = htonl(chk.final() + txkey);
	QByteArray sumbuf((const char*)&sum, 4);

	// Append it and return the resulting packet.
	epkt.append(sumbuf);
	return epkt;
}

bool ChecksumArmor::rxdec(qint64 pktseq, QByteArray &pkt)
{
	int size = pkt.size() - 4;
	if (size < Flow::hdrlen)
		return false;	// too small to contain a full checksum

	// Compute the checksum for the packet,
	// including the full 64-bit packet sequence number as a pseudo-header.
	Chk32 chk;
	quint32 ivec[2] = { htonl(pktseq >> 32), htonl(pktseq) };
	chk.update(ivec, 8);
	chk.update(pkt.data(), size);
	quint32 sum = htonl(chk.final() + rxkey);
	QByteArray sumbuf((const char*)&sum, 4);

	// Verify and strip the packet's checksum.
	if (pkt.mid(size) != sumbuf)
		return false;
	pkt.resize(size);
	return true;
}



////////// AESArmor //////////

#include "aes.h"
#include "hmac.h"

AESArmor::AESArmor(const QByteArray &txenckey, const QByteArray &txmackey,
		const QByteArray &rxenckey, const QByteArray &rxmackey)
:	txaes(txenckey, AES::CtrEncrypt), rxaes(rxenckey, AES::CtrDecrypt),
	txmac(txmackey), rxmac(rxmackey)
{
	//qDebug() << this << "txenc" << txenckey.toBase64();
	//qDebug() << this << "rxenc" << rxenckey.toBase64();
	//qDebug() << this << "txmac" << txmackey.toBase64();
	//qDebug() << this << "rxmac" << rxmackey.toBase64();
}

QByteArray AESArmor::txenc(qint64 pktseq, const QByteArray &pkt)
{
	int size = pkt.size();
	const quint8 *buf = (const quint8*)pkt.constData();

	// Create a buffer for the encrypted packet
	QByteArray epkt;
	epkt.resize(size);
	quint8 *ebuf = (quint8*)epkt.data();

	// Build the initialization vector template for encryption.
	// We also use the first 8 bytes as a pseudo-header for the MAC.
	ivec.l[0] = htonl(pktseq >> 32);
	ivec.l[1] = htonl(pktseq);
	ivec.l[2] = htonl(0x56584166);	// 'VXAf'
	ivec.l[3] = 0;	// per-packet block counter

	// Copy the unencrypted header (XX hack)
	Q_ASSERT(encofs == 4);
	*(quint32*)ebuf = *(quint32*)buf;

	// Encrypt the block in CTR mode
	txaes.ctrEncrypt(buf + encofs, ebuf + encofs, size - encofs, &ivec);

	// Compute the MAC for the packet,
	// including the full 64-bit packet sequence number as a pseudo-header.
	HMAC hmac(txmac);
	hmac.update(ivec.b, 8);
	hmac.update(ebuf, size);
	hmac.finalAppend(epkt);
	Q_ASSERT(epkt.size() == size + HMACLEN);

	//qDebug() << this << "txenc" << pktseq << epkt.size();

	return epkt;
}

bool AESArmor::rxdec(qint64 pktseq, QByteArray &pkt)
{
	//qDebug() << this << "rxdec" << pktseq << pkt.size();

	int size = pkt.size() - HMACLEN;
	if (size < Flow::hdrlen) {
		qDebug() << this << "rxdec: received packet too small";
		return false;	// too small to contain a full HMAC
	}

	// Build the initialization vector template for decryption.
	// We also use the first 8 bytes as a pseudo-header for the MAC.
	ivec.l[0] = htonl(pktseq >> 32);
	ivec.l[1] = htonl(pktseq);
	ivec.l[2] = htonl(0x56584166);	// 'VXAf'
	ivec.l[3] = 0;	// per-packet block counter

	// Verify the packet's MAC.
	HMAC hmac(rxmac);
	hmac.update(ivec.b, 8);
	hmac.update(pkt.data(), size);
	if (!hmac.finalVerify(pkt))
		return false;

	// Decrypt the block in CTR mode
	quint8 *buf = (quint8*)pkt.data();
	rxaes.ctrDecrypt(buf + encofs, buf + encofs, size - encofs, &ivec);

	return true;
}

