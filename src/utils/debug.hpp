#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <opencv2/opencv.hpp>
#include "logging.hpp"
#include "communication/score_token.hpp"
#include "communication/turnaus_address.hpp"

namespace debug
{

    // Print application startup banner
    inline void printStartup(const std::string &appName, const std::string &version)
    {
        std::cout << "=====================================\n";
        std::cout << "  " << appName << " v" << version << " starting...\n";
        std::cout << "=====================================\n";
    }

    // Print configuration details
    inline void printConfig(int width, int height, int fps,
                            const std::string &model,
                            const std::vector<std::string> &cams)
    {
        std::cout << "Configuration:\n";
        std::cout << "  - Resolution: " << width << "x" << height << "\n";
        std::cout << "  - FPS: " << fps << "\n";
        std::cout << "  - Model: " << model << "\n";
        std::cout << "  - Cameras (" << cams.size() << "):\n";
        for (size_t i = 0; i < cams.size(); i++)
        {
            std::cout << "      " + std::to_string(i + 1) + ": " + cams[i] << std::endl;
        }
        std::cout << "-------------------------------------" << std::endl;
    }

    // #1187: where the score socket is and where its token is kept. The token itself
    // is never printed here; --show-token is the one place it is.
    inline void printSocketConfig(const std::string &bind, int port, const std::string &token_path,
                                  bool token_created, bool token_readable_by_others)
    {
        bool loopback = bind == "127.0.0.1";
        std::cout << "Score socket:\n";
        std::cout << "  - Listening: ws://" << bind << ":" << port << "/scores"
                  << (loopback ? "  (loopback only; --listen opens it on the network)" : "  (open on the network)") << "\n";
        std::cout << "  - Token: " << token_path << (token_created ? "  (created now, mode 0600)" : "")
                  << "; a subscriber presents it as ?token=...; --show-token prints it\n";
        if (token_readable_by_others)
        {
            std::cout << "  - WARNING: " << token_path << " is readable by other users; chmod 600 it\n";
        }
        std::cout << "-------------------------------------" << std::endl;
    }

    // #1187: print the score socket token and exit, creating it on the first run.
    inline void printTokenAndExit(const std::string &token_path)
    {
        score_token::Resolved token = score_token::loadOrCreate(token_path);
        if (token.token.empty())
        {
            std::cerr << "cannot read or create the score token at " << token_path << ": " << token.error << std::endl;
            exit(1);
        }
        std::cout << token.token << std::endl;
        exit(0);
    }

    // Print version information and exit
    inline void printVersionAndExit(const std::string &version)
    {
        std::cout << "OpenDartboard runtime version: " << version << std::endl;
        exit(0);
    }

