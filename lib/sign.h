//
// SignKey: abstract base class for public-key cryptographic signing methods
//
#ifndef SST_SIGN_H
#define SST_SIGN_H

#include <QString>

class QObject;
class QByteArray;

namespace SST {

class SecureHash;

class SignKey
{
public:
	enum Type {
		Invalid = 0,		// Uninitialized or invalid key
		Public = 1,		// Public key only
		Private = 2		// Private key
	};

private:
	Type t;

public:
	// Get the type of this key
	inline Type type() const { return t; }

	// Get the short hash ID of this public or private key,
	// which is usable only for uniquely identifying the key.
	virtual QByteArray id() const = 0;

	// Get this Ident's binary-encoded public or private key.
	virtual QByteArray key(bool getPrivateKey = false) const = 0;

	// Create a new SecureHash object suitable for hashing messages
	// to be signed using this identity's private key.
	virtual SecureHash *newHash(QObject *parent = 0) const = 0;

	// Generate or verify a signature
	virtual QByteArray sign(const QByteArray &digest) const = 0;
	virtual bool verify(const QByteArray &digest,
				const QByteArray &sig) const = 0;

	virtual ~SignKey();

protected:
	SignKey();

	inline void setType(Type t) { this->t = t; }
};

} // namespace SST

#endif	// SST_SIGN_H
