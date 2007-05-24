
#include <QFile>
#include <QtDebug>

#include "stream.h"
#include "util.h"

#include "srv.h"

using namespace SST;


////////// WebImage //////////



////////// WebServer //////////

WebServer::WebServer(Host *host, quint16)
:	host(host)
{
	srv = new StreamServer(host, this);
	if (!srv->listen("webtest", "SST prioritized web browser demo",
			"basic", "Basic web request protocol"))
		qFatal("Can't listen on 'webtest' service name");
	connect(srv, SIGNAL(newConnection()),
		this, SLOT(gotConnection()));
}

void WebServer::gotConnection()
{
	Stream *strm = srv->accept();
	if (!strm)
		return;

	strm->listen();
	strm->setParent(this);
	connect(strm, SIGNAL(readyReadMessage()),
		this, SLOT(connRead()));
	connect(strm, SIGNAL(newSubstream()),
		this, SLOT(connSub()));
	connect(strm, SIGNAL(reset(const QString &)),
		this, SLOT(connReset()));

	// Check for more queued incoming connections
	gotConnection();
}

void WebServer::connRead()
{
	Stream *strm = (Stream*)sender();
	QByteArray msg = strm->readMessage();
	if (msg.isEmpty())
		return;

	// Interpret the message as a filename to retrieve within the page
	QString name = "page/" + QString::fromAscii(msg);
	QFile f(name);
	if (!f.open(QIODevice::ReadOnly)) {
		bad:
		qDebug() << "Can't open requested object " << name;
		strm->shutdown(strm->Reset);	// Kinda severe, but...
		strm->deleteLater();
		return;
	}

	QByteArray dat = f.readAll();
	if (dat.isEmpty())
		goto bad;

	strm->writeMessage(dat);

	// Go back and read more messages if available
	connRead();
}

void WebServer::connSub()
{
	Stream *strm = (Stream*)sender();

	while (true) {
		Stream *sub = strm->acceptSubstream();
		if (!sub)
			return;

		qDebug() << "Got priority change request";
		qint32 buf;
		int act = sub->read((char*)&buf, 4);
		if (act != 4) {
			qWarning("bad priority change req len %d", act);
			delete sub;
			continue;
		}

		int newpri = ntohl(buf);
		strm->setPriority(newpri);

		// We're done with the substream now;
		// SST will close it gracefully.
		delete sub;
	}
}

void WebServer::connReset()
{
	Stream *strm = (Stream*)sender();
	qDebug() << strm << "reset by client";
	strm->deleteLater();
}

