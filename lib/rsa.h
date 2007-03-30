#ifndef SST_RSA_H
#define SST_RSA_H

#include <openssl/rsa.h>

#include "sign.h"

namespace SST {

class RSAKey : public SignKey
{
	RSA *rsa;

	RSAKey(RSA *rsa);

public:
	RSAKey(const QByteArray &key);
	RSAKey(int bits = 0, unsigned e = 65537);
	~RSAKey();

	QByteArray id() const;
	QByteArray key(bool getPrivateKey = false) const;

	SecureHash *newHash(QObject *parent = NULL) const;

	QByteArray sign(const QByteArray &digest) const;
	bool verify(const QByteArray &digest, const QByteArray &sig) const;

private:
	void dump() const;
};

} // namespace SST

#endif	// SST_RSA_H
