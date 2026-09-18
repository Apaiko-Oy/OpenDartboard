#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <iostream>
#include "utils.hpp"
#include "detector/geometry/calibration/geometry_calibration.hpp"

using namespace cv;
using namespace std;

namespace cache
{
    namespace geometry
    {

        // #1330: A CALIBRATION IS STILL WRITTEN AND READ AS RAW BYTES, AND THE STRUCT IS
        // HELD TO DESERVING THAT.
        //
        // The two static_asserts under DartboardCalibration are the write half: nothing
        // in the struct may own memory, so there is no pointer for the fwrite below to
        // put in the file. This file's own header is the read half.
        //
        // `record_bytes` is new and it is what the old TODO here was reaching for. The
        // on-disk record IS sizeof(DartboardCalibration), so every change to that struct
        // is a change to this file format -- and `version` never moved when #1317, #1318
        // and #1321 each changed it, because nothing connected the two. A number nobody
        // remembers to bump is not a version. So the writer records the size it wrote and
        // the reader refuses a file whose records are a different size, which needs
        // nobody to remember anything and catches a member added, removed or retyped.
        //
        // It is not a substitute for the ownership assert and the reverse is also true,
        // which is worth one sentence because it is the reason both exist. A std::string
        // is 32 bytes on this toolchain and so is a char[32]: swapping one for the other
        // keeps `record_bytes` identical and turns the file into pointers. And a member
        // added that owns nothing passes the assert while making every old file
        // unreadable. One guards what is in the bytes, the other that they are the same
        // bytes.
        //
        // The frame size the calibration was taken at is checked too, against the frames
        // THIS run will score with -- the rest of the old TODO. A cached bull center is a
        // pixel coordinate, and read back at another resolution it points somewhere else
        // on the board.

        static const uint32_t MAGIC = 0x42554C4C; // "BULL" in ASCII
        // 2 since #1330: the header gained a field, so a v1 file cannot be read by this.
        static const uint32_t VERSION = 2;

        struct FileHeader
        {
            uint32_t magic = MAGIC;     // File signature
            uint32_t version = VERSION; // Version for future compatibility
            uint32_t record_bytes = (uint32_t)sizeof(DartboardCalibration); // One calibration, on disk
            uint32_t count = 0;         // Number of calibrations
        };

        /**
         * Whether this start may score on a calibration a previous start measured.
         *
         * #1330: OFF, and it is the flag rather than the code that had to be added,
         * because the read cannot be turned on for everybody and the measurement is in
         * the harnesses. The file is named by nothing but the working directory: it does
         * not say which cameras it was taken of, and it cannot, because a camera is a
         * path or an index and both of those name a different device tomorrow. So a
         * second start in the same directory inherits the first one's board.
         *
         * Measured, restoring the read unconditionally: testers/i1318_run.sh starts the
         * detector five times in one directory with five different --cams lists, and runs
         * two to five all scored through the geometry the FIRST one measured -- including
         * the run whose cameras are three copies of a wall. That is #1318's fault exactly,
         * the wrong camera scoring through another camera's perspective, reached through
         * the cache instead of through the device list, and #899's harness goes the same
         * way. A default that does that cannot be argued for by eight and a half seconds.
         *
         * So the board looks at the picture every start unless somebody who knows their
         * cameras have not moved says --reuse-calibration, and it says on every such start
         * that it did not look. What would let this become the default is a file that can
         * say whose geometry it is; that is a piece of work and not a line.
         */
        inline bool &calibrationMayBeReused()
        {
            static bool allowed = false;
            return allowed;
        }

        inline void allowReuse(bool allowed)
        {
            calibrationMayBeReused() = allowed;
        }

        // Generate standard filename for calibration cache
        inline string generateFilename()
        {
            // Create cache directory if it doesn't exist. ensureDirectory reports the
            // failure; the caller opens the returned path and reports that too.
            (void)odfs::ensureDirectory("cache");
            return "cache/geometry_calibration.dat";
        }

