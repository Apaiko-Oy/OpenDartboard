#include "scorer/scorer.hpp"
#include "utils/args.hpp"
#include "utils/debug.hpp"
#include "utils/signals.hpp"
#include "utils/logging.hpp"
#include "utils/autocam.hpp"
#include "communication/score_token.hpp"
#include "communication/announce.hpp"
#include "utils/setup_view.hpp"
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

  // #1187: the score socket's credential, resolved before anything else opens. The
  // token is created on the first run and printed only by --show-token; the
  // detector refuses to start rather than serve a socket with no credential.
  string token_path = getArg(argc, argv, "--token-file", score_token::kDefaultPath);
  if (hasFlag(argc, argv, "--show-token"))
    debug::printTokenAndExit(token_path);

  // #1189: the board's label - what it is announced as and what the setup view
  // calls it - and the setup view itself, which prints the QR a phone scans.
  string label = getArg(argc, argv, "--label", announce::defaultLabel());
  bool listen = hasFlag(argc, argv, "--listen");
  if (hasFlag(argc, argv, "--setup"))
    setup_view::printAndExit(token_path, label, ScoreSocketSettings().port,
                             getArg(argc, argv, "--setup-address", ""), listen);

  // Parse command line arguments with defaults
  string model_path = getArg(argc, argv, "--model", "/usr/local/share/opendartboard/models/dart.param");
  bool useAuto = hasFlag(argc, argv, "--autocams");
  int width = getArg(argc, argv, "--width", 1280);
  int height = getArg(argc, argv, "--height", 720);
  int fps = getArg(argc, argv, "--fps", 15);
  bool debug_mode = hasFlag(argc, argv, "--debug") || hasFlag(argc, argv, "-d");
  bool quite_mode = hasFlag(argc, argv, "--quiet") || hasFlag(argc, argv, "-q");

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
    cams = getArgVector(argc, argv, "--cams", "/dev/video0,/dev/video1,/dev/video2");
  }

  debug::printConfig(width, height, fps, model_path, cams);

  // #1187: loopback unless --listen; the token is required either way.
  ScoreSocketSettings socket;
  socket.bind_address = listen ? "0.0.0.0" : "127.0.0.1";
  score_token::Resolved token = score_token::loadOrCreate(token_path);
  if (token.token.empty())
  {
    log_error("score socket: cannot read or create the token at " + token_path + ": " + token.error);
    return 1;
  }
  socket.token = token.token;
  debug::printSocketConfig(socket.bind_address, socket.port, token_path, token.created,
                           score_token::isReadableByOthers(token_path));

  // #1189: announced through the host's responder while - and only while - the
  // socket is on the network. A loopback-only start withdraws what an earlier
  // --listen run may have left, so the two states cannot disagree.
  string announce_dir = getArg(argc, argv, "--announce-dir", announce::kDefaultDir);
  if (listen)
  {
    announce::Outcome published = announce::publish(announce_dir, label, socket.port, version);
    if (published.done)
      log_info("announced as '" + label + "' (" + announce::kServiceType + ", port " + to_string(socket.port) +
               ") via " + published.detail);
    else
      log_warning("not announced: " + published.detail + " (--announce-dir names another directory)");
  }
  else
  {
    announce::Outcome withdrawn = announce::withdraw(announce_dir);
    if (withdrawn.done)
      log_info("not announced: loopback only; removed " + withdrawn.detail + " left by an earlier --listen run");
    else
      log_info("not announced: loopback only");
  }

  // Initialise the scorer with debug mode if requested
  Scorer scorer(model_path, width, height, fps, cams, debug_mode, detector_type, socket);

  // Register signal handlers with a lambda to stop the scorer
  signals::setupSignalHandlers([&scorer]()
                               { scorer.stop(); });

  // Start the scorer processing in background thread
  scorer.run();

  // #1189: the announcement does not outlive the socket.
  if (listen)
  {
    announce::Outcome withdrawn = announce::withdraw(announce_dir);
    log_info(withdrawn.done ? "announcement withdrawn: removed " + withdrawn.detail
                            : "announcement not withdrawn: " + withdrawn.detail);
  }

  // Best practice: wait for the scorer thread to finish
  return 0;
}