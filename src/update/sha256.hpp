#pragma once
// #1305: SHA-256, because the signature over a manifest is a signature over its digest.
//
// One implementation for both platforms, deliberately. ADR-0077 §2 says the primitive is
// ECDSA P-256 through Windows' own CNG, and CNG's BCryptVerifySignature is handed a HASH
// rather than a message -- so the hashing step is the same code on Windows and on Linux
// whichever way the signature is checked, and there is one place a digest can be wrong.
//
// FIPS 180-4. Nothing here is a secret: the input is a manifest anybody can fetch.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

namespace od_sha256
{
    struct Context
    {
        uint32_t state[8];
        uint64_t bits;
        uint8_t buffer[64];
        size_t held;
    };

    namespace detail
    {
        inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

        inline const uint32_t *roundConstants()
        {
            static const uint32_t k[64] = {
                0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
                0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
                0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
                0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
                0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
                0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
                0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
                0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
            return k;
        }

        inline void block(Context &c, const uint8_t *p)
        {
            const uint32_t *k = roundConstants();
            uint32_t w[64];
            for (int i = 0; i < 16; i++)
            {
                w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) |
                       uint32_t(p[i * 4 + 3]);
            }
            for (int i = 16; i < 64; i++)
            {
                uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
                uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            uint32_t a = c.state[0], b = c.state[1], cc = c.state[2], d = c.state[3];
            uint32_t e = c.state[4], f = c.state[5], g = c.state[6], h = c.state[7];
            for (int i = 0; i < 64; i++)
            {
                uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
                uint32_t ch = (e & f) ^ (~e & g);
                uint32_t t1 = h + S1 + ch + k[i] + w[i];
                uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
                uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
                uint32_t t2 = S0 + maj;
                h = g;
                g = f;
                f = e;
                e = d + t1;
                d = cc;
                cc = b;
                b = a;
                a = t1 + t2;
            }
            c.state[0] += a;
            c.state[1] += b;
            c.state[2] += cc;
            c.state[3] += d;
            c.state[4] += e;
            c.state[5] += f;
            c.state[6] += g;
            c.state[7] += h;
        }
    }

    inline void begin(Context &c)
    {
        c.state[0] = 0x6a09e667u;
        c.state[1] = 0xbb67ae85u;
        c.state[2] = 0x3c6ef372u;
        c.state[3] = 0xa54ff53au;
        c.state[4] = 0x510e527fu;
        c.state[5] = 0x9b05688cu;
        c.state[6] = 0x1f83d9abu;
        c.state[7] = 0x5be0cd19u;
        c.bits = 0;
        c.held = 0;
    }

    inline void feed(Context &c, const uint8_t *data, size_t length)
    {
        c.bits += uint64_t(length) * 8;
        while (length > 0)
        {
            size_t room = 64 - c.held;
            size_t take = length < room ? length : room;
            std::memcpy(c.buffer + c.held, data, take);
            c.held += take;
            data += take;
            length -= take;
            if (c.held == 64)
            {
                detail::block(c, c.buffer);
                c.held = 0;
            }
        }
    }

    inline void finish(Context &c, uint8_t out[32])
    {
        uint64_t bits = c.bits;
        uint8_t one = 0x80;
        feed(c, &one, 1);
        uint8_t zero = 0;
        while (c.held != 56)
        {
            feed(c, &zero, 1);
        }
        uint8_t tail[8];
        for (int i = 0; i < 8; i++)
        {
            tail[i] = uint8_t((bits >> (56 - i * 8)) & 0xff);
        }
        c.bits = bits; // feed() below must not move the length that is being written
        std::memcpy(c.buffer + c.held, tail, 8);
        detail::block(c, c.buffer);
        c.held = 0;
        for (int i = 0; i < 8; i++)
        {
            out[i * 4] = uint8_t(c.state[i] >> 24);
            out[i * 4 + 1] = uint8_t(c.state[i] >> 16);
            out[i * 4 + 2] = uint8_t(c.state[i] >> 8);
            out[i * 4 + 3] = uint8_t(c.state[i]);
        }
    }

    /** The digest of one string, as 32 raw bytes. */
    inline void digest(const std::string &message, uint8_t out[32])
    {
        Context c;
        begin(c);
        feed(c, reinterpret_cast<const uint8_t *>(message.data()), message.size());
        finish(c, out);
    }

    /** The digest of one string, as 64 lowercase hexadecimal characters. */
    inline std::string hex(const std::string &message)
    {
        uint8_t raw[32];
        digest(message, raw);
        static const char *digits = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (int i = 0; i < 32; i++)
        {
            out += digits[raw[i] >> 4];
            out += digits[raw[i] & 0x0f];
        }
        return out;
    }
}
