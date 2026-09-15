// #1258: the camera question's logic, proved without a camera or a console.
//
// camera_choice.hpp is handed its hardware and its console, so here both are scripts: a
// list of sources that can change between looks, a set of sources that refuse to open,
// and a queue of typed lines. Everything said is recorded and asserted on.
//
//   testers/i1258_check.sh     (compiles and runs this in od-amd64:bullseye, stdin </dev/null)

#include "camera_choice.hpp"
#include "console_prompt.hpp"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <set>
#include <string>
#include <vector>
#include <unistd.h>

using camera_choice::Opened;
using camera_choice::Refusal;
using camera_choice::Remembered;
using camera_choice::Source;

static int checks = 0;
static int failed = 0;

static void check(bool ok, const std::string &what)
{
    checks++;
    if (!ok)
    {
        failed++;
        std::printf("FAIL %s\n", what.c_str());
    }
    else
    {
        std::printf("ok   %s\n", what.c_str());
    }
}

struct ScriptedConsole : console_prompt::Console
{
    std::deque<std::string> typed;
    std::vector<std::string> said; // every line, Finnish and English separately
    int reads = 0;
    int questions = 0;

    void say(const console_prompt::Text &text) override
    {
        said.push_back(text.fi);
        said.push_back(text.en);
        if (text.en == camera_choice::questionText().en)
            questions++;
    }
    void sayVerbatim(const std::string &line) override { said.push_back(line); }
    bool readLine(std::string &line) override
    {
        reads++;
        if (typed.empty())
            return false;
        line = typed.front();
        typed.pop_front();
        return true;
    }
    bool heard(const std::string &line) const
    {
        for (const auto &s : said)
            if (s == line)
                return true;
        return false;
    }
    int count(const std::string &line) const
    {
        int n = 0;
        for (const auto &s : said)
            if (s == line)
                n++;
        return n;
    }
};

struct ScriptedHardware
{
    std::vector<std::vector<Source>> looks; // successive answers to list(); the last repeats
    std::set<std::string> refuses;          // `open` strings that do not open
    size_t listed_calls = 0;
    int opens = 0;
    int waits = 0;

    camera_choice::Hardware bind()
    {
        camera_choice::Hardware hw;
        hw.list = [this]
        {
            size_t at = listed_calls < looks.size() ? listed_calls : looks.size() - 1;
            listed_calls++;
            return looks[at];
        };
        hw.open = [this](const Source &s)
        {
            opens++;
            Opened o;
            o.ok = refuses.count(s.open) == 0;
            if (o.ok)
            {
                o.width = 1280;
                o.height = 720;
            }
            return o;
        };
        hw.wait = [this] { waits++; };
        return hw;
    }
};

static const std::vector<std::string> kDefaults = {"0", "1", "2"};

static Source src(const std::string &name, const std::string &id, const std::string &open)
{
    return Source{name, id, open};
}

