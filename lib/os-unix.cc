
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ifaddrs.h>

#include <QHostAddress>

#include "os.h"

using namespace SST;


QString SST::userName()
{
	const char *username = getenv("USER");
	if (username == NULL)
		username = getenv("LOGNAME");
	if (username == NULL)
		username = getlogin();
	if (username == NULL)
		username = "someone";
	return QString::fromLocal8Bit(username);
}

QList<QHostAddress> SST::localHostAddrs()
{
	ifaddrs *ifa;
	QList<QHostAddress> qaddrs;
	if (getifaddrs(&ifa) < 0) {
		qWarning("Can't find my own IP addresses!?");
		return qaddrs;
	}

	for (ifaddrs *a = ifa; a; a = a->ifa_next) {
		sockaddr *sa = a->ifa_addr;
		if (!sa)
			continue;
#if 0	// XX Qt bug? - exists in Qt 4.1.0, apparently fixed in 4.1.1
		QHostAddress qa(sa);
		if (qa == QHostAddress::LocalHost ||
		    qa == QHostAddress::LocalHostIPv6)
			continue;
#else	// version that works in 4.1.0...
		QHostAddress qa;
		switch (sa->sa_family) {
		case AF_INET: {
			sockaddr_in *sin = (sockaddr_in*)sa;
			qa.setAddress(ntohl(sin->sin_addr.s_addr));
			if (qa == QHostAddress::LocalHost)
				continue;
			break; }
		case AF_INET6: {
			sockaddr_in6 *sin6 = (sockaddr_in6*)sa;
			Q_ASSERT(sizeof(Q_IPV6ADDR) == 16);
			qa.setAddress(*(Q_IPV6ADDR*)&sin6->sin6_addr);
			if (qa == QHostAddress::LocalHostIPv6)
				continue;
			break; }
		}
#endif
		if (qa.isNull())
			continue;
		qaddrs.append(qa);
		//qDebug() << "Local IP address:" << qa.toString();
	}

	freeifaddrs(ifa);

	return qaddrs;
}

