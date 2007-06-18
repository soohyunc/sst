#ifndef SHELL_CLI_H
#define SHELL_CLI_H

#include "stream.h"

#include "../proto.h"
#include "../asyncfile.h"

namespace SST {
	class Host;
	class Endpoint;
};

class ShellClient : public QObject, public ShellProtocol
{
	Q_OBJECT

private:
	SST::Stream strm;
	ShellStream shs;
	AsyncFile afin, afout;

public:
	ShellClient(SST::Host *host, QObject *parent = NULL);

	inline void connectTo(const QByteArray &dsteid,
			const QList<SST::Endpoint> &dsteps
				= QList<SST::Endpoint>()) {
		Q_ASSERT(!strm.isConnected());
		strm.connectTo(dsteid, serviceName, protocolName, dsteps);
	}

	void setupTerminal(int fd);
	void runShell(const QString &cmd, int infd, int outfd);

private:
	void gotControl(const QByteArray &msg);

private slots:
	void inReady();
	void outReady();
};

#endif	// SHELL_CLI_H