    // Print help message and exit
    inline void printHelpAndExit()
    {
        std::cout << "Usage: opendartboard [options]\n";
        std::cout << "Options:\n";
        std::cout << "  --model <path>       Path to the AI model file (default: /usr/local/share/opendartboard/models/dart.param)\n";
        std::cout << "  --cams <cameras>     Comma-separated list of camera devices. Given here they are opened in this\n";
        std::cout << "                       order and nothing is probed or asked. Without it, every video device on the\n";
        std::cout << "                       machine is looked through and the first 3 that can see the dartboard are used\n";
        std::cout << "  --autocams           Automatically detect and lock up to 3 cameras that negotiate MJPG. This asks\n";
        std::cout << "                       what a camera can do, not where it points; the default start asks both\n";
        std::cout << "  --width <width>      Frame width (default: 1280)\n";
        std::cout << "  --height <height>    Frame height (default: 720)\n";
        std::cout << "  --fps <fps>          Frames per second (default: 15)\n";
        std::cout << "  --detector <type>    Detector type: geometry, ai, custom (default: geometry)\n";
        std::cout << "  --debug, -d          Enable debug mode (saves frames to debug_frames/ directory)\n";
        std::cout << "  --quiet, -q          Quiet mode (only show errors)\n";
        std::cout << "  --log-file <path>    Also append the log to a file (off unless asked for; --debug implies debug_frames/opendartboard.log)\n";
        std::cout << "  --listen             Open the score socket on the network (0.0.0.0:13520); loopback only without it\n";
        std::cout << "  --show-token         Print the token a subscriber presents as ws://.../scores?token=..., creating it if absent, and exit\n";
        std::cout << "  --token-file <path>  Where the token is kept (default: score_token in the working directory, mode 0600)\n";
        std::cout << "  --label <name>       What the board is announced as and called in --setup (default: the hostname)\n";
        std::cout << "  --announce-dir <dir> Avahi services directory the announcement is written to while --listen is on\n";
        std::cout << "                       (default: /etc/avahi/services); nothing is announced without --listen\n";
        std::cout << "  --setup              Print the setup view - a QR code a phone scans to subscribe, encoding\n";
        std::cout << "                       ws://<address>:13520/scores?token=<token> - and exit; the token is inside the QR only\n";
        std::cout << "  --setup-address <a>  The address the QR names (default: this board's first non-loopback IPv4 address)\n";
        std::cout << "  --pair <code>        Exchange a Turnaus pairing code for a credential, keep it, and exit\n";
        std::cout << "  --pair-contest <code>\n";
        std::cout << "                       Exchange a Casual Contest pairing code, keep it, and exit. Binds this\n";
        std::cout << "                       board to one Casual Contest for that evening and to nothing else: no\n";
        std::cout << "                       Station and no Organisation. A club pairing already on the board is\n";
        std::cout << "                       kept, and the Contest binding wins while it lasts\n";
        std::cout << "  --turnaus <url>      The Turnaus this board pairs with and posts to. Resolved in order: --turnaus,\n";
        std::cout << "                       then OD_TURNAUS_URL, then the base_url the last pairing stored in the\n";
        std::cout << "                       credential file, then the default: " << turnaus_address::kDefault << "\n";
        std::cout << "  --credentials <path> Where the Turnaus credential is kept (default: credentials.json in the config directory)\n";
        std::cout << "  --allow-plaintext    Permit an http:// Turnaus address (a loopback or a lab only)\n";
        std::cout << "                       (OD_ALLOW_PLAINTEXT=1 is the same, for a start with no command line)\n";
        std::cout << "  (no flag)            Started in an interactive console with no credential, asks for a\n";
        std::cout << "                       six-digit pairing code, tries the club's door then a Casual Contest's,\n";
        std::cout << "                       and goes straight on to scoring; asks again if the credential is refused\n";
        std::cout << "  --version            Show version information\n";
        std::cout << "  --help               Show this help message\n";
        exit(0);
    }

