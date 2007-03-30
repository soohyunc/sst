//
// OS-specific networking facilities
//
#ifndef SST_NET_H
#define SST_NET_H

class QHostAddress;

#ifdef WIN32	// htonl, ntohl
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif


namespace SST {

// Find the current user's name according to the operating system.
QString userName();

// Find all of the local host's IP addresses (OS specific)
QList<QHostAddress> localHostAddrs();

} // namespace SST

#endif	// SST_NET_H
