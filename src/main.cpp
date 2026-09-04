#include "scorer/scorer.hpp"
#include "utils/args.hpp"
#include "utils/debug.hpp"
#include "utils/signals.hpp"
#include "utils/logging.hpp"
#include "utils/autocam.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

using namespace std;

// version string for the application
const string version = APP_VERSION;

int main(int argc, char **argv)
{
  // Check for help or version flags first
  if (hasFlag(argc, argv, "--version"))
    debug::printVersionAndExit(version);
  if (hasFlag(argc, argv, "--help"))
    debug::printHelpAndExit();

  // Parse command line arguments with defaults
#ifdef _WIN32
  string model_path = getArg(argc, argv, "--model", "models/dart.param");
#else
  string model_path = getArg(argc, argv, "--model", "/usr/local/share/opendartboard/models/dart.param");
#endif
  bool useAuto = hasFlag(argc, argv, "--autocams");
  int width = getArg(argc, argv, "--width", 1280);
  int height = getArg(argc, argv, "--height", 720);
  int fps = getArg(argc, argv, "--fps", 15);
  bool debug_mode = hasFlag(argc, argv, "--debug") || hasFlag(argc, argv, "-d");
  bool quite_mode = hasFlag(argc, argv, "--quiet") || hasFlag(argc, argv, "-q");
  // #824: --setup. Aiming a camera at a board cannot be done blind, and #805 closed the
  // only way this program has ever had to look. This reopens one view, on 127.0.0.1 and
  // only when asked for, which answers both halves of what was wrong: nothing is
  // reachable from the network, and nothing runs unless somebody sat down and asked.
  bool setup_mode = hasFlag(argc, argv, "--setup");

  // Replace boolean flag with detector type string
  string detector_type = getArg(argc, argv, "--detector", "geometry");

  // set log level based on debug mode
  if (debug_mode)
  {
    logging::setLogLevel(logging::LogLevel::DEBUG); // Show everything
    log_info("Debug mode enabled - showing all log messages");
  }
  else if (quite_mode)
  {
    logging::setLogLevel(logging::LogLevel::ERROR); // Only errors
    log_info("Quiet mode enabled - showing only error messages");
  }

  // Print startup and configuration information
  debug::printStartup("OpenDartboard", version);

  // setup cams
  vector<string> cams;
  if (useAuto)
  {
    cams = autocam::detectAndLock(/*max*/ 3, width, height, fps);
  }
  else
  {
#ifdef _WIN32
    // Media Foundation has no filesystem name for a camera, so a device is an index.
    cams = getArgVector(argc, argv, "--cams", "0,1,2");
#else
    cams = getArgVector(argc, argv, "--cams", "/dev/video0,/dev/video1,/dev/video2");
#endif
  }

  debug::printConfig(width, height, fps, model_path, cams);

  // #824: what --setup implies, decided and recorded here because it is a decision
  // rather than a mechanism.
  //
  //   IT PUBLISHES NOTHING. A person aiming a camera is pointing it at the floor, at
  //   the ceiling and at their own hand, and every dart-shaped thing that crosses the
  //   frame while they do is a score as far as the pipeline is concerned. Publishing
  //   those to a paired Turnaus would put fiction in somebody's leg. So --setup does
  //   not construct the WebSocket service at all: no score leaves the process, and the
  //   0.0.0.0 listener on 13520 that every build otherwise opens is not opened either.
  //   That is a consequence and not the reason; 13520 stays open in an ordinary run and
  //   closing it is still #805's question, not this one's.
  //
  //   IT SHOWS THE RAW FEEDS AND DRAWS NOTHING ON THEM. "Is the camera pointed at the
  //   board" and "does the calibration agree" are two questions and only the first one
  //   can be answered no other way. The overlay needs the board to have been *found*,
  //   which is exactly what has not happened yet in the state a person is in while
  //   aiming, and reaching for it would mean a release build asking the detector for
  //   calibration state it may not have. The debug build already draws six views for
  //   the second question. Worth revisiting when three real cameras exist.
  //
  //   IT DOES NOT IMPLY --debug. Debug mode writes hundreds of images to disk and, in a
  //   dev build, opens the other seven listeners. Setup is one view.
  //
  // Initialise the scorer with debug mode if requested
  Scorer scorer(model_path, width, height, fps, cams, debug_mode, detector_type, setup_mode);

  // Register signal handlers with a lambda to stop the scorer.
  // #824: setup gets the cooperative handler, so Ctrl+C leaves the capture loop, unwinds
  // main, and lets ~Scorer release the cameras. The shipped handler calls exit(signal)
  // and no destructor runs; on Windows an unreleased Media Foundation handle is less
  // forgiving than V4L2, and a setup mode you have to kill is a bad first experience.
  // Nothing else's exit path moves — giving the whole program a real exit is #805's
  // work, and the shutdown study says why.
  if (setup_mode)
  {
    signals::setupCooperativeSignalHandlers([&scorer]()
                                            { scorer.stop(); });
  }
  else
  {
    signals::setupSignalHandlers([&scorer]()
                                 { scorer.stop(); });
  }

  // Start the scorer processing in background thread
  scorer.run();

  // Best practice: wait for the scorer thread to finish
  return 0;
}