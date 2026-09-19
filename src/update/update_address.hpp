#pragma once
// #1306: the four steps that decide where Turnaus is, in one place, because two programs
// now have to agree about it.
//
// ADR-0077 §5: "The launcher reads the same credential file and uses the same rule. It
// introduces no address of its own." Until this header the rule was twenty lines in the
// middle of the detector's main(), which is fine while one program follows it and a
// drift waiting to happen the moment a second one does -- and a launcher that resolved a
// different address from the detector would check one deployment for updates and post
// scores to another.
//
//   1. --turnaus <url>
//   2. OD_TURNAUS_URL
//   3. the base_url the pairing wrote into the credential file
//   4. turnaus_address::kDefault (#1257: production, never a domain somebody can buy)
//
// The credential file is read as TEXT and not as JSON, which is what main.cpp already did
// and is kept deliberately: this is the one thing the launcher reads out of a file whose
// other field is a bearer token, and a hand-rolled scan for one quoted value cannot be
// talked into materialising the rest of the document anywhere. Nothing here logs, returns
// or stores anything but the address.

#include "../communication/turnaus_address.hpp"
#include "../utils/od_paths.hpp"

#include <string>
#include <vector>

namespace update_address
{
    struct Resolved
    {
        std::string url;
        std::string source; // "--turnaus" | "OD_TURNAUS_URL" | "the credential file" | "the default"
    };

    /** The `base_url` a pairing wrote, or empty. Reads one quoted value and nothing else. */
    inline std::string baseUrlIn(const std::string &credentials_path)
    {
        std::string raw;
        if (!od_paths::readFile(credentials_path, raw))
        {
            return std::string();
        }
        const size_t at = raw.find("\"base_url\"");
        if (at == std::string::npos)
        {
            return std::string();
        }
        const size_t colon = raw.find(':', at);
        if (colon == std::string::npos)
        {
            return std::string();
        }
        const size_t open_quote = raw.find('"', colon + 1);
        const size_t close_quote = open_quote == std::string::npos ? std::string::npos : raw.find('"', open_quote + 1);
        if (close_quote == std::string::npos)
        {
            return std::string();
        }
        return raw.substr(open_quote + 1, close_quote - open_quote - 1);
    }

    /** The four steps. `flag` is whatever --turnaus said, empty when it said nothing. */
    inline Resolved resolve(const std::string &flag, const std::string &credentials_path)
    {
        Resolved resolved;
        if (!flag.empty())
        {
            resolved.url = flag;
            resolved.source = "--turnaus";
            return resolved;
        }
        resolved.url = od_paths::env("OD_TURNAUS_URL");
        if (!resolved.url.empty())
        {
            resolved.source = "OD_TURNAUS_URL";
            return resolved;
        }
        resolved.url = baseUrlIn(credentials_path);
        if (!resolved.url.empty())
        {
            resolved.source = "the credential file";
            return resolved;
        }
        resolved.url = turnaus_address::kDefault;
        resolved.source = "the default";
        return resolved;
    }

    // ------------------------------------------------------------------ for the launcher
    //
    // The detector has getArg/hasFlag over argc/argv. The launcher holds its arguments as
    // a vector<string> because it passes them on as one (#1303), so the same two questions
    // are asked of that here. It READS them and claims none: every one is still handed to
    // the detector, in order, untouched.

    inline std::string argumentAfter(const std::vector<std::string> &arguments, const char *name)
    {
        for (size_t i = 0; i + 1 < arguments.size(); i++)
        {
            if (arguments[i] == name)
            {
                return arguments[i + 1];
            }
        }
        return std::string();
    }

    inline bool hasArgument(const std::vector<std::string> &arguments, const char *name)
    {
        for (size_t i = 0; i < arguments.size(); i++)
        {
            if (arguments[i] == name)
            {
                return true;
            }
        }
        return false;
    }
}
