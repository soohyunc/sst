#ifndef SRV_H
#define SRV_H

#include <QObject>

class QIODevice;
class QTcpServer;
class QUdpSocket;


namespace SST {

class Stream;
class StreamServer;
class TcpStream;
class TcpServer;
class Host;


// An instance of this class serves one client socket.
class SockServer : public QObject
{
	Q_OBJECT

private:
	Stream *strm;

public:
	SockServer(QObject *parent, Stream *strm);

private slots:
	void gotReadyRead();
	void gotReset();
	void gotSubstream();
};


class TestServer : public QObject
{
	Q_OBJECT

	Host *host;
	StreamServer *srv;		// Userland SST server

public:
	TestServer(Host *host, quint16 port);

private slots:
	void gotConnection();
};

} // namespace SST

#endif	// SRV_H
