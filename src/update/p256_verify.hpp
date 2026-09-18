#pragma once
// #1305: ECDSA P-256 signature verification, for the builds that have no CNG.
//
// WHY THIS EXISTS AT ALL, because it is the part of this slice a reviewer should read
// hardest. ADR-0077 §2 chose ECDSA P-256 through Windows' own `BCryptVerifySignature`
// precisely so that the Windows artefact carries no vendored cryptography and
// `dumpbin /dependents` goes on naming only Windows' own DLLs. That decision stands and
// `manifest.hpp` honours it: on Windows this file is not compiled at all.
//
// It exists because a refusal nothing can run is an argument rather than a measurement.
// #1305's one load-bearing criterion is that a manifest with one byte changed is refused
// in a test that goes red when the verification is deleted, and the only machine this
// repository's checks run on -- the od-amd64:bullseye container, and CI -- is Linux with
// no OpenSSL headers (http_transport.hpp says so for the same reason). Verification here
// is therefore the Linux arm of the same seam capture.hpp and http_transport.hpp already
// are: one policy above, two implementations below.
//
// WHAT IT IS SAFE TO WRITE BY HAND AND WHAT IS NOT. Everything this touches is public:
// a published manifest, a published signature, a compiled-in public key. There is no
// secret here to leak through a timing channel, and nothing in this file ever signs. A
// key generation or a signing routine would be a different proposition and there is
// none. The failure mode that would matter is the opposite one -- accepting a signature
// that is not valid -- and that is what testers/i1305_update_check.cpp measures, against
// signatures produced by OpenSSL rather than by anything in this tree.
//
// HOW IT IS WRITTEN. Modular multiplication is double-and-add over 256 iterations rather
// than Montgomery or a Solinas reduction: the fast forms are where this kind of code goes
// silently wrong, and one whole verification costs a fraction of a second on a machine
// that is about to spend two minutes deciding whether to download 38 MB. Field inversion
// is Fermat, so no extended Euclid to get wrong either.

#include <cstdint>
#include <cstddef>
#include <cstring>

#ifndef _WIN32

namespace p256
{
    typedef uint64_t u64;

    /** A 256-bit unsigned integer, little-endian limbs. */
    struct U256
    {
        u64 v[4];
    };

    inline U256 zero()
    {
        U256 r;
        r.v[0] = r.v[1] = r.v[2] = r.v[3] = 0;
        return r;
    }

    inline bool isZero(const U256 &a) { return (a.v[0] | a.v[1] | a.v[2] | a.v[3]) == 0; }

    inline bool equal(const U256 &a, const U256 &b)
    {
        return a.v[0] == b.v[0] && a.v[1] == b.v[1] && a.v[2] == b.v[2] && a.v[3] == b.v[3];
    }

    /** -1, 0 or 1. */
    inline int compare(const U256 &a, const U256 &b)
    {
        for (int i = 3; i >= 0; i--)
        {
            if (a.v[i] < b.v[i])
            {
                return -1;
            }
            if (a.v[i] > b.v[i])
            {
                return 1;
            }
        }
        return 0;
    }

    /** a + b, with the carry out. */
    inline u64 addRaw(const U256 &a, const U256 &b, U256 &out)
    {
        u64 carry = 0;
        for (int i = 0; i < 4; i++)
        {
            u64 sum = a.v[i] + b.v[i];
            u64 first = (sum < a.v[i]) ? 1u : 0u;
            u64 total = sum + carry;
            u64 second = (total < sum) ? 1u : 0u;
            out.v[i] = total;
            carry = first | second;
        }
        return carry;
    }

    /** a - b, with the borrow out. */
    inline u64 subRaw(const U256 &a, const U256 &b, U256 &out)
    {
        u64 borrow = 0;
        for (int i = 0; i < 4; i++)
        {
            u64 left = a.v[i];
            u64 right = b.v[i];
            u64 next = (left < right || (left == right && borrow)) ? 1u : 0u;
            out.v[i] = left - right - borrow;
            borrow = next;
        }
        return borrow;
    }

    /** (a + b) mod m, for a, b < m. */
    inline U256 addMod(const U256 &a, const U256 &b, const U256 &m)
    {
        U256 r;
        u64 carry = addRaw(a, b, r);
        if (carry || compare(r, m) >= 0)
        {
            U256 t;
            subRaw(r, m, t);
            return t;
        }
        return r;
    }

    /** (a - b) mod m, for a, b < m. */
    inline U256 subMod(const U256 &a, const U256 &b, const U256 &m)
    {
        U256 r;
        u64 borrow = subRaw(a, b, r);
        if (borrow)
        {
            U256 t;
            addRaw(r, m, t);
            return t;
        }
        return r;
    }

    inline int bitAt(const U256 &a, int index) { return int((a.v[index / 64] >> (index % 64)) & 1u); }

