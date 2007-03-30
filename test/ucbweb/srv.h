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
	QIODevice *dev;

public:
	SockServer(QObject *parent, QIODevice *dev);

private slots:
	void gotReadyRead();
	void gotDisconnected();
	void gotSubstream();
};


class TestServer : public QObject
{
	Q_OBJECT

	Host *host;

	StreamServer *sstsrv;		// Userland SST server
	TcpServer *tcpsrv;		// Userland TCP server
	QTcpServer *natsrv;		// Native TCP server socket
	QUdpSocket *udpsock;		// Native UDP socket

public:
	TestServer(Host *host, quint16 port);

private slots:
	void sstConnection();
	void tcpConnection(TcpStream *strm);
	void natPending();
	void udpReadyRead();
};

} // namespace SST

#endif	// SRV_H
