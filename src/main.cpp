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

  // Replace boolean flag with detector type string
  string detector_type = getArg(argc, argv, "--detector", "geometry");

  // i817: file logging is off unless it is asked for. It ships off because the only
  // thing that ever set it was the header's own default, its default path is inside
  // debug_frames/, and a released binary that appends a file to whatever directory it
  // was started in is a surprise nobody asked for. --debug asks for it, next to the
  // debug images it already writes; --log-file <path> asks for it on its own.
  string log_file = getArg(argc, argv, "--log-file", string(""));

  // set log level based on debug mode
  if (debug_mode)
  {
    logging::setLogLevel(logging::LogLevel::DEBUG); // Show everything
    if (log_file.empty())
    {
      log_file = "debug_frames/opendartboard.log";
    }
    log_info("Debug mode enabled - showing all log messages");
  }
  else if (quite_mode)
  {
    logging::setLogLevel(logging::LogLevel::ERROR); // Only errors
    log_info("Quiet mode enabled - showing only error messages");
  }

  if (!log_file.empty())
  {
    logging::setFileLogging(true, log_file);
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

  // Initialise the scorer with debug mode if requested
  Scorer scorer(model_path, width, height, fps, cams, debug_mode, detector_type);

  // #825: the handler records the signal and returns; Scorer::run()'s loop is what
  // observes it. There is no callback here any more, because a callback called from a
  // signal context is a callback that runs while the interrupted thread holds locks.
  signals::setupSignalHandlers();

  // Run the scorer on this thread. It returns when the loop sees the shutdown flag.
  scorer.run();

  // #825: and then this function ends normally, which is the whole point. ~Scorer runs
  // here -- joining the WebSocket worker, stopping the HTTP server and releasing the
  // cameras -- because main is unwound rather than skipped by exit().
  return 0;
}