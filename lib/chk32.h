#ifndef SST_CHK32_H
#define SST_CHK32_H

#include <stdint.h>

namespace SST {


// Default initialization value for unkeyed use: b = 0, a = 1
//#define CHK32_INIT	((uint32_t)0x00000001)


class Chk32
{
private:
	// Counter initialization values
	static const uint32_t InitA = 0;
	static const uint32_t InitB = 0;

	// Prime modulus for our wraparound checksum arithmetic
	static const int Modulus = 65537;

	// Number of words we can process before our 64-bit counters overflow.
	// Actually 23726746, but just to be safe...
	static const int MaxRun = 23726740;

	uint64_t a;
	uint64_t b;
	int run;
	union {
		uint8_t b[2];
		uint16_t w;
	} oddbuf;
	bool haveodd;

public:
	inline Chk32() : a(InitA), b(InitB), run(MaxRun), haveodd(false) { }

	// Primitive init/update/final API for 
	inline void init16()
		{ a = InitA; b = InitB; run = MaxRun; haveodd = false; }
	void update16(const uint16_t *buf, int nwords);
	uint32_t final16();

	// Standard init/update/final API for byte streams
	inline void init() { init16(); }
	void update(const void *buf, size_t size);
	uint32_t final();

	// Compute a checksum over a single data block
	static inline uint32_t sum(const void *buf, size_t size) {
		Chk32 c; c.update(buf, size); return c.final(); }
};


} // namespace SST

#endif	// SST_CHK32_H
