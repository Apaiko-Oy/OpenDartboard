#pragma once
// #1258: which three cameras, when the three this board would open do not open.
//
// This header is the whole decision and none of the hardware. It knows a video source as
// a name, a stable identity and the string CaptureSource::open() takes; it is handed a
// way to list the sources, a way to try opening one, and a console. The Windows half that
// supplies those is camera_setup.hpp. Keeping it free of OpenCV and of Windows is what
// lets testers/i1258_choice_check.cpp prove it in the Linux container.
//
// THE RULES, as #1258 states them:
//
//  * It asks only in an interactive console (console_prompt::isInteractiveConsole), and
//    only when at least one of the three does not open. The three are the remembered
//    cameras if there are any, else the platform's defaults. A start where all three open
//    asks nothing.
//  * It lists every source by name with a number and asks for three numbers. An answer
//    that is not three numbers, names a number that is not listed, repeats a camera, or
//    picks a camera that then does not open is said plainly and asked again.
//  * Fewer than three sources attached is said plainly, and the question waits for a
//    camera to be plugged in or for the program to be closed.
//  * The choice is remembered by identity, not by index, in the configuration directory
//    beside the credential. A remembered camera that is missing at start is asked about
//    again.
//  * Not interactive, not asked: no line is read and no camera is opened here. A
//    remembered choice whose cameras are all present is used; anything else is the
//    defaults, which is what the program did before this header existed.
//
// IDENTITY. On Windows a source's identity is Media Foundation's symbolic link. For a USB
// camera with a serial number that link survives a move to another port. For one WITHOUT
// a serial number -- most webcams -- Windows builds the instance part of the link from the
// port, so the link changes when the camera moves. A remembered camera not found by its
// link is therefore matched by its name, but only when that is unambiguous: exactly one
// unmatched remembered camera carries the name, and exactly one unclaimed present source
// does. Anything else is "missing" and is asked about. Two identical cameras swapped
// between two ports keep their links and are indistinguishable to any software; that is
// said here rather than claimed away.

