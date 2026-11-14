#pragma once
// turnaus#1995: reading back what OD_KEEP_WINDOWS (window_keep.hpp, turnaus#1991) wrote --
// one published dart's account, its pictures and the calibration its start scored through.
//
// This header only READS. What is done with a window once it is read is
// dart_processing::closeKeptWindow (the seat) and GeometryDetector::replayKeptWindows
// (the scorer, and the I1991 REPLAY line testers/i1991_grade.py --replay grades).
//
// THREE REFUSALS, EACH BY NAME, NONE SILENT.
//
//   a picture   A picture is named by its content (window_keep::pictureName), so a file
//               that is read back is hashed again and held to its own name. One that is
//               gone -- the bound took it, or a copy was cut short -- or that no longer
//               hashes to its name refuses the DART, naming the role and the picture.
//   an account  One that does not parse, or is of a version this reader was not written
//               for, refuses the dart.
//   the calibration
//               The file is cache/geometry_calibration.dat as the board's start wrote it:
//               a header that STATES the size of one record, then that many raw structs.
//               A build whose DartboardCalibration is another size is a build with another
//               layout, and every field past the first difference would be read from the
//               wrong offset and look plausible. So the record's own stated size is held
//               to this build's sizeof, and a difference is said with both numbers -- no
//               size is written in this file, because the struct is not this file's to
//               freeze (utils/cache.hpp makes the same refusal for a live start, #1330).

#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "window_keep.hpp"
#include "../calibration/geometry_calibration.hpp"
#include "../../../utils/cache.hpp"

namespace window_replay
{
    namespace fs = std::filesystem;

    struct Account
    {
        std::string reference;
        std::string calibration; // the name of the kept calibration; empty where the account says null
        window_keep::Window window;
        std::map<std::string, std::string> switches; // the OD_* environment the board ran under
        std::string refusal;     // empty when the account and every picture it names were read
        std::string refusal_slug;
    };

