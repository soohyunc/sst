
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <QSocketNotifier>
#include <QCoreApplication>

#include "xdr.h"

#include "cli.h"

using namespace SST;


ShellClient::ShellClient(Host *host, QObject *parent)
:	QObject(parent),
	strm(host),
	shs(&strm)
{
	connect(&afin, SIGNAL(readyRead()), this, SLOT(inReady()));
	connect(&strm, SIGNAL(bytesWritten(qint64)), this, SLOT(inReady()));

	connect(&strm, SIGNAL(readyRead()), this, SLOT(outReady()));
	connect(&afout, SIGNAL(bytesWritten(qint64)), this, SLOT(outReady()));
}

void ShellClient::setupTerminal(int fd)
{
	// Get current terminal name
	QString termname(getenv("TERM"));

	// Get current terminal settings
	struct termios tios;
	if (tcgetattr(fd, &tios) < 0)
		qFatal("Can't get terminal settings: %s", strerror(errno));

	// Get current window size
	struct winsize ws;
	memset(&ws, 0, sizeof(ws));
	if (ioctl(fd, TIOCGWINSZ, &ws) < 0)
		qWarning("Can't get terminal window size: %s",
			strerror(errno));

	// Build the pseudo-tty parameter control message
	QByteArray msg;
	XdrStream wxs(&msg, QIODevice::WriteOnly);
	wxs << (quint32)Terminal
		<< termname
		<< (quint32)ws.ws_col << (quint32)ws.ws_row
		<< (quint32)ws.ws_xpixel << (quint32)ws.ws_ypixel;
	termpack(wxs, tios);

	// Send it
	shs.sendControl(msg);

	// Turn off terminal input processing
	tios.c_lflag &= ~(ICANON | ECHO);
	if (tcsetattr(fd, TCSAFLUSH, &tios) < 0)
		qFatal("Can't set terminal settings: %s", strerror(errno));
}

void ShellClient::runShell(const QString &cmd, int infd, int outfd)
{
	if (!afin.open(infd, afin.ReadOnly))
		qFatal("Error setting up input forwarding: %s",
			afin.errorString().toLocal8Bit().data());

	if (!afout.open(outfd, afout.WriteOnly))
		qFatal("Error setting up output forwarding: %s",
			afout.errorString().toLocal8Bit().data());

	// Build the message to start the shell or command
	QByteArray msg;
	XdrStream wxs(&msg, QIODevice::WriteOnly);
	if (cmd.isEmpty())
		wxs << (quint32)Shell;
	else
		wxs << (quint32)Exec << cmd;

	// Send it
	shs.sendControl(msg);
}

void ShellClient::inReady()
{
	//qDebug() << this << "inReady";
	while (true) {
		// XX if (shs.bytesToWrite() >= shellBufferSize) return;

		char buf[4096];
		int act = afin.read(buf, sizeof(buf));
		//qDebug() << this << "got:" << QByteArray(buf, act);
		if (act < 0)
			qFatal("Error reading input for remote shell: %s",
				afin.errorString().toLocal8Bit().data());
		if (act == 0) {
			if (afin.atEnd()) {
				qDebug() << "End of remote shell stream";
				QCoreApplication::exit(0);
			}
			return;
		}
		shs.sendData(buf, act);
	}
}

void ShellClient::outReady()
{
	//qDebug() << this << "outReady";
	while (true) {
		if (afout.bytesToWrite() >= shellBufferSize)
			return;	// Wait until the write buffer empties a bit

		ShellStream::Packet pkt = shs.receive();
		switch (pkt.type) {
		case ShellStream::Null:
			return;	// Nothing more to receive for now
		case ShellStream::Data:
			if (afout.write(pkt.data) < 0)
				qFatal("Error writing remote shell output: %s",
					afout.errorString()
						.toLocal8Bit().data());
			break;
		case ShellStream::Control:
			gotControl(pkt.data);
			break;
		}
	}
}

void ShellClient::gotControl(const QByteArray &msg)
{
	qDebug() << "XXX got control message size" << msg.size();
}

