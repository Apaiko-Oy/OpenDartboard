// #1531: ONE corpus of manifests, asked of whichever signature arm this build has.
//
// #1337's decision, taken 2026-09-23: keep the hand-written P-256 arm, and hold both arms
// to one negative corpus. Options 1 and 3 of that issue together, with no new dependency.
//
// WHAT WAS UNEQUAL BEFORE THIS. src/update/manifest.hpp is one policy over two
// implementations: Windows verifies through CNG's BCryptVerifySignature (ADR-0077 §2,
// which is what ships to a board), and everything else -- including every harness in this
// directory -- through the hand-written src/update/p256_verify.hpp. The hand-written arm
// was measured against OpenSSL-minted signatures on Linux. The arm that SHIPS was asked
// about exactly two manifests, the release's own and that one with a byte moved, in the
// Windows release job, which runs on a tag or a dispatch. So the code most people read was
// not the code that runs, and the code that runs had two data points.
//
// HOW ONE CORPUS MAKES THE TWO ARMS ANSWER TO EACH OTHER, because this is the part worth
// reading twice. The two arms never meet: they compile on different machines, in different
// jobs, hours apart. What meets is testers/corpus1531/cases.tsv -- a committed table of
// case names and the verdict each one MUST get. Both arms read the same bytes and are held
// to the same pinned column, so an arm that answers differently from the pin is red, by
// name, on its own machine; and two arms cannot disagree about a case without at least one
// of them differing from the pin. That is the whole mechanism, and it needs no third
// opinion and no cross-job comparison.
//
// The pin is not somebody's idea of what ought to happen either. It is what both arms
// answer today, checked in, so a change to either arm that moves a verdict has to move the
// table in the same commit -- where a reader sees it.
//
// WHAT THIS CANNOT SEE, said here rather than discovered later. Three cases -- the two
// non-canonical DER encodings and the truncated signature -- are refused by
// manifest.hpp::detail::derSignature, which is SHARED, above the seam. Both arms agree
// about them trivially because neither arm is asked. They are in the corpus because they
// are refusals a board must make, not because they tell the arms apart.
//
//   i1531_corpus_check <corpus directory>
//
// It prints one line per case, and its exit status is the number of cases whose verdict is
// not the pinned one, capped at 100.

#ifdef _WIN32
#include "utils/od_platform_first.hpp"
#endif

#include "update/manifest.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::string slurp(const std::string &path)
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    std::string trimmed(const std::string &text)
    {
        size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return "";
        }
        size_t last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    /** The refusal's own name, so a mismatch reads as a sentence rather than as a number. */
    std::string nameOf(update_manifest::Refusal why)
    {
        switch (why)
        {
        case update_manifest::Refusal::None:
            return "None";
        case update_manifest::Refusal::NoAnchor:
            return "NoAnchor";
        case update_manifest::Refusal::NotJson:
            return "NotJson";
        case update_manifest::Refusal::NotAnEnvelope:
            return "NotAnEnvelope";
        case update_manifest::Refusal::NotBase64:
            return "NotBase64";
        case update_manifest::Refusal::NotVerified:
            return "NotVerified";
        case update_manifest::Refusal::PayloadNotJson:
            return "PayloadNotJson";
        case update_manifest::Refusal::PayloadNotAnObject:
            return "PayloadNotAnObject";
        case update_manifest::Refusal::FieldMissing:
            return "FieldMissing";
        case update_manifest::Refusal::SizeMissing:
            return "SizeMissing";
        case update_manifest::Refusal::DigestMalformed:
            return "DigestMalformed";
        case update_manifest::Refusal::UrlNotHttps:
            return "UrlNotHttps";
        case update_manifest::Refusal::WrongChannel:
            return "WrongChannel";
        }
        return "unknown";
    }

    struct Case
    {
        std::string name;
        std::string expect; // "accept" or "refuse"
        std::string anchor; // "-" for the corpus anchor, or 128 hex characters of its own
        std::string why;
    };
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: i1531_corpus_check <corpus directory>" << std::endl;
        return 100;
    }
    const std::string corpus = argv[1];

