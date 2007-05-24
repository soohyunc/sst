#ifndef SRV_H
#define SRV_H

#include <QObject>


namespace SST {

class Host;
class Stream;
class StreamServer;


class WebServer : public QObject
{
	Q_OBJECT

	Host *host;
	StreamServer *srv;

public:
	WebServer(Host *host, quint16 port);

private slots:
	void gotConnection();

	void connRead();
	void connSub();
	void connReset();
};

} // namespace SST

#endif	// SRV_H
