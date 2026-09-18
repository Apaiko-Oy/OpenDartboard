#pragma once
// #1305: what a release manifest says, and every reason it might not be one (ADR-0077).
//
// This is the detector's half of a description that exists in two places on purpose. The
// other half is App\Autoscoring\Updates\SignedManifest in Turnaus, which checks the same
// envelope before a deployment will publish it; ADR-0077 says why the two are not
// redundant, and the refusals below are deliberately the same sentences in the same order
// so that a manifest refused here and a manifest refused there are recognisably the same
// refusal. If you change one, read the other.
//
// THE SIGNATURE COVERS BYTES, NOT AN OBJECT. A manifest is
// {"payload": "<base64>", "signature": "<base64>"}, and what is verified is the payload's
// bytes exactly as they arrived. Nothing here re-serialises anything before verifying, so
// there is no canonicalisation rule for a signer and a verifier to drift apart on.
//
// THE ORDER IS LOAD-BEARING. The payload's fields are read only after its signature has
// been verified. Reading them first would be reading an attacker's JSON.
//
// WHAT VERIFIES. ADR-0077 §2: ECDSA P-256 over SHA-256. On Windows through the system's
// own CNG, so the artefact vendors no cryptography and `dumpbin /dependents` names only
// Windows' own DLLs; everywhere else through p256_verify.hpp, which says at length why it
// exists. One anchor spelling serves both: the public key is the uncompressed point's 64
// bytes, X then Y.

