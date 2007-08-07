
#include <netinet/in.h>

#include <QTcpSocket>
#include <QTcpServer>
#include <QtDebug>

#include "stream.h"
#include "util.h"

#include "main.h"
#include "cli.h"	// XXX FLG_
#include "srv.h"
#include "tcp.h"

using namespace SST;


static const int maxChildSize = 4;	// pri change request size


////////// SockServer //////////

SockServer::SockServer(QObject *parent, Stream *strm)
:	QObject(parent), strm(strm)
{
}

void SockServer::gotReadyRead()
{
	while (true) {
		// Get one REQLEN-byte request at a time
		QByteArray req;
		req.resize(REQLEN);
		int act = strm->read(req.data(), REQLEN);
		if (act == 0)
			return;
		Q_ASSERT(act == REQLEN); // not correct, but OK for tests

		// The request is the number of reply bytes the client wants.
		int replen = ntohl(*(quint32*)req.data());
		int pri = ntohl(((quint32*)req.data())[1]);
		int flags = ntohl(((quint32*)req.data())[2]);

		qDebug() << "got request for" << replen << "bytes"
			<< "on socket" << this << "pri" << pri
			<< "flags" << flags;

		strm->setPriority(pri);

		// Form a response buffer of the appropriate size;
		// don't care about the data it contains (just use garbage).
		QByteArray repl;
		repl.resize(replen);

		// Ship it
		act = strm->write(repl);
		Q_ASSERT(act == replen);

		// Self-destruct after sending reply if client asked us to
		if (flags & FLG_CLOSE) {
			//qDebug() << this << "self-destructing";
			return delete(this);
		}
	}
}

void SockServer::gotReset()
{
	qDebug("SockServer disconnected");
	deleteLater();
}

void SockServer::gotSubstream()
{
	Stream *substrm = strm->acceptSubstream();
	if (!substrm)
		return;

	qDebug() << "Got priority change request";
	qint32 buf;
	int act = substrm->read((char*)&buf, 4);
	Q_ASSERT(act == 4);

	int newpri = ntohl(buf);
	strm->setPriority(newpri);

	// We're done with the substream now; SST will close it gracefully.
	delete substrm;

	// Check for more substreams
	return gotSubstream();
}


////////// TestServer //////////

TestServer::TestServer(Host *host, quint16)
:	host(host)
{
	srv = new StreamServer(host, this);
	if (!srv->listen("regress", "SST regression test server",
			"basic", "Basic stream test protocol"))
		qFatal("Can't listen on 'regress' service name");
	connect(srv, SIGNAL(newConnection()),
		this, SLOT(gotConnection()));
}

void TestServer::gotConnection()
{
	Stream *strm = srv->accept();
	if (!strm)
		return;

	SockServer *ss = new SockServer(this, strm);
	connect(strm, SIGNAL(readyRead()),
		ss, SLOT(gotReadyRead()));
	connect(strm, SIGNAL(reset(const QString &)),
		ss, SLOT(gotReset()));
	connect(strm, SIGNAL(newSubstream()),
		ss, SLOT(gotSubstream()), Qt::QueuedConnection);
	strm->setParent(ss);
	strm->setChildReceiveBuffer(maxChildSize);
	strm->listen(Stream::BufLimit);

	// Check for more queued incoming connections
	gotConnection();
}

