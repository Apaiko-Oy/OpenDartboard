#pragma once
// #1305: the keys this build will accept a manifest from (ADR-0077 §4).
//
// Two anchors, current and next, compiled in: a rotation is then three ordinary releases
// -- ship a build trusting both, start signing with the new key, ship a build trusting
// only the new one. If both are ever lost, no update can be delivered over the wire again
// and the recovery is a download by hand. The ADR says so rather than leaving it to be
// found out.
//
// BOTH ARE EMPTY TODAY, AND THAT IS NOT AN OVERSIGHT. #1299 mints the first signing key
// in the fork's release workflow; until its public half is written in here, every manifest
// is refused with NoAnchor -- "nothing here can vouch for anything" -- which is a
// different sentence from "this was not signed by us" and is said as one. That is the same
// answer PublishedManifests gives a deployment with no key in config/autoscoring.php, and
// it is the safe direction to fail in: a board with no anchor keeps the version it has.
//
// An anchor is 128 hexadecimal characters: the public point uncompressed, X then Y. Take
// it from the signing key's public half with
//
//     openssl ec -pubin -in public.pem -text -noout
//
// and drop the leading 04 from the `pub:` block.
//
// There is deliberately no way to name an anchor at runtime -- no flag, no environment
// variable, no file beside the .exe. An update the machine's own operator can point at
// their own key is not an update anybody has to sign.

#include "manifest.hpp"

#include <string>
#include <vector>

// The two may also be given at build time -- -DOD_UPDATE_ANCHOR_CURRENT="<128 hex>" -- which
// is still compiled in, and is how a release workflow holding the public half as a CI
// variable writes it without editing a source file. There is no runtime equivalent and
// there must never be one.
#ifndef OD_UPDATE_ANCHOR_CURRENT
#define OD_UPDATE_ANCHOR_CURRENT ""
#endif
#ifndef OD_UPDATE_ANCHOR_NEXT
#define OD_UPDATE_ANCHOR_NEXT ""
#endif

namespace update_keys
{
    inline const char *kCurrent = OD_UPDATE_ANCHOR_CURRENT;
    inline const char *kNext = OD_UPDATE_ANCHOR_NEXT;

    /** Every anchor this build trusts, malformed ones left out rather than accepted. */
    inline std::vector<update_manifest::Anchor> anchors()
    {
        std::vector<update_manifest::Anchor> out;
        const char *written[2] = {kCurrent, kNext};
        for (int i = 0; i < 2; i++)
        {
            if (written[i] == nullptr || *written[i] == '\0')
            {
                continue;
            }
            update_manifest::Anchor anchor = update_manifest::anchorFromHex(written[i]);
            if (!anchor.empty())
            {
                out.push_back(anchor);
            }
        }
        return out;
    }
}