    /** (a * b) mod m, double-and-add. b must be < m; a may be anything. */
    inline U256 mulMod(const U256 &a, const U256 &b, const U256 &m)
    {
        U256 result = zero();
        for (int i = 255; i >= 0; i--)
        {
            result = addMod(result, result, m);
            if (bitAt(a, i))
            {
                result = addMod(result, b, m);
            }
        }
        return result;
    }

    /** a^e mod m, square and multiply. */
    inline U256 powMod(const U256 &a, const U256 &e, const U256 &m)
    {
        U256 result = zero();
        result.v[0] = 1;
        for (int i = 255; i >= 0; i--)
        {
            result = mulMod(result, result, m);
            if (bitAt(e, i))
            {
                result = mulMod(result, a, m);
            }
        }
        return result;
    }

    /** a^-1 mod m for prime m, by Fermat. Zero in, zero out. */
    inline U256 invMod(const U256 &a, const U256 &m)
    {
        U256 two = zero();
        two.v[0] = 2;
        U256 e;
        subRaw(m, two, e);
        return powMod(a, e, m);
    }

    // ------------------------------------------------------------------ the curve

    inline U256 fromLimbs(u64 l0, u64 l1, u64 l2, u64 l3)
    {
        U256 r;
        r.v[0] = l0;
        r.v[1] = l1;
        r.v[2] = l2;
        r.v[3] = l3;
        return r;
    }

    /** p = 2^256 - 2^224 + 2^192 + 2^96 - 1 */
    inline U256 fieldModulus()
    {
        return fromLimbs(0xFFFFFFFFFFFFFFFFull, 0x00000000FFFFFFFFull, 0x0000000000000000ull, 0xFFFFFFFF00000001ull);
    }

    /** n, the order of G. */
    inline U256 groupOrder()
    {
        return fromLimbs(0xF3B9CAC2FC632551ull, 0xBCE6FAADA7179E84ull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFF00000000ull);
    }

    inline U256 curveB()
    {
        return fromLimbs(0x3BCE3C3E27D2604Bull, 0x651D06B0CC53B0F6ull, 0xB3EBBD55769886BCull, 0x5AC635D8AA3A93E7ull);
    }

    inline U256 generatorX()
    {
        return fromLimbs(0xF4A13945D898C296ull, 0x77037D812DEB33A0ull, 0xF8BCE6E563A440F2ull, 0x6B17D1F2E12C4247ull);
    }

    inline U256 generatorY()
    {
        return fromLimbs(0xCBB6406837BF51F5ull, 0x2BCE33576B315ECEull, 0x8EE7EB4A7C0F9E16ull, 0x4FE342E2FE1A7F9Bull);
    }

    /** A point in Jacobian coordinates; z == 0 is the point at infinity. */
    struct Point
    {
        U256 x, y, z;
    };

    inline Point infinity()
    {
        Point p;
        p.x = zero();
        p.x.v[0] = 1;
        p.y = zero();
        p.y.v[0] = 1;
        p.z = zero();
        return p;
    }

    inline bool atInfinity(const Point &p) { return isZero(p.z); }

    inline Point doublePoint(const Point &a, const U256 &m)
    {
        if (atInfinity(a))
        {
            return a;
        }
        U256 delta = mulMod(a.z, a.z, m);
        U256 gamma = mulMod(a.y, a.y, m);
        U256 beta = mulMod(a.x, gamma, m);
        U256 t1 = subMod(a.x, delta, m);
        U256 t2 = addMod(a.x, delta, m);
        U256 alpha = mulMod(t1, t2, m);
        alpha = addMod(alpha, addMod(alpha, alpha, m), m); // 3 * (x - delta) * (x + delta)

        U256 fourBeta = addMod(beta, beta, m);
        fourBeta = addMod(fourBeta, fourBeta, m);
        U256 eightBeta = addMod(fourBeta, fourBeta, m);

        Point out;
        out.x = subMod(mulMod(alpha, alpha, m), eightBeta, m);
        U256 yz = addMod(a.y, a.z, m);
        out.z = subMod(subMod(mulMod(yz, yz, m), gamma, m), delta, m);
        U256 gammaSquared = mulMod(gamma, gamma, m);
        U256 eightGammaSquared = addMod(gammaSquared, gammaSquared, m);
        eightGammaSquared = addMod(eightGammaSquared, eightGammaSquared, m);
        eightGammaSquared = addMod(eightGammaSquared, eightGammaSquared, m);
        out.y = subMod(mulMod(alpha, subMod(fourBeta, out.x, m), m), eightGammaSquared, m);
        return out;
    }

