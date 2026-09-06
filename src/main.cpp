#include "scorer/scorer.hpp"
#include "utils/args.hpp"
#include "utils/debug.hpp"
#include "utils/signals.hpp"
#include "utils/logging.hpp"
#include "utils/autocam.hpp"
#include "utils/od_paths.hpp"
#include "communication/turnaus_client.hpp"
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

  // #822: where this board posts what it sees, and where it keeps what it was given.
  //
  // The address, in order of precedence: --turnaus, then OD_TURNAUS_URL, then the
  // base_url the pairing wrote into the credential file, then the default. A board is
  // paired once and the address it was paired against is the address it keeps, so a
  // pub PC needs no flag and no configuration file of its own after the pairing.
  TurnausConfig turnaus_config;
  turnaus_config.credentials_path =
      getArg(argc, argv, "--credentials", od_paths::join(od_paths::configDir(), "credentials.json"));
  turnaus_config.allow_plaintext = hasFlag(argc, argv, "--allow-plaintext");

  string configured_url = getArg(argc, argv, "--turnaus", string(""));
  if (configured_url.empty())
  {
    configured_url = od_paths::env("OD_TURNAUS_URL");
  }
  if (configured_url.empty())
  {
    // What the last pairing was made against, if there was one.
    string raw;
    if (od_paths::readFile(turnaus_config.credentials_path, raw))
    {
      size_t at = raw.find("\"base_url\"");
      if (at != string::npos)
      {
        size_t open_quote = raw.find('"', raw.find(':', at) + 1);
        size_t close_quote = open_quote == string::npos ? string::npos : raw.find('"', open_quote + 1);
        if (close_quote != string::npos)
        {
          configured_url = raw.substr(open_quote + 1, close_quote - open_quote - 1);
        }
      }
    }
  }
  if (configured_url.empty())
  {
    configured_url = "https://turnaus.fi";
  }
  turnaus_config.base_url = configured_url;

  // --pair exchanges a code for a credential and stops. It opens no camera and starts
  // no detector: a board being paired is a board somebody is standing in front of with
  // a six-digit code that expires in ten minutes, not a board that needs to calibrate.
  string pairing_code = getArg(argc, argv, "--pair", string(""));
  if (!pairing_code.empty())
  {
    TurnausClient client(turnaus_config);
    return client.pair(pairing_code) ? 0 : 1;
  }

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

  // #892: the client is built and STARTED before the Scorer, so the beat covers the
  // startup it used to pass over in silence. Opening three cameras and calibrating on
  // thirty averaged frames is seconds of a board being up and not yet able to see, and
  // INITIALISING and CALIBRATING are two of the five words a board may say precisely so
  // that interval is not indistinguishable from a machine nobody switched on. start()
  // is idempotent, so Scorer::run()'s own call is unchanged and harmless.
  std::unique_ptr<TurnausClient> turnaus(new TurnausClient(turnaus_config));
  turnaus->start();

  // Initialise the scorer with debug mode if requested
  Scorer scorer(model_path, width, height, fps, cams, debug_mode, detector_type);
  scorer.attachTurnaus(std::move(turnaus));

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