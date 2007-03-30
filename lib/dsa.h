#ifndef SST_DSA_H
#define SST_DSA_H

#include <openssl/dsa.h>

#include "sign.h"

namespace SST {

class DSAKey : public SignKey
{
	DSA *dsa;

	DSAKey(DSA *dsa);

public:
	DSAKey(const QByteArray &key);
	DSAKey(int bits);
	~DSAKey();

	QByteArray id() const;
	QByteArray key(bool getPrivateKey = false) const;

	SecureHash *newHash(QObject *parent = NULL) const;

	QByteArray sign(const QByteArray &digest) const;
	bool verify(const QByteArray &digest, const QByteArray &sig) const;
};

} // namespace SST

#endif	// SST_DSA_H