    /** The accounts under `dir`, in the order the darts were published (a reference is a ULID: it sorts by time). */
    inline std::vector<std::string> accountsIn(const std::string &dir)
    {
        std::vector<std::string> out;
        std::error_code ec;
        for (const auto &e : fs::directory_iterator(fs::path(dir) / "darts", ec))
        {
            if (e.path().extension() == ".json")
            {
                out.push_back(e.path().filename().string());
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    /** One picture, held to its name. Empty `name` is an honest empty picture (no working background on a clean board). */
    inline bool readPicture(const std::string &dir, const std::string &name, const char *role, int camera,
                            cv::Mat &out, std::string &refusal, std::string &slug)
    {
        out = cv::Mat();
        if (name.empty())
        {
            return true;
        }
        const std::string path = (fs::path(dir) / "pics" / (name + ".png")).string();
        cv::Mat read = cv::imread(path, cv::IMREAD_UNCHANGED);
        if (read.empty())
        {
            refusal = "camera " + std::to_string(camera) + "'s " + role + " picture " + name +
                      " is not on disk (the bound evicts oldest first, or the copy was cut short)";
            slug = "picture-missing";
            return false;
        }
        if (window_keep::pictureName(read) != name)
        {
            refusal = "camera " + std::to_string(camera) + "'s " + role + " picture " + name +
                      " reads back as " + window_keep::pictureName(read) + ": not the bytes that were kept";
            slug = "picture-altered";
            return false;
        }
        out = read;
        return true;
    }

    inline Account readAccount(const std::string &dir, const std::string &file)
    {
        Account a;
        a.reference = fs::path(file).stem().string();
        std::ifstream f(fs::path(dir) / "darts" / file, std::ios::binary);
        std::stringstream text;
        text << f.rdbuf();
        nlohmann::json j = nlohmann::json::parse(text.str(), nullptr, false);
        if (!f || j.is_discarded() || !j.is_object() || !j.contains("cameras") || !j["cameras"].is_array())
        {
            a.refusal = "the account does not parse";
            a.refusal_slug = "account-unreadable";
            return a;
        }
        if (j.value("v", 0) != 1)
        {
            a.refusal = "the account is version " + std::to_string(j.value("v", 0)) + " and this reader reads version 1";
            a.refusal_slug = "account-version";
            return a;
        }
        try
        {
            a.reference = j.value("reference", a.reference);
            a.calibration = j["calibration"].is_string() ? j["calibration"].get<std::string>() : std::string();
            a.window.window = j.value("window", -1L);
            a.window.board_state_before = j.value("board_state_before", 0);
            if (j.contains("switches") && j["switches"].is_object())
            {
                for (auto it = j["switches"].begin(); it != j["switches"].end(); ++it)
                {
                    a.switches[it.key()] = it.value().is_string() ? it.value().get<std::string>() : std::string();
                }
            }
            for (const auto &c : j["cameras"])
            {
                window_keep::Camera k;
                const int camera = c.value("camera", (int)a.window.cameras.size() + 1);
                const auto name = [&](const char *role)
                {
                    return c.contains(role) && c[role].is_string() ? c[role].get<std::string>() : std::string();
                };
                k.previous_state = c.value("previous_state", 0);
                k.previous_board_change = c.value("previous_board_change", -1);
                k.reversion_memory = c.value("reversion_memory", 0);
                k.frames = c.value("frames", 0);
                if (c.contains("board") && c["board"].is_object())
                {
                    const auto &b = c["board"];
                    k.board_known = true;
                    k.board = cv::RotatedRect(cv::Point2f(b.value("cx", 0.0f), b.value("cy", 0.0f)),
                                              cv::Size2f(b.value("w", 0.0f), b.value("h", 0.0f)), b.value("angle", 0.0f));
                }
                if (c.contains("reported_tips") && c["reported_tips"].is_array())
                {
                    for (const auto &t : c["reported_tips"])
                    {
                        k.reported_tips.push_back(cv::Point2f(t.at(0).get<float>(), t.at(1).get<float>()));
                    }
                }
                if (a.refusal.empty())
                {
                    readPicture(dir, name("settled"), "settled", camera, k.settled, a.refusal, a.refusal_slug) &&
                        readPicture(dir, name("clean"), "clean", camera, k.clean, a.refusal, a.refusal_slug) &&
                        readPicture(dir, name("fresh"), "fresh", camera, k.fresh, a.refusal, a.refusal_slug);
                }
                a.window.cameras.push_back(k);
            }
        }
        catch (const std::exception &e)
        {
            a.refusal = std::string("the account does not read: ") + e.what();
            a.refusal_slug = "account-unreadable";
        }
        return a;
    }

    struct KeptCalibration
    {
        std::vector<DartboardCalibration> cameras;
        std::string refusal; // empty when read
    };

    /**
     * The kept calibration `name`, or why not. The record's OWN stated size against this
     * build's struct: nothing here says what either number is.
     */
    inline KeptCalibration readCalibration(const std::string &dir, const std::string &name)
    {
        KeptCalibration out;
        const std::string said = "the kept calibration " + name + ".dat ";
        if (name.empty())
        {
            out.refusal = "the account names no calibration (the board had written none when the dart was kept)";
            return out;
        }
        std::ifstream f(fs::path(dir) / "calibration" / (name + ".dat"), std::ios::binary);
        if (!f)
        {
            out.refusal = said + "is not on disk";
            return out;
        }
        cache::geometry::FileHeader header;
        f.read((char *)&header, sizeof(header));
        if (!f.good() || header.magic != cache::geometry::MAGIC)
        {
            out.refusal = said + "is not a calibration file (its first bytes are not the cache's signature)";
            return out;
        }
        if (header.version != cache::geometry::VERSION)
        {
            out.refusal = said + "is file version " + std::to_string(header.version) + " and this build reads version " +
                          std::to_string(cache::geometry::VERSION);
            return out;
        }
        if (header.record_bytes != (uint32_t)sizeof(DartboardCalibration))
        {
            out.refusal = said + "states records of " + std::to_string(header.record_bytes) +
                          " bytes and this build's DartboardCalibration is " + std::to_string(sizeof(DartboardCalibration)) +
                          " bytes: another layout of the struct, so it is refused rather than read at the wrong offsets";
            return out;
        }
        if (header.count == 0 || header.count > 16)
        {
            out.refusal = said + "states " + std::to_string(header.count) + " cameras";
            return out;
        }
        out.cameras.resize(header.count);
        f.read((char *)out.cameras.data(), (std::streamsize)(sizeof(DartboardCalibration) * header.count));
        if (!f.good())
        {
            out.cameras.clear();
            out.refusal = said + "ends before its " + std::to_string(header.count) + " records do";
        }
        return out;
    }
} // namespace window_replay
