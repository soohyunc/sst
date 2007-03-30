#ifndef MAIN_H
#define MAIN_H

namespace SST {

class Host;


// Since the UCB traces don't give us request lengths,
// just use some hopefully-reasonable fixed request length.
#define REQLEN		128


extern enum TestProto {
	TESTPROTO_SST,		// Userland SST
	TESTPROTO_TCP,		// Userland TCP
	TESTPROTO_NAT,		// Native TCP
	TESTPROTO_UDP,		// Native UDP
} testproto;

} // namespace SST

#endif	// MAIN_H
