#ifndef MAIN_H
#define MAIN_H

class QHostAddress;

namespace SST {

class Host;


#define REQLEN		128


extern QHostAddress cliaddr;
extern QHostAddress srvaddr;

extern bool success;
extern int nerrors;

#define oops(args) (nerrors++, (void)qWarning args)

#define check(exp) ((exp) ? (void)0 : \
	(void)oops(("Failed check '%s' at %s:%d", #exp, __FILE__, __LINE__)))


} // namespace SST

#endif	// MAIN_H