#ifdef _WIN32
    const std::string arm = "CNG BCryptVerifySignature -- THE ARM THAT SHIPS (ADR-0077 s2)";
#else
    const std::string arm = "src/update/p256_verify.hpp -- the hand-written arm";
#endif
    std::cout << "ARM  " << arm << std::endl;
    std::cout << "FROM " << corpus << std::endl;

    const std::string anchorHex = trimmed(slurp(corpus + "/anchor.hex"));
    if (anchorHex.size() != 128)
    {
        std::cout << "FAIL the corpus has no anchor: " << corpus << "/anchor.hex" << std::endl;
        return 100;
    }

    std::vector<Case> cases;
    {
        std::istringstream index(slurp(corpus + "/cases.tsv"));
        std::string line;
        while (std::getline(index, line))
        {
            line = trimmed(line);
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            std::istringstream fields(line);
            Case one;
            if (!std::getline(fields, one.name, '\t') || !std::getline(fields, one.expect, '\t') ||
                !std::getline(fields, one.anchor, '\t'))
            {
                std::cout << "FAIL cases.tsv has a row this reader cannot split: " << line << std::endl;
                return 100;
            }
            std::getline(fields, one.why);
            cases.push_back(one);
        }
    }
    if (cases.size() < 2)
    {
        std::cout << "FAIL the corpus holds " << cases.size() << " cases, which is not a corpus" << std::endl;
        return 100;
    }

    // The positive control is what makes every refusal below mean something, so its absence
    // is refused here rather than noticed by nobody: a corpus of nothing but refusals is
    // satisfied by `return false`.
    bool hasControl = false;
    for (size_t i = 0; i < cases.size(); i++)
    {
        if (cases[i].expect == "accept")
        {
            hasControl = true;
        }
    }
    if (!hasControl)
    {
        std::cout << "FAIL no case in this corpus must be ACCEPTED, so a verifier that "
                     "refuses everything would pass it"
                  << std::endl;
        return 100;
    }

    int mismatches = 0;
    for (size_t i = 0; i < cases.size(); i++)
    {
        const Case &one = cases[i];
        const std::string bytes = slurp(corpus + "/" + one.name + ".json");
        if (bytes.empty())
        {
            std::cout << "FAIL " << one.name << ": there is no " << one.name << ".json in this corpus" << std::endl;
            mismatches++;
            continue;
        }

        std::vector<update_manifest::Anchor> anchors;
        const std::string hex = (one.anchor == "-") ? anchorHex : one.anchor;
        const update_manifest::Anchor anchor = update_manifest::anchorFromHex(hex);
        if (anchor.size() != 64)
        {
            std::cout << "FAIL " << one.name << ": its anchor column is not 128 hex characters" << std::endl;
            mismatches++;
            continue;
        }
        anchors.push_back(anchor);

        const update_manifest::Read read = update_manifest::read(bytes, anchors);
        const std::string got = read.ok ? std::string("accept") : ("refuse:" + nameOf(read.refusal));
        const bool agreed = read.ok ? (one.expect == "accept") : (one.expect.compare(0, 6, "refuse") == 0 &&
                                                                  (one.expect == "refuse" || one.expect == got));

        if (agreed)
        {
            std::cout << "ok   " << one.name << " -> " << got << "   (" << one.why << ")" << std::endl;
        }
        else
        {
            std::cout << "FAIL " << one.name << " -> " << got << ", and this corpus pins " << one.expect
                      << "   (" << one.why << ")" << std::endl;
            mismatches++;
        }
    }

    std::cout << mismatches << " of " << cases.size() << " cases answered differently from the corpus" << std::endl;
    return mismatches > 100 ? 100 : mismatches;
}
