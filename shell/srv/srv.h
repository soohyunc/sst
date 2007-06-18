#ifndef SHELL_H
#define SHELL_H

#include "stream.h"
#include "../proto.h"
#include "../asyncfile.h"

class ShellSession : public QObject, ShellProtocol
{
	Q_OBJECT

private:
	ShellStream shs;
	int ptyfd, ttyfd;
	AsyncFile aftty;
	QString termname;	// Name for TERM environment variable 
public:
	ShellSession(SST::Stream *strm, QObject *parent = NULL);
	~ShellSession();

private:
	void gotControl(const QByteArray &msg);

	void openPty(SST::XdrStream &rxs);
	void runShell(SST::XdrStream &rxs);
	void doExec(SST::XdrStream &rxs);

	void run(const QString &cmd = QString());

	// Send an error message and reset the stream
	void error(const QString &str);

private slots:
	void inReady();
	void outReady();
};

class ShellServer : public QObject, public ShellProtocol
{
	Q_OBJECT

private:
	SST::StreamServer srv;

public:
	ShellServer(SST::Host *host, QObject *parent = NULL);

private slots:
	void gotConnection();
};

#endif	// SHELL_H
