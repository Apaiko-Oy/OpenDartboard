#pragma once
// #1305: which releases this install is offered, and where that is kept.
//
// WHERE. `channel.json`, beside `credentials.json` and `cameras.json` in the directory
// od_paths.hpp chose (%APPDATA%\OpenDartboard on Windows). Beside rather than inside,
// exactly as camera_choice.hpp is beside: nothing here is a secret, and the credential is
// never read or rewritten to store something that is not one. --credentials moves a
// board's configuration for a service or a USB stick, and this moves with it.
//
// WHICH. `stable` or `beta` and nothing else. Those two are not this file's opinion --
// `routes/web.php` in Turnaus constrains {channel} to exactly them, so a third word here
// would be a file that survives a restart and can only ever be answered with a 404.
//
// WHEN IT IS WRITTEN. Only when somebody sets it. A board that has never been told reads
// `stable` and writes nothing, so a --check-update run leaves the filesystem as it found
// it (#1305's fourth criterion).

#include "../utils/od_paths.hpp"

#include <string>
#include <fstream>

#include <nlohmann/json.hpp>

namespace update_channel
{
    inline const char *kFileName = "channel.json";
    inline const char *kDefault = "stable";

    /** True for one of the two the route constrains. */
    inline bool isKnown(const std::string &channel) { return channel == "stable" || channel == "beta"; }

    /** The two, for a sentence that has to name them. */
    inline std::string known() { return "stable, beta"; }

    inline std::string fileBeside(const std::string &credentials_path)
    {
        size_t cut = credentials_path.find_last_of("/\\");
        if (cut == std::string::npos)
        {
            return kFileName;
        }
        return credentials_path.substr(0, cut + 1) + kFileName;
    }

    /**
     * What this install is on. `stable` when there is no file, when it will not parse, or
     * when it names something the route does not serve -- a board is never left with no
     * channel at all, because that would be a board that can never be updated again.
     */
    inline std::string load(const std::string &path)
    {
        std::string raw;
        if (!od_paths::readFile(path, raw))
        {
            return kDefault;
        }
        nlohmann::json doc = nlohmann::json::parse(raw, nullptr, /*allow_exceptions*/ false);
        if (!doc.is_object() || !doc.contains("channel") || !doc["channel"].is_string())
        {
            return kDefault;
        }
        std::string channel = doc["channel"].get<std::string>();
        return isKnown(channel) ? channel : std::string(kDefault);
    }

    /** False when the channel is not one of the two, or when the file cannot be written. */
    inline bool save(const std::string &path, const std::string &channel)
    {
        if (!isKnown(channel))
        {
            return false;
        }
        nlohmann::json doc;
        doc["version"] = 1;
        doc["channel"] = channel;
        size_t cut = path.find_last_of("/\\");
        if (cut != std::string::npos)
        {
            od_paths::ensureDir(path.substr(0, cut));
        }
        std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return false;
        }
        out << doc.dump(2) << "\n";
        out.flush();
        return out.good();
    }
}
