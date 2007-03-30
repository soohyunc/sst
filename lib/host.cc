
#include "host.h"

using namespace SST;


Host::Host()
:	RegHostState(this)
{
}

Host::Host(QSettings *settings, quint16 defaultport)
:	RegHostState(this)
{
	initSocket(settings, defaultport);
	initHostIdent(settings);
}

Host *Host::host()
{
	return this;
}