        /**
         * The calibrations a previous run of this board wrote, or an empty vector.
         *
         * #1330 gave this a parameter and restored the call that had been commented out
         * in geometry_detector.cpp since before any of this. A cache nothing reads is a
         * write-only file: it costs the eight and a half seconds of calibration it exists
         * to save on every single start, and it cannot justify the guarantee the struct is
         * now held to. So the call is live and the path is taken -- but only when the
         * operator asked for it (see calibrationMayBeReused above) and only when the file
         * says it was written by this build, of this struct, for this many cameras, at
         * these frame sizes, by a run in which something was looking at a dartboard.
         *
         * `frames` are the frames this run would otherwise calibrate on, and every
         * question asked of the file is asked against them. A cached calibration is a set
         * of pixel coordinates in a particular camera's particular frame; handed to a
         * different number of cameras or a different resolution it is not a worse
         * calibration, it is a confident wrong one, and score_processing reads
         * calibrations[i] by position.
         */
        inline vector<DartboardCalibration> load(const vector<Mat> &frames)
        {
            if (!calibrationMayBeReused())
            {
                log_debug("Not reading the calibration cache: this start will look at the board. "
                          "--reuse-calibration scores on what the last start measured.");
                return {};
            }

            string filename = generateFilename();
            try
            {
                ifstream file(filename, ios::binary);
                if (!file)
                {
                    log_debug("No calibration file found will create new one: " + filename);
                    return {}; // Return empty vector
                }

                // Read and verify header
                FileHeader header;
                file.read((char *)&header, sizeof(header));

                if (header.magic != MAGIC)
                {
                    log_warning("Invalid calibration file format");
                    return {};
                }

                if (header.version != VERSION)
                {
                    log_warning("Incompatible calibration file version: " + to_string(header.version) + ". Expected " + to_string(VERSION));
                    return {};
                }

                if (header.record_bytes != (uint32_t)sizeof(DartboardCalibration))
                {
                    // The struct changed since this file was written. Every field after
                    // the first difference is at the wrong offset, and none of it would
                    // look wrong: this is the one refusal that costs nothing and is worth
                    // everything.
                    log_warning("Calibration cache was written by a different build: its records are " +
                                to_string(header.record_bytes) + " bytes and this one's are " +
                                to_string(sizeof(DartboardCalibration)) + ". Recalibrating.");
                    return {};
                }

                if (header.count != (uint32_t)frames.size())
                {
                    log_warning("Calibration cache holds " + to_string(header.count) +
                                " cameras and this run has " + to_string(frames.size()) +
                                ". Recalibrating.");
                    return {};
                }

                // Read calibration data
                vector<DartboardCalibration> calibrations(header.count);
                file.read((char *)calibrations.data(), sizeof(DartboardCalibration) * header.count);

                if (!file.good())
                {
                    log_error("Failed to read calibration data");
                    return {};
                }

                // The frame size each calibration was measured at, against the frame that
                // camera is producing now. A resolution change is the crash the TODO that
                // used to be at the top of this file was written about.
                for (size_t i = 0; i < calibrations.size(); i++)
                {
                    if (frames[i].empty())
                    {
                        continue; // No frame from this camera; its slot is kept either way.
                    }
                    if (calibrations[i].capture_width != frames[i].cols ||
                        calibrations[i].capture_height != frames[i].rows)
                    {
                        log_warning("Calibration cache was taken at " +
                                    to_string(calibrations[i].capture_width) + "x" +
                                    to_string(calibrations[i].capture_height) + " for camera " +
                                    to_string(i + 1) + " and that camera is now producing " +
                                    to_string(frames[i].cols) + "x" + to_string(frames[i].rows) +
                                    ". Recalibrating.");
                        return {};
                    }
                }

                // A cached run in which no camera could see the board is a cached failure.
                // Trusting it would leave a board that was once pointed at a wall unable
                // to calibrate again without somebody deleting a file they do not know about.
                bool any_sees_board = false;
                for (const auto &calibration : calibrations)
                {
                    any_sees_board = any_sees_board || calibration.sees_board;
                }
                if (!any_sees_board)
                {
                    log_warning("Calibration cache holds no camera that was looking at a dartboard. Recalibrating.");
                    return {};
                }

                log_info("Loaded " + to_string(header.count) + " calibrations from cache");
                return calibrations;
            }
            catch (const exception &e)
            {
                log_error("Error loading calibration: " + string(e.what()));
                return {}; // Return empty vector on error
            }
        }

        // Save calibration data to binary file - calibrations only
        inline bool save(const vector<DartboardCalibration> &calibrations)
        {
            string filename = generateFilename();
            try
            {
                ofstream file(filename, ios::binary);
                if (!file)
                {
                    log_error("Failed to open file for writing: " + filename);
                    return false;
                }

                // Write header. record_bytes is filled in by its member initialiser from
                // the size of the struct this build compiled, which is the number the
                // reader compares against its own.
                FileHeader header;
                header.count = (uint32_t)calibrations.size();
                file.write((char *)&header, sizeof(header));

                // Write calibration data
                file.write((char *)calibrations.data(), sizeof(DartboardCalibration) * calibrations.size());

                if (!file.good())
                {
                    log_error("Failed to write calibration data");
                    return false;
                }

                log_debug("Saved " + to_string(calibrations.size()) + " calibrations to cache");
                return true;
            }
            catch (const exception &e)
            {
                log_error("Error saving calibration: " + string(e.what()));
                return false;
            }
        }

        // Save background frames as individual image files
        inline bool saveBackgroundFrames(const vector<Mat> &background_frames)
        {
            try
            {
                if (!odfs::ensureDirectory("cache/backgrounds"))
                {
                    return false;
                }

                for (size_t i = 0; i < background_frames.size(); i++)
                {
                    if (!background_frames[i].empty())
                    {
                        string filename = "cache/backgrounds/camera_" + to_string(i) + "_background.jpg";
                        if (!imwrite(filename, background_frames[i]))
                        {
                            log_error("Failed to save background frame for camera " + to_string(i));
                            return false;
                        }
                    }
                }

                log_info("Saved " + to_string(background_frames.size()) + " background frames");
                return true;
            }
            catch (const exception &e)
            {
                log_error("Error saving background frames: " + string(e.what()));
                return false;
            }
        }

        // Load background frames from individual image files
        inline vector<Mat> loadBackgroundFrames()
        {
            vector<Mat> background_frames;

            try
            {
                // Try to load background frames (check for up to 3 cameras)
                for (int i = 0; i < 3; i++)
                {
                    string filename = "cache/backgrounds/camera_" + to_string(i) + "_background.jpg";
                    Mat frame = imread(filename);

                    if (!frame.empty())
                    {
                        background_frames.push_back(frame);
                        log_debug("Loaded background frame for camera " + to_string(i));
                    }
                    else
                    {
                        // Stop when we can't find more consecutive frames
                        break;
                    }
                }

                if (!background_frames.empty())
                {
                    log_info("Loaded " + to_string(background_frames.size()) + " background frames");
                }

                return background_frames;
            }
            catch (const exception &e)
            {
                log_error("Error loading background frames: " + string(e.what()));
                return vector<Mat>();
            }
        }
    }
}