#include "sha256.hpp"
#include "p256_verify.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include "../utils/od_platform_first.hpp"
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace update_manifest
{
    /** A trust anchor: 64 raw bytes, the public point's X then Y (ADR-0077 §4). */
    typedef std::string Anchor;

    /** Why a manifest was not read. Every one of these is a different sentence to a tester. */
    enum class Refusal
    {
        None,
        NoAnchor,
        NotJson,
        NotAnEnvelope,
        NotBase64,
        NotVerified,
        PayloadNotJson,
        PayloadNotAnObject,
        FieldMissing,
        SizeMissing,
        DigestMalformed,
        UrlNotHttps,
        WrongChannel,
    };

    struct Read
    {
        bool ok = false;
        Refusal refusal = Refusal::None;
        /** The field, the channel, or the parser's own complaint. Never bytes from the wire. */
        std::string detail;

        std::string channel;
        std::string version;
        std::string url;
        std::string sha256;
        long long size = 0;
        std::string minimum_launcher;
    };

    namespace detail
    {
        inline int base64Value(char c)
        {
            if (c >= 'A' && c <= 'Z')
                return c - 'A';
            if (c >= 'a' && c <= 'z')
                return c - 'a' + 26;
            if (c >= '0' && c <= '9')
                return c - '0' + 52;
            if (c == '+')
                return 62;
            if (c == '/')
                return 63;
            return -1;
        }

        /** Strict: anything that is not base64 is refused rather than skipped. */
        inline bool base64Decode(const std::string &in, std::string &out)
        {
            out.clear();
            int accumulator = 0;
            int bits = 0;
            size_t padding = 0;
            for (size_t i = 0; i < in.size(); i++)
            {
                char c = in[i];
                if (c == '=')
                {
                    padding++;
                    continue;
                }
                if (padding > 0)
                {
                    return false; // data after padding
                }
                int value = base64Value(c);
                if (value < 0)
                {
                    return false;
                }
                accumulator = (accumulator << 6) | value;
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    out += char((accumulator >> bits) & 0xff);
                }
            }
            return padding <= 2 && (in.size() % 4) == 0;
        }

        /**
         * r and s out of a DER SEQUENCE { INTEGER r, INTEGER s }, each left-padded to 32
         * bytes. This is what `openssl dgst -sign` writes and what PHP's `openssl_verify`
         * reads, so it is what is on the wire; CNG wants the two integers raw and
         * concatenated instead, which is what this produces.
         */
        inline bool derSignature(const std::string &der, uint8_t r[32], uint8_t s[32])
        {
            if (der.size() < 8 || uint8_t(der[0]) != 0x30)
            {
                return false;
            }
            uint8_t sequence_length = uint8_t(der[1]);
            size_t i = (sequence_length < 0x80) ? 2 : 2 + size_t(sequence_length & 0x7f);
            uint8_t *into[2] = {r, s};
            for (int which = 0; which < 2; which++)
            {
                if (i + 2 > der.size() || uint8_t(der[i]) != 0x02)
                {
                    return false;
                }
                size_t stated = uint8_t(der[i + 1]);
                if (stated == 0 || stated > 33 || i + 2 + stated > der.size())
                {
                    return false;
                }
                const uint8_t *bytes = reinterpret_cast<const uint8_t *>(der.data()) + i + 2;
                size_t length = stated;
                if (length == 33)
                {
                    // DER writes a leading zero when the high bit would make the integer
                    // negative; the value is still 32 bytes.
                    if (bytes[0] != 0x00)
                    {
                        return false;
                    }
                    bytes++;
                    length = 32;
                }
                for (size_t b = 0; b < 32; b++)
                {
                    into[which][b] = 0;
                }
                for (size_t b = 0; b < length; b++)
                {
                    into[which][32 - length + b] = bytes[b];
                }
                i += 2 + stated;
            }
            return true;
        }

        /** One anchor's verdict on one signature. */
        inline bool verifiedBy(const std::string &payload, const uint8_t r[32], const uint8_t s[32],
                               const Anchor &anchor)
        {
            if (anchor.size() != 64)
            {
                return false;
            }
            uint8_t digest[32];
            od_sha256::digest(payload, digest);
            const uint8_t *key = reinterpret_cast<const uint8_t *>(anchor.data());
#ifdef _WIN32
            // BCRYPT_ECCKEY_BLOB: magic, key length, then X and Y raw -- exactly the 64
            // bytes an anchor already is.
            std::string blob;
            blob.resize(8 + 64);
            uint32_t magic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
            uint32_t length = 32;
            memcpy(&blob[0], &magic, 4);
            memcpy(&blob[4], &length, 4);
            memcpy(&blob[8], key, 64);

            BCRYPT_KEY_HANDLE handle = NULL;
            NTSTATUS status = BCryptImportKeyPair(BCRYPT_ECDSA_P256_ALG_HANDLE, NULL, BCRYPT_ECCPUBLIC_BLOB,
                                                  &handle, (PUCHAR)blob.data(), (ULONG)blob.size(), 0);
            if (status != 0)
            {
                return false;
            }
            uint8_t signature[64];
            memcpy(signature, r, 32);
            memcpy(signature + 32, s, 32);
            status = BCryptVerifySignature(handle, NULL, (PUCHAR)digest, 32, (PUCHAR)signature, 64, 0);
            BCryptDestroyKey(handle);
            return status == 0;
#else
            return p256::verify(digest, r, s, key);
#endif
        }
    }

    inline Read refuse(Refusal why, const std::string &detail = "")
    {
        Read read;
        read.ok = false;
        read.refusal = why;
        read.detail = detail;
        return read;
    }

    /**
     * Read and verify a manifest's bytes, or say which refusal this is.
     *
     * `anchors` is every key this build trusts; any one of them verifying is enough
     * (ADR-0077 §4). An empty list is its own refusal rather than a failed verification,
     * because "nothing here can vouch for anything" and "this was not signed by us" are
     * two different things to be told.
     */
    inline Read read(const std::string &bytes, const std::vector<Anchor> &anchors)
    {
        if (anchors.empty())
        {
            return refuse(Refusal::NoAnchor);
        }

        nlohmann::json envelope = nlohmann::json::parse(bytes, nullptr, /*allow_exceptions*/ false);
        if (envelope.is_discarded())
        {
            return refuse(Refusal::NotJson);
        }
        if (!envelope.is_object() || !envelope.contains("payload") || !envelope["payload"].is_string() ||
            !envelope.contains("signature") || !envelope["signature"].is_string())
        {
            return refuse(Refusal::NotAnEnvelope);
        }

        std::string payload;
        std::string signature;
        if (!detail::base64Decode(envelope["payload"].get<std::string>(), payload) ||
            !detail::base64Decode(envelope["signature"].get<std::string>(), signature))
        {
            return refuse(Refusal::NotBase64);
        }

        uint8_t r[32];
        uint8_t s[32];
        bool verified = false;
        if (detail::derSignature(signature, r, s))
        {
            for (size_t i = 0; i < anchors.size() && !verified; i++)
            {
                verified = detail::verifiedBy(payload, r, s, anchors[i]);
            }
        }
        if (!verified)
        {
            return refuse(Refusal::NotVerified);
        }

        nlohmann::json object = nlohmann::json::parse(payload, nullptr, /*allow_exceptions*/ false);
        if (object.is_discarded())
        {
            return refuse(Refusal::PayloadNotJson);
        }
        if (!object.is_object())
        {
            return refuse(Refusal::PayloadNotAnObject);
        }

        const char *required[5] = {"channel", "version", "url", "sha256", "minimumLauncher"};
        for (int i = 0; i < 5; i++)
        {
            if (!object.contains(required[i]) || !object[required[i]].is_string() ||
                object[required[i]].get<std::string>().empty())
            {
                return refuse(Refusal::FieldMissing, required[i]);
            }
        }
        if (!object.contains("size") || !object["size"].is_number_integer() || object["size"].get<long long>() <= 0)
        {
            return refuse(Refusal::SizeMissing);
        }

        Read out;
        out.channel = object["channel"].get<std::string>();
        out.version = object["version"].get<std::string>();
        out.url = object["url"].get<std::string>();
        out.sha256 = object["sha256"].get<std::string>();
        out.size = object["size"].get<long long>();
        out.minimum_launcher = object["minimumLauncher"].get<std::string>();

        if (out.sha256.size() != 64 || out.sha256.find_first_not_of("0123456789abcdef") != std::string::npos)
        {
            return refuse(Refusal::DigestMalformed);
        }
        if (out.url.rfind("https://", 0) != 0)
        {
            // ADR-0077 §6: the URL is untrusted data, because the digest and the signature
            // are the trust. It still may not be plaintext -- a board that fetches over
            // http learns nothing new, but anybody watching it learns which club runs
            // which version.
            return refuse(Refusal::UrlNotHttps);
        }

        out.ok = true;
        return out;
    }

    /**
     * The same read, then the one check only the asker can make: that the signer signed
     * this for the channel that was asked for. Without it a stable manifest served at the
     * beta address is a perfectly valid signed document and every beta board quietly
     * leaves the channel it was put on.
     */
    inline Read readForChannel(const std::string &bytes, const std::vector<Anchor> &anchors,
                               const std::string &channel)
    {
        Read result = read(bytes, anchors);
        if (!result.ok)
        {
            return result;
        }
        if (result.channel != channel)
        {
            return refuse(Refusal::WrongChannel, result.channel);
        }
        return result;
    }

    /** 128 hexadecimal characters to one anchor's 64 bytes; empty when it is not that. */
    inline Anchor anchorFromHex(const std::string &hex)
    {
        if (hex.size() != 128)
        {
            return "";
        }
        std::string out;
        out.reserve(64);
        for (size_t i = 0; i < 128; i++)
        {
            char c = hex[i];
            int value = (c >= '0' && c <= '9')   ? c - '0'
                        : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                        : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                                 : -1;
            if (value < 0)
            {
                return "";
            }
            if (i % 2 == 0)
            {
                out += char(value << 4);
            }
            else
            {
                out[out.size() - 1] = char(out[out.size() - 1] | value);
            }
        }
        return out;
    }
}