    inline Point addPoints(const Point &a, const Point &b, const U256 &m)
    {
        if (atInfinity(a))
        {
            return b;
        }
        if (atInfinity(b))
        {
            return a;
        }
        U256 z1z1 = mulMod(a.z, a.z, m);
        U256 z2z2 = mulMod(b.z, b.z, m);
        U256 u1 = mulMod(a.x, z2z2, m);
        U256 u2 = mulMod(b.x, z1z1, m);
        U256 s1 = mulMod(a.y, mulMod(b.z, z2z2, m), m);
        U256 s2 = mulMod(b.y, mulMod(a.z, z1z1, m), m);
        if (equal(u1, u2))
        {
            if (!equal(s1, s2))
            {
                return infinity();
            }
            return doublePoint(a, m);
        }
        U256 h = subMod(u2, u1, m);
        U256 twoH = addMod(h, h, m);
        U256 i = mulMod(twoH, twoH, m);
        U256 j = mulMod(h, i, m);
        U256 r = addMod(subMod(s2, s1, m), subMod(s2, s1, m), m);
        U256 v = mulMod(u1, i, m);

        Point out;
        out.x = subMod(subMod(mulMod(r, r, m), j, m), addMod(v, v, m), m);
        U256 s1j = mulMod(s1, j, m);
        out.y = subMod(mulMod(r, subMod(v, out.x, m), m), addMod(s1j, s1j, m), m);
        U256 zz = addMod(a.z, b.z, m);
        out.z = mulMod(subMod(subMod(mulMod(zz, zz, m), z1z1, m), z2z2, m), h, m);
        return out;
    }

    inline Point multiply(const U256 &k, const Point &g, const U256 &m)
    {
        Point result = infinity();
        for (int i = 255; i >= 0; i--)
        {
            result = doublePoint(result, m);
            if (bitAt(k, i))
            {
                result = addPoints(result, g, m);
            }
        }
        return result;
    }

    /** 32 big-endian bytes to a U256. */
    inline U256 fromBytes(const uint8_t *bytes)
    {
        U256 r = zero();
        for (int i = 0; i < 32; i++)
        {
            r.v[3 - i / 8] = (r.v[3 - i / 8] << 8) | u64(bytes[i]);
        }
        return r;
    }

    /** True when (x, y) is on the curve: y^2 == x^3 - 3x + b (mod p). */
    inline bool onCurve(const U256 &x, const U256 &y)
    {
        const U256 m = fieldModulus();
        if (compare(x, m) >= 0 || compare(y, m) >= 0)
        {
            return false;
        }
        U256 left = mulMod(y, y, m);
        U256 right = mulMod(mulMod(x, x, m), x, m);
        U256 threeX = addMod(x, addMod(x, x, m), m);
        right = subMod(right, threeX, m);
        right = addMod(right, curveB(), m);
        return equal(left, right);
    }

    /**
     * True when (r, s) is this public key's signature over this 32-byte digest.
     *
     * `publicKey` is the uncompressed point's 64 bytes, X then Y, big-endian apiece --
     * which is what a CNG BCRYPT_ECCKEY_BLOB carries after its header, so one anchor
     * spelling serves both arms of the seam.
     */
    inline bool verify(const uint8_t digest[32], const uint8_t r[32], const uint8_t s[32], const uint8_t publicKey[64])
    {
        const U256 p = fieldModulus();
        const U256 n = groupOrder();

        U256 qx = fromBytes(publicKey);
        U256 qy = fromBytes(publicKey + 32);
        if (isZero(qx) && isZero(qy))
        {
            return false;
        }
        if (!onCurve(qx, qy))
        {
            return false;
        }

        U256 rr = fromBytes(r);
        U256 ss = fromBytes(s);
        if (isZero(rr) || isZero(ss) || compare(rr, n) >= 0 || compare(ss, n) >= 0)
        {
            return false;
        }

        U256 e = fromBytes(digest);
        while (compare(e, n) >= 0)
        {
            U256 reduced;
            subRaw(e, n, reduced);
            e = reduced;
        }

        U256 w = invMod(ss, n);
        U256 u1 = mulMod(e, w, n);
        U256 u2 = mulMod(rr, w, n);

        Point g;
        g.x = generatorX();
        g.y = generatorY();
        g.z = zero();
        g.z.v[0] = 1;

        Point q;
        q.x = qx;
        q.y = qy;
        q.z = zero();
        q.z.v[0] = 1;

        Point sum = addPoints(multiply(u1, g, p), multiply(u2, q, p), p);
        if (atInfinity(sum))
        {
            return false;
        }

        U256 zInverse = invMod(mulMod(sum.z, sum.z, p), p);
        U256 x = mulMod(sum.x, zInverse, p);
        while (compare(x, n) >= 0)
        {
            U256 reduced;
            subRaw(x, n, reduced);
            x = reduced;
        }
        return equal(x, rr);
    }
}

#endif // !_WIN32