    // Create combined calibration visualization showing all processing steps
    inline cv::Mat createCombinedCalibrationVisualization(int numCameras, const std::string &baseDir = "debug_frames")
    {
        std::vector<std::string> stepNames = {
            "1. ROI Processing",
            "2. Color Processing",
            "3. Bull Processing",
            "4. Mask Processing",
            // temporarily disabled contour processing
            // "5. Contour Processing",
            "6. Ellipse Processing",
            "7. Wire Processing",
            "8. Orientation Processing",
            "9. Final Calibration"};

        std::vector<std::string> stepPaths = {
            "roi_processing/roi_frame_",
            "color_processing/red_green_frame_",
            "bull_processing/bull_detection_",
            "mask_processing/mask_grid_",
            // temporarily disabled contour processing
            // "contour_processing/contours_",
            "ellipse_processing/ellipse_result_",
            "wire_processing/ensemble_average_result_",
            "orientation_processing/orientation_result_",
            "geometry_calibration/calibration_camera_"};

        // Load first image to get dimensions
        cv::Mat firstImage = cv::imread(baseDir + "/" + stepPaths[0] + "0.jpg");
        if (firstImage.empty())
        {
            log_error("Could not load debug images for combined visualization");
            return cv::Mat::zeros(480, 640, CV_8UC3);
        }

        int imgWidth = firstImage.cols;
        int imgHeight = firstImage.rows;
        int labelHeight = 40;
        int padding = 10;

        // Calculate combined image dimensions
        int combinedWidth = (imgWidth + padding) * numCameras + padding;
        int combinedHeight = (imgHeight + labelHeight + padding) * stepNames.size() + labelHeight + padding;

        cv::Mat combinedImage = cv::Mat::zeros(combinedHeight, combinedWidth, CV_8UC3);
        combinedImage.setTo(cv::Scalar(50, 50, 50)); // Dark gray background

        // Add title
        cv::putText(combinedImage, "OpenDartboard Calibration Pipeline - All Cameras",
                    cv::Point(20, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2);

        // Add camera headers
        for (int cam = 0; cam < numCameras; cam++)
        {
            int x = padding + cam * (imgWidth + padding);
            int y = labelHeight + 20;
            cv::putText(combinedImage, "Camera " + std::to_string(cam),
                        cv::Point(x + imgWidth / 2 - 50, y), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 255, 255), 2);
        }

        // Process each step
        for (size_t step = 0; step < stepNames.size(); step++)
        {
            int stepY = labelHeight + padding + step * (imgHeight + labelHeight + padding);

            // Add step label
            cv::putText(combinedImage, stepNames[step],
                        cv::Point(20, stepY + labelHeight - 5), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2);

            // Add images for each camera
            for (int cam = 0; cam < numCameras; cam++)
            {
                std::string imagePath = baseDir + "/" + stepPaths[step] + std::to_string(cam) + ".jpg";
                cv::Mat stepImage = cv::imread(imagePath);

                if (!stepImage.empty())
                {
                    int x = padding + cam * (imgWidth + padding);
                    int y = stepY + labelHeight;

                    // Ensure the image fits in the allocated space
                    if (stepImage.cols != imgWidth || stepImage.rows != imgHeight)
                    {
                        cv::resize(stepImage, stepImage, cv::Size(imgWidth, imgHeight));
                    }

                    cv::Rect roi(x, y, imgWidth, imgHeight);
                    if (roi.x + roi.width <= combinedImage.cols && roi.y + roi.height <= combinedImage.rows)
                    {
                        stepImage.copyTo(combinedImage(roi));
                    }
                }
                else
                {
                    // Draw placeholder for missing image
                    int x = padding + cam * (imgWidth + padding);
                    int y = stepY + labelHeight;
                    cv::Rect roi(x, y, imgWidth, imgHeight);
                    cv::rectangle(combinedImage, roi, cv::Scalar(100, 100, 100), -1);
                    cv::putText(combinedImage, "Missing Image",
                                cv::Point(x + 20, y + imgHeight / 2), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 1);

                    log_warning("Missing image: " + imagePath);
                }
            }
        }

        return combinedImage;
    }

    // Helper function to create combined frame with label
    inline cv::Mat createCombinedFrame(const vector<Mat> &frames, const string &label)
    {
        if (frames.empty())
            return cv::Mat();

        int numCams = min(3, (int)frames.size());

        // #798: take the geometry from the first slot that has a frame, not from slot 0,
        // which may be a camera that failed to read.
        const Mat *reference = nullptr;
        for (int i = 0; i < numCams; i++)
        {
            if (!frames[i].empty())
            {
                reference = &frames[i];
                break;
            }
        }
        if (!reference)
            return cv::Mat();

        int frameWidth = reference->cols;
        int frameHeight = reference->rows;

        // Horizontal layout: [Cam0][Cam1][Cam2]
        Mat combined(frameHeight, frameWidth * numCams, reference->type());

        for (int i = 0; i < numCams; i++)
        {
            if (!frames[i].empty())
            {
                Rect roi(i * frameWidth, 0, frameWidth, frameHeight);
                frames[i].copyTo(combined(roi));

                // Add camera and stream labels
                putText(combined, "Camera " + to_string(i),
                        Point(i * frameWidth + 10, 30),
                        FONT_HERSHEY_SIMPLEX, 0.8, Scalar(0, 255, 0), 2);
                putText(combined, label,
                        Point(i * frameWidth + 10, 60),
                        FONT_HERSHEY_SIMPLEX, 0.6, Scalar(255, 255, 255), 2);
            }
            else
            {
                // put black frame
                Rect roi(i * frameWidth, 0, frameWidth, frameHeight);
                Mat blackFrame = Mat::zeros(frameHeight, frameWidth, reference->type());
                blackFrame.copyTo(combined(roi));
            }
        }

        return combined;
    }

} // namespace debug