#include "console_prompt.hpp"
#include "od_paths.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace camera_choice
{
    constexpr size_t kWanted = 3;

    /** A video source this machine can see. */
    struct Source
    {
        std::string name; // what a person recognises: the friendly name
        std::string id;   // what survives a change of index: the device's stable identifier
        std::string open; // what CaptureSource::open() is given: an index, or a path
    };

    /** A camera as it is remembered between starts. Never an index, never a secret. */
    struct Remembered
    {
        std::string name;
        std::string id;
    };

    /** What trying to open one source found. */
    struct Opened
    {
        bool ok = false;
        int width = 0;
        int height = 0;
    };

    // ------------------------------------------------------------------ the answer

    enum class Refusal
    {
        None,
        NotThreeNumbers,
        NotListed,
        Repeated
    };

    struct Answer
    {
        Refusal refusal = Refusal::None;
        std::vector<size_t> picks; // zero-based positions in the list, in the order given
        std::string offending;     // the number that was not listed, or was repeated
    };

    /**
     * An answer is `wanted` whole numbers from 1 to `listed`, separated by spaces, commas or
     * semicolons. Checked in this order: the shape, then each number is listed, then no
     * camera twice -- so the refusal said is the first thing wrong with the answer.
     */
    inline Answer parseAnswer(const std::string &text, size_t listed, size_t wanted = kWanted)
    {
        Answer answer;
        std::vector<std::string> tokens;
        std::string token;
        for (size_t i = 0; i <= text.size(); i++)
        {
            char c = i < text.size() ? text[i] : ' ';
            if (c == ' ' || c == '\t' || c == ',' || c == ';')
            {
                if (!token.empty())
                {
                    tokens.push_back(token);
                    token.clear();
                }
                continue;
            }
            token += c;
        }

        if (tokens.size() != wanted)
        {
            answer.refusal = Refusal::NotThreeNumbers;
            return answer;
        }
        for (const std::string &t : tokens)
        {
            if (t.size() > 6)
            {
                answer.refusal = Refusal::NotListed;
                answer.offending = t;
                return answer;
            }
            for (char c : t)
            {
                if (!std::isdigit(static_cast<unsigned char>(c)))
                {
                    answer.refusal = Refusal::NotThreeNumbers;
                    return answer;
                }
            }
        }
        for (const std::string &t : tokens)
        {
            long number = std::stol(t);
            if (number < 1 || static_cast<size_t>(number) > listed)
            {
                answer.refusal = Refusal::NotListed;
                answer.offending = t;
                return answer;
            }
        }
        std::set<size_t> seen;
        for (const std::string &t : tokens)
        {
            size_t position = static_cast<size_t>(std::stol(t)) - 1;
            if (!seen.insert(position).second)
            {
                answer.refusal = Refusal::Repeated;
                answer.offending = std::to_string(position + 1);
                answer.picks.clear();
                return answer;
            }
            answer.picks.push_back(position);
        }
        return answer;
    }

    // ------------------------------------------------------------------ the words

    inline console_prompt::Text refusalText(const Answer &answer, size_t listed)
    {
        const std::string n = std::to_string(listed);
        switch (answer.refusal)
        {
        case Refusal::NotListed:
            return {"Numeroa " + answer.offending + " ei ole listassa. Valitse numeroista 1-" + n + ".",
                    "There is no number " + answer.offending + " in the list. Choose from 1-" + n + "."};
        case Refusal::Repeated:
            return {"Lähde " + answer.offending + " on valittu kahdesti. Valitse kolme eri kameraa.",
                    "Source " + answer.offending + " was chosen twice. Choose three different cameras."};
        case Refusal::NotThreeNumbers:
        default:
            return {"Vastaus ei ollut kolme numeroa. Kirjoita esimerkiksi: 1 2 3",
                    "That was not three numbers. Type, for example: 1 2 3"};
        }
    }

    inline console_prompt::Text questionText()
    {
        return {"Kirjoita kolmen kameran numerot välilyönnein erotettuina ja paina Enter:",
                "Type the numbers of three cameras, separated by spaces, and press Enter:"};
    }

    inline console_prompt::Text listHeaderText()
    {
        return {"Tällä koneella näkyvät videolähteet:", "The video sources this computer can see:"};
    }

    inline console_prompt::Text fewerThanThreeText(size_t count)
    {
        const std::string n = std::to_string(count);
        return {"Videolähteitä on vain " + n + ", ja tarvitaan kolme. Kytke kamera, niin kysymys jatkuu, tai sulje ohjelma.",
                "Only " + n + " video source(s) are attached, and three are needed. Plug in a camera and the question "
                              "continues, or close the program."};
    }

    inline console_prompt::Text notOpenedText(const std::string &what)
    {
        return {"Kamera ei auennut: " + what, "A camera did not open: " + what};
    }

    inline console_prompt::Text missingText(const Remembered &camera)
    {
        return {"Muistettu kamera \"" + camera.name + "\" ei ole kytkettynä.",
                "The remembered camera \"" + camera.name + "\" is not attached."};
    }

    inline console_prompt::Text openedText(size_t slot, const std::string &name, const Opened &opened)
    {
        const std::string k = std::to_string(slot + 1);
        if (!opened.ok)
        {
            return {"Kamera " + k + ": " + name + " ei auennut.", "Camera " + k + ": " + name + " did not open."};
        }
        const std::string size = std::to_string(opened.width) + "x" + std::to_string(opened.height);
        return {"Kamera " + k + ": " + name + " avautui, " + size + ".",
                "Camera " + k + ": " + name + " opened at " + size + "."};
    }

    inline console_prompt::Text chooseAgainText()
    {
        return {"Kaikki valitut kamerat eivät auenneet. Valitse uudelleen.",
                "Not every chosen camera opened. Choose again."};
    }

    inline console_prompt::Text usingRememberedText()
    {
        return {"Käytetään muistettuja kameroita.", "Using the remembered cameras."};
    }

    // ------------------------------------------------------------------ identity

    struct Resolution
    {
        std::vector<long> found;    // per remembered camera: its position in the sources, or -1
        std::vector<size_t> missing; // positions in the remembered list that were not found

        bool complete() const { return missing.empty() && !found.empty(); }
    };

    /** Find each remembered camera among the present sources, by the rule at the top. */
    inline Resolution resolve(const std::vector<Remembered> &remembered, const std::vector<Source> &sources)
    {
        Resolution resolution;
        resolution.found.assign(remembered.size(), -1);
        std::vector<bool> claimed(sources.size(), false);

        // By identity first.
        for (size_t r = 0; r < remembered.size(); r++)
        {
            if (remembered[r].id.empty())
            {
                continue;
            }
            for (size_t s = 0; s < sources.size(); s++)
            {
                if (!claimed[s] && sources[s].id == remembered[r].id)
                {
                    resolution.found[r] = static_cast<long>(s);
                    claimed[s] = true;
                    break;
                }
            }
        }

        // Then by name, where the name cannot mean two cameras.
        for (size_t r = 0; r < remembered.size(); r++)
        {
            if (resolution.found[r] >= 0 || remembered[r].name.empty())
            {
                continue;
            }
            size_t unmatched_with_name = 0;
            for (size_t other = 0; other < remembered.size(); other++)
            {
                if (resolution.found[other] < 0 && remembered[other].name == remembered[r].name)
                {
                    unmatched_with_name++;
                }
            }
            long candidate = -1;
            size_t candidates = 0;
            for (size_t s = 0; s < sources.size(); s++)
            {
                if (!claimed[s] && sources[s].name == remembered[r].name)
                {
                    candidate = static_cast<long>(s);
                    candidates++;
                }
            }
            if (unmatched_with_name == 1 && candidates == 1)
            {
                resolution.found[r] = candidate;
                claimed[static_cast<size_t>(candidate)] = true;
            }
        }

        for (size_t r = 0; r < remembered.size(); r++)
        {
            if (resolution.found[r] < 0)
            {
                resolution.missing.push_back(r);
            }
        }
        return resolution;
    }

    // ------------------------------------------------------------------ the file

    inline const char *kFileName = "cameras.json";

    /**
     * The choice is kept in the directory the credential is kept in, so --credentials,
     * which moves a board's configuration for a service or a USB stick, moves this too.
     * A separate file: nothing here is a secret and the credential is never read or
     * rewritten to store it.
     */
    inline std::string fileBeside(const std::string &credentials_path)
    {
        size_t cut = credentials_path.find_last_of("/\\");
        if (cut == std::string::npos)
        {
            return kFileName;
        }
        return credentials_path.substr(0, cut + 1) + kFileName;
    }

    /** False, and `remembered` empty, when there is no usable choice on disk. */
    inline bool load(const std::string &path, std::vector<Remembered> &remembered)
    {
        remembered.clear();
        std::string raw;
        if (!od_paths::readFile(path, raw))
        {
            return false;
        }
        nlohmann::json doc = nlohmann::json::parse(raw, nullptr, /*allow_exceptions*/ false);
        if (!doc.is_object() || !doc.contains("cameras") || !doc["cameras"].is_array())
        {
            return false;
        }
        for (const auto &entry : doc["cameras"])
        {
            if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() || !entry.contains("name") ||
                !entry["name"].is_string())
            {
                remembered.clear();
                return false;
            }
            remembered.push_back(Remembered{entry["name"].get<std::string>(), entry["id"].get<std::string>()});
        }
        if (remembered.size() != kWanted)
        {
            remembered.clear();
            return false;
        }
        return true;
    }

    inline bool save(const std::string &path, const std::vector<Remembered> &remembered)
    {
        nlohmann::json doc;
        doc["version"] = 1;
        doc["cameras"] = nlohmann::json::array();
        for (const Remembered &camera : remembered)
        {
            doc["cameras"].push_back({{"name", camera.name}, {"id", camera.id}});
        }
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

    // ------------------------------------------------------------------ the flow

    /** The hardware, handed in. */
    struct Hardware
    {
        std::function<std::vector<Source>()> list;
        std::function<Opened(const Source &)> open;
        std::function<void()> wait; // between two looks for a camera being plugged in
    };

    struct Outcome
    {
        std::vector<std::string> cams;  // what the scorer is given
        bool asked = false;             // a question was put to the person
        bool chosen = false;            // and answered with three cameras that opened
        bool from_remembered = false;   // the remembered choice was used without asking
        std::vector<Remembered> remember; // what to keep, when chosen
    };

    namespace detail
    {
        inline Source sourceFor(const std::string &open, const std::vector<Source> &sources)
        {
            for (const Source &s : sources)
            {
                if (s.open == open)
                {
                    return s;
                }
            }
            return Source{open, "", open};
        }

        inline bool sameList(const std::vector<Source> &a, const std::vector<Source> &b)
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (size_t i = 0; i < a.size(); i++)
            {
                if (a[i].id != b[i].id || a[i].name != b[i].name || a[i].open != b[i].open)
                {
                    return false;
                }
            }
            return true;
        }
    }

    /**
     * Decide the cameras at start. `defaults` is what the program opened before #1258 --
     * 0,1,2 on Windows -- and is what a start that does not ask falls back to.
     * `have_remembered` is whether `remembered` was loaded from disk.
     */
    inline Outcome choose(bool interactive, const std::vector<std::string> &defaults, bool have_remembered,
                          const std::vector<Remembered> &remembered, console_prompt::Console &console,
                          Hardware &hardware)
    {
        Outcome outcome;
        outcome.cams = defaults;

        // Not interactive and nothing remembered: exactly the program as it was. Nothing
        // is listed, nothing is opened, nothing is read.
        if (!interactive && !have_remembered)
        {
            return outcome;
        }

        std::vector<Source> sources = hardware.list();
        std::vector<Source> candidates;
        bool remembered_complete = false;
        if (have_remembered)
        {
            Resolution resolution = resolve(remembered, sources);
            if (resolution.complete())
            {
                remembered_complete = true;
                for (long position : resolution.found)
                {
                    candidates.push_back(sources[static_cast<size_t>(position)]);
                }
            }
            else if (interactive)
            {
                for (size_t r : resolution.missing)
                {
                    console.say(missingText(remembered[r]));
                }
            }
        }
        else
        {
            for (const std::string &open : defaults)
            {
                candidates.push_back(detail::sourceFor(open, sources));
            }
        }

        if (!interactive)
        {
            // A remembered choice that is all present is used; anything else is the
            // defaults, unopened here, so a camera that will not open meets #895's fault
            // exactly as it did before.
            if (remembered_complete)
            {
                outcome.cams.clear();
                for (const Source &s : candidates)
                {
                    outcome.cams.push_back(s.open);
                }
                outcome.from_remembered = true;
            }
            return outcome;
        }

        // Interactive: do the three open? Tried once each, and released; the capture layer
        // opens them again for real.
        if (!candidates.empty())
        {
            bool all_open = true;
            for (const Source &s : candidates)
            {
                if (!hardware.open(s).ok)
                {
                    all_open = false;
                    console.say(notOpenedText(s.name));
                }
            }
            if (all_open)
            {
                outcome.cams.clear();
                for (const Source &s : candidates)
                {
                    outcome.cams.push_back(s.open);
                }
                if (remembered_complete)
                {
                    outcome.from_remembered = true;
                    console.say(usingRememberedText());
                }
                return outcome;
            }
        }

        // Ask.
        outcome.asked = true;
        std::vector<Source> shown;
        size_t said_fewer_for = static_cast<size_t>(-1);
        for (;;)
        {
            sources = hardware.list();
            if (sources.size() < kWanted)
            {
                if (said_fewer_for != sources.size())
                {
                    console.say(fewerThanThreeText(sources.size()));
                    said_fewer_for = sources.size();
                }
                shown.clear();
                hardware.wait();
                continue;
            }
            said_fewer_for = static_cast<size_t>(-1);

            if (!detail::sameList(sources, shown))
            {
                console.say(listHeaderText());
                for (size_t i = 0; i < sources.size(); i++)
                {
                    // A name is not translated; it is the same line in both languages.
                    console.sayVerbatim("  " + std::to_string(i + 1) + ") " + sources[i].name);
                }
                shown = sources;
            }

            Answer answer;
            const size_t listed = sources.size();
            std::string line;
            bool answered = console_prompt::askUntil(
                console, questionText(),
                [&](const std::string &text)
                {
                    answer = parseAnswer(text, listed);
                    return answer.refusal == Refusal::None
                               ? console_prompt::Verdict::accept()
                               : console_prompt::Verdict::refuse(refusalText(answer, listed));
                },
                line);
            if (!answered)
            {
                // Input ended. Behave as the program did: the defaults, or whatever was
                // about to be opened, and #895's fault if they do not open.
                return outcome;
            }

            bool all_open = true;
            std::vector<Source> picked;
            for (size_t slot = 0; slot < answer.picks.size(); slot++)
            {
                const Source &s = sources[answer.picks[slot]];
                Opened opened = hardware.open(s);
                console.say(openedText(slot, s.name, opened));
                all_open = all_open && opened.ok;
                picked.push_back(s);
            }
            if (!all_open)
            {
                console.say(chooseAgainText());
                continue;
            }

            outcome.chosen = true;
            outcome.cams.clear();
            outcome.remember.clear();
            for (const Source &s : picked)
            {
                outcome.cams.push_back(s.open);
                outcome.remember.push_back(Remembered{s.name, s.id});
            }
            return outcome;
        }
    }
}