int main()
{
    // ---------------------------------------------------------------- parsing
    {
        auto a = camera_choice::parseAnswer("1 2 3", 4);
        check(a.refusal == Refusal::None && a.picks == std::vector<size_t>({0, 1, 2}), "parse: \"1 2 3\" is three picks");
        a = camera_choice::parseAnswer(" 3,1 ; 4 ", 4);
        check(a.refusal == Refusal::None && a.picks == std::vector<size_t>({2, 0, 3}),
              "parse: commas, semicolons and spaces separate, order kept");
        check(camera_choice::parseAnswer("1 2", 4).refusal == Refusal::NotThreeNumbers, "parse: two numbers refused");
        check(camera_choice::parseAnswer("1 2 3 4", 4).refusal == Refusal::NotThreeNumbers, "parse: four numbers refused");
        check(camera_choice::parseAnswer("", 4).refusal == Refusal::NotThreeNumbers, "parse: an empty line refused");
        check(camera_choice::parseAnswer("yksi kaksi kolme", 4).refusal == Refusal::NotThreeNumbers,
              "parse: words refused");
        check(camera_choice::parseAnswer("1 2 -3", 4).refusal == Refusal::NotThreeNumbers, "parse: a sign refused");
        a = camera_choice::parseAnswer("1 2 9", 4);
        check(a.refusal == Refusal::NotListed && a.offending == "9", "parse: 9 of 4 refused as not listed, naming 9");
        a = camera_choice::parseAnswer("0 1 2", 4);
        check(a.refusal == Refusal::NotListed && a.offending == "0", "parse: 0 refused as not listed (the list is 1-based)");
        a = camera_choice::parseAnswer("2 1 2", 4);
        check(a.refusal == Refusal::Repeated && a.offending == "2" && a.picks.empty(),
              "parse: a repeated camera refused, naming it");
        a = camera_choice::parseAnswer("99999999999999999999 1 2", 4);
        check(a.refusal == Refusal::NotListed, "parse: an absurdly long number refused without overflowing");

        for (Refusal r : {Refusal::NotThreeNumbers, Refusal::NotListed, Refusal::Repeated})
        {
            camera_choice::Answer x;
            x.refusal = r;
            x.offending = "7";
            auto t = camera_choice::refusalText(x, 4);
            check(!t.fi.empty() && !t.en.empty() && t.fi != t.en, "words: every refusal has a Finnish and an English line");
        }
    }

    // ---------------------------------------------------------------- identity
    const Source A = src("Cam A", "usb#vid_1&pid_1#serialA", "0");
    const Source B = src("Cam B", "usb#vid_2&pid_2#serialB", "1");
    const Source C = src("Cam C", "usb#vid_3&pid_3#serialC", "2");
    const Source X = src("Laptop", "pci#builtin", "3");
    const std::vector<Remembered> remembered = {{A.name, A.id}, {B.name, B.id}, {C.name, C.id}};
    {
        // The same three, but Windows now numbers them differently.
        std::vector<Source> moved = {src(X.name, X.id, "0"), src(C.name, C.id, "1"), src(A.name, A.id, "2"),
                                     src(B.name, B.id, "3")};
        auto r = camera_choice::resolve(remembered, moved);
        check(r.complete() && r.found == std::vector<long>({2, 3, 1}),
              "identity: a remembered camera found under a different index is still chosen");

        std::vector<Source> without_c = {X, A, B};
        r = camera_choice::resolve(remembered, without_c);
        check(!r.complete() && r.missing == std::vector<size_t>({2}), "identity: a remembered camera that is missing is missing");

        // B plugged into another port: no serial number, so Windows gave it another link.
        std::vector<Source> replugged = {A, src(B.name, "usb#vid_2&pid_2#port7", "1"), C};
        r = camera_choice::resolve(remembered, replugged);
        check(r.complete() && r.found[1] == 1, "identity: a changed link is matched by a name nothing else carries");

        // Two cameras now carry B's name and neither carries B's link: which one is B?
        std::vector<Source> twins = {A, src(B.name, "usb#port7", "1"), C, src(B.name, "usb#port8", "3")};
        r = camera_choice::resolve(remembered, twins);
        check(!r.complete() && r.missing == std::vector<size_t>({1}), "identity: an ambiguous name is not guessed");

        // A present source is never given to two remembered cameras.
        std::vector<Remembered> same_twice = {{A.name, A.id}, {A.name, A.id}, {C.name, C.id}};
        r = camera_choice::resolve(same_twice, {A, B, C});
        check(!r.complete(), "identity: one source never answers for two remembered cameras");
    }

    // ---------------------------------------------------------------- the file
    {
        char dir_template[] = "/tmp/i1258-XXXXXX";
        std::string dir = mkdtemp(dir_template);
        std::string creds = dir + "/cfg/credentials.json";
        std::string path = camera_choice::fileBeside(creds);
        check(path == dir + "/cfg/cameras.json", "file: kept beside the credential");
        check(camera_choice::fileBeside("C:\\Users\\u\\AppData\\Roaming\\OpenDartboard\\credentials.json") ==
                  "C:\\Users\\u\\AppData\\Roaming\\OpenDartboard\\cameras.json",
              "file: beside the credential on a Windows path too");

        std::vector<Remembered> loaded;
        check(!camera_choice::load(path, loaded) && loaded.empty(), "file: absent means nothing remembered");

        std::vector<Remembered> windows = {{"Iriun Webcam", "\\\\?\\root#camera#0000#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global"},
                                           {"Mock \"B\"", "file:C:\\od\\mocks\\cam_2.mp4"},
                                           {"Kamera ä", "usb#vid_046d&pid_0825"}};
        check(camera_choice::save(path, windows), "file: saved, creating the directory");
        check(camera_choice::load(path, loaded) && loaded.size() == 3 && loaded[0].id == windows[0].id &&
                  loaded[1].name == windows[1].name && loaded[2].name == windows[2].name,
              "file: backslashes, quotes and UTF-8 come back as they went");
        std::string raw;
        od_paths::readFile(path, raw);
        check(raw.find("token") == std::string::npos && raw.find("\"index\"") == std::string::npos,
              "file: holds no token and no index");

        FILE *f = std::fopen(path.c_str(), "wb");
        std::fputs("{\"cameras\":[{\"name\":\"a\",\"id\":\"1\"},{\"name\":\"b\",\"id\":\"2\"}]}", f);
        std::fclose(f);
        check(!camera_choice::load(path, loaded) && loaded.empty(), "file: two cameras is not a choice");
        f = std::fopen(path.c_str(), "wb");
        std::fputs("{\"cameras\": [", f);
        std::fclose(f);
        check(!camera_choice::load(path, loaded), "file: a truncated file is not a choice, and does not throw");
    }

    // ---------------------------------------------------------------- the flow
    {
        // All three defaults open: nothing is asked and nothing is read.
        ScriptedConsole console;
        ScriptedHardware hw;
        hw.looks = {{A, B, C, X}};
        auto hardware = hw.bind();
        auto out = camera_choice::choose(true, kDefaults, false, {}, console, hardware);
        check(!out.asked && out.cams == kDefaults && console.reads == 0 && console.said.empty(),
              "flow: a start where all three open asks nothing");
    }
    {
        // Camera 1 of the defaults does not open. Then: words, a repeat, an unlisted number,
        // a pick that does not open, and finally three that do.
        ScriptedConsole console;
        ScriptedHardware hw;
        hw.looks = {{A, B, C, X}};
        hw.refuses = {"1"};
        console.typed = {"kolme", "1 1 3", "1 3 9", "1 2 4", "1 3 4"};
        auto hardware = hw.bind();
        auto out = camera_choice::choose(true, kDefaults, false, {}, console, hardware);
        check(out.asked && out.chosen, "flow: a camera that does not open asks, and an answer chooses");
        check(out.cams == std::vector<std::string>({"0", "2", "3"}), "flow: the chosen three are what the scorer gets");
        check(out.remember.size() == 3 && out.remember[2].id == X.id && out.remember[1].id == C.id,
              "flow: what is remembered is identity, in the order chosen");
        check(console.heard("A camera did not open: Cam B") && console.heard("Kamera ei auennut: Cam B"),
              "flow: the default that did not open is named, in both languages");
        check(console.heard("  1) Cam A") && console.heard("  4) Laptop"), "flow: every source is listed by name with a number");
        check(console.count(camera_choice::listHeaderText().en) == 1,
              "flow: the list is said once while it does not change");
        check(console.heard("That was not three numbers. Type, for example: 1 2 3"), "flow: words are refused plainly");
        check(console.heard("Source 1 was chosen twice. Choose three different cameras.") &&
                  console.heard("Lähde 1 on valittu kahdesti. Valitse kolme eri kameraa."),
              "flow: a repeated camera is refused plainly, in both languages");
        check(console.heard("There is no number 9 in the list. Choose from 1-4."), "flow: an unlisted number is refused plainly");
        check(console.heard("Camera 2: Cam B did not open.") && console.heard(camera_choice::chooseAgainText().en),
              "flow: a pick that does not open is said, and the question asked again");
        check(console.heard("Camera 3: Laptop opened at 1280x720.") && console.heard("Kamera 3: Laptop avautui, 1280x720."),
              "flow: each opened camera says its resolution, in both languages");
        check(console.questions == 5 && console.reads == 5, "flow: five answers, five questions, nothing read twice");
    }
    {
        // Windows lists only three sources and one of them is a file, so default index 2
        // names nothing at all. It is said as an index with nothing attached, it is not
        // tried, and it is not called a failure of "camera 2" -- which, on a 1-based list,
        // would be a different camera.
        const Source D = src("Mock file", "file:C:\\mocks\\cam_3.mp4", "C:\\mocks\\cam_3.mp4");
        ScriptedConsole console;
        ScriptedHardware hw;
        hw.looks = {{A, B, D}};
        console.typed = {"1 2 3"};
        auto hardware = hw.bind();
        auto out = camera_choice::choose(true, kDefaults, false, {}, console, hardware);
        check(console.heard("Nothing is attached at camera index 2.") && console.heard("Kameraindeksissä 2 ei ole kameraa."),
              "flow: a default index with no source is said as an index with nothing attached");
        check(!console.heard("A camera did not open: 2") && hw.opens == 2 + 3,
              "flow: and is not tried, nor called a camera that did not open");
        check(out.chosen && out.cams == std::vector<std::string>({"0", "1", "C:\\mocks\\cam_3.mp4"}),
              "flow: then asked, and a listed file source can be chosen");
    }
    {
        // Remembered, all present under other indices, all open: no question.
        ScriptedConsole console;
        ScriptedHardware hw;
        hw.looks = {{src(X.name, X.id, "0"), src(C.name, C.id, "1"), src(A.name, A.id, "2"), src(B.name, B.id, "3")}};
        auto hardware = hw.bind();
        auto out = camera_choice::choose(true, kDefaults, true, remembered, console, hardware);
        check(!out.asked && out.from_remembered && out.cams == std::vector<std::string>({"2", "3", "1"}) && console.reads == 0,
              "flow: the remembered cameras open under their new indices, and nothing is asked");
    }
    {
        // Remembered, one missing: named, and asked.
        ScriptedConsole console;
        ScriptedHardware hw;
        hw.looks = {{A, B, X}};
        console.typed = {"1 2 3"};
        auto hardware = hw.bind();
        auto out = camera_choice::choose(true, kDefaults, true, remembered, console, hardware);
        check(out.asked && out.chosen && console.heard("The remembered camera \"Cam C\" is not attached."),
              "flow: a remembered camera that is missing is named and asked about again");
    }
    {
        // Two sources, then two again, then a third is plugged in.
        ScriptedConsole console;
        ScriptedHardware hw;
        hw.looks = {{A, B}, {A, B}, {A, B}, {A, B, C}};
        hw.refuses = {"2"};
        std::vector<Source> first = {A, B};
        console.typed = {"1 2 3"};
        hw.looks[3][2].open = "5";
        auto hardware = hw.bind();
        auto out = camera_choice::choose(true, kDefaults, false, {}, console, hardware);
        check(console.heard(camera_choice::fewerThanThreeText(2).en) && console.heard(camera_choice::fewerThanThreeText(2).fi),
              "flow: fewer than three sources is said plainly, in both languages");
        check(console.count(camera_choice::fewerThanThreeText(2).en) == 1, "flow: and said once, not once per look");
        check(hw.waits == 2 && console.reads == 1 && out.chosen, "flow: the question waits for a camera, then asks");
    }
    {
        // Input ends mid-question: no loop, the defaults, nothing chosen.
        ScriptedConsole console;
        ScriptedHardware hw;
        hw.looks = {{A, B, C}};
        hw.refuses = {"0"};
        console.typed = {"nope"};
        auto hardware = hw.bind();
        auto out = camera_choice::choose(true, kDefaults, false, {}, console, hardware);
        check(out.asked && !out.chosen && out.cams == kDefaults && console.reads == 2,
              "flow: input that ends leaves the defaults and does not spin");
    }
    {
        // Not interactive: never read, never opened.
        ScriptedConsole console;
        ScriptedHardware hw;
        hw.looks = {{A, B}};
        hw.refuses = {"0", "1", "2"};
        auto hardware = hw.bind();
        auto out = camera_choice::choose(false, kDefaults, false, {}, console, hardware);
        check(out.cams == kDefaults && console.reads == 0 && hw.opens == 0 && hw.listed_calls == 0 && console.said.empty(),
              "not interactive, nothing remembered: the defaults, nothing listed, opened, said or read");

        hw.looks = {{src(X.name, X.id, "0"), src(C.name, C.id, "1"), src(A.name, A.id, "2"), src(B.name, B.id, "3")}};
        out = camera_choice::choose(false, kDefaults, true, remembered, console, hardware);
        check(out.cams == std::vector<std::string>({"2", "3", "1"}) && console.reads == 0 && hw.opens == 0,
              "not interactive, remembered and present: the remembered cameras, nothing opened or read");

        hw.looks = {{A, B, X}};
        out = camera_choice::choose(false, kDefaults, true, remembered, console, hardware);
        check(out.cams == kDefaults && console.reads == 0 && hw.opens == 0 && console.said.empty(),
              "not interactive, a remembered camera missing: the defaults, nothing said or read");
    }

    // ---------------------------------------------------------------- the console decision
    // Run with stdin </dev/null: redirected input is not an interactive console.
    check(!console_prompt::isInteractiveConsole(), "console: input redirected from /dev/null is not interactive");

    std::printf("checks %d   failed %d\n%s\n", checks, failed, failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
