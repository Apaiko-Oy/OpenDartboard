#include "scorer/scorer.hpp"
#include "utils/args.hpp"
#include "utils/cache.hpp"
#include "utils/geometry_fault.hpp"
#include "utils/debug.hpp"
#include "utils/signals.hpp"
#include "utils/logging.hpp"
#include "utils/autocam.hpp"
// #1318: which cameras a start with no --cams opens, decided by looking through them.
#include "utils/board_cameras.hpp"
#include "utils/camera_choice.hpp"
#include "communication/score_token.hpp"
#include "communication/announce.hpp"
// #1473: one board per host, claimed on a lock before anything opens.
#include "communication/one_board.hpp"
#include "utils/setup_view.hpp"
#include "utils/od_paths.hpp"
#include "communication/turnaus_client.hpp"
#include "communication/pairing_prompt.hpp"
#include "utils/console_prompt.hpp"
// #1305: --check-update. Reads the manifest this board's channel publishes, verifies it,
// compares the version it names against this build's own, and stops there.
#include "update/update_address.hpp"
#include "update/update_check.hpp"
#include "update/update_keys.hpp"
#ifdef _WIN32
// #1258: the question at start, Windows only. Linux compiles none of it.
#include "utils/camera_setup.hpp"
#endif
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>

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
  //
  // #1259: on Windows the default is beside the pairing credential rather than in the
  // working directory, so a board started from a shortcut, a console in another folder or
  // a double-click finds the same token every time instead of minting a new one wherever
  // it happened to start. It is a secret, so it goes where #822 put the other one and for
  // #822's reason, not beside the .exe. Linux is unchanged.
#ifdef _WIN32
  string token_default = od_paths::configDir().empty() ? string(score_token::kDefaultPath)
                                                       : od_paths::join(od_paths::configDir(), "score_token");
  if (!od_paths::configDir().empty() && !hasFlag(argc, argv, "--token-file"))
    od_paths::ensureDir(od_paths::configDir());
#else
  string token_default = score_token::kDefaultPath;
#endif
  string token_path = getArg(argc, argv, "--token-file", token_default);
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
#ifdef _WIN32
  // #1259: beside the .exe, not in whatever directory the program was started from.
  string model_path = getArg(argc, argv, "--model",
                             od_paths::exeDir().empty() ? string("models\\dart.param")
                                                        : od_paths::join(od_paths::exeDir(), "models\\dart.param"));
#else
  string model_path = getArg(argc, argv, "--model", "/usr/local/share/opendartboard/models/dart.param");
#endif
  bool useAuto = hasFlag(argc, argv, "--autocams");

  // #1330: score on the calibration the last start measured instead of looking at the
  // board. Off unless asked for: the cache file is named by the working directory and by
  // nothing else, so it cannot say whose geometry it is, and a board whose camera has been
  // nudged since it was written would score through a perspective that is wrong and looks
  // right. utils/cache.hpp holds the measurement.
  cache::geometry::allowReuse(hasFlag(argc, argv, "--reuse-calibration"));

  // #1388 / ADR-0080 section 4: the operator has looked at the rig. This is the ONLY
  // thing that clears a recorded geometry fault -- not a restart, not a successful
  // calibration, not time -- because a frame that has shifted on its bolts is a physical
  // fault and a board that cleared its own record would be a board deciding it had been
  // fixed. It exits rather than going on to score, so that what happens next is a start
  // somebody watched.
  if (hasFlag(argc, argv, "--clear-geometry-fault"))
  {
    const std::string was = geometry_fault::held();
    if (was.empty())
    {
      std::cout << "No geometry fault is recorded at " << geometry_fault::path()
                << "; there was nothing to clear." << std::endl;
      return 0;
    }
    if (!geometry_fault::clear())
    {
      std::cerr << "Could not clear the geometry fault at " << geometry_fault::path()
                << "; it is still held and this board will still refuse to calibrate."
                << std::endl;
      return 1;
    }
    std::cout << "Cleared the geometry fault recorded at " << geometry_fault::path()
              << ". It said: " << was << std::endl;
    std::cout << "This board will calibrate on the rig as it is now at the next start."
              << std::endl;
    return 0;
  }
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
  // #1259: OD_ALLOW_PLAINTEXT=1 is --allow-plaintext for a start that has no command line --
  // a double-click -- exactly as OD_TURNAUS_URL is --turnaus. Only the exact value 1.
  turnaus_config.allow_plaintext =
      hasFlag(argc, argv, "--allow-plaintext") || od_paths::env("OD_ALLOW_PLAINTEXT") == "1";
  // #1259: the pairing request says the board's label, not the literal it always sent.
  turnaus_config.label = label;

  // #1306: the four steps are update_address::resolve() and were these twenty lines. They
  // moved because the launcher now follows the same rule (ADR-0077 §5), and a rule two
  // programs follow from two copies is a rule they will one day disagree about.
  const update_address::Resolved resolved =
      update_address::resolve(getArg(argc, argv, "--turnaus", string("")), turnaus_config.credentials_path);
  string configured_url = resolved.url;
  string address_source = resolved.source;
  turnaus_config.base_url = configured_url;
  // #1257: one line, the address and which rule chose it; the credential is never logged.
  log_info("TURNAUS: address " + configured_url + " (from " + address_source + ")");

  // #1305: which releases this install is offered, and the one-shot check that says what
  // is published for it. The channel lives beside the credential (update_channel.hpp);
  // --channel is the ONLY thing on this path that writes a file, and it writes only when
  // somebody is setting it.
  const string channel_path = update_channel::fileBeside(turnaus_config.credentials_path);
  string channel = update_channel::load(channel_path);
  {
    const string asked_channel = getArg(argc, argv, "--channel", string(""));
    if (!asked_channel.empty())
    {
      console_prompt::StdConsole console;
      if (!update_channel::isKnown(asked_channel))
      {
        console.say({"Tuntematon päivityskanava '" + asked_channel + "'. Kanavat ovat: " + update_channel::known() + ".",
                     "Unknown update channel '" + asked_channel + "'. The channels are: " + update_channel::known() +
                         "."});
        return 1;
      }
      if (!update_channel::save(channel_path, asked_channel))
      {
        console.say({"Päivityskanavaa ei voitu tallentaa tiedostoon " + channel_path + ".",
                     "The update channel could not be written to " + channel_path + "."});
        return 1;
      }
      channel = asked_channel;
      console.say({"Päivityskanava on nyt " + channel + " (" + channel_path + ").",
                   "The update channel is now " + channel + " (" + channel_path + ")."});
    }
  }

  // --check-update asks, verifies and prints. It opens no camera, starts no detector,
  // downloads no release and replaces no file (ADR-0077; #1305).
  if (hasFlag(argc, argv, "--check-update"))
  {
    update_check::Answer answer = update_check::ask(turnaus_config.base_url, channel, version,
                                                    update_keys::anchors(), update_check::fetchOverHttp);
    console_prompt::StdConsole console;
    const vector<console_prompt::Text> said = update_check::lines(answer);
    for (size_t i = 0; i < said.size(); i++)
      console.say(said[i]);
    return update_check::exitCode(answer);
  }

  // --pair exchanges a code for a credential and stops. It opens no camera and starts
  // no detector: a board being paired is a board somebody is standing in front of with
  // a six-digit code that expires in ten minutes, not a board that needs to calibrate.
  string pairing_code = getArg(argc, argv, "--pair", string(""));
  if (!pairing_code.empty())
  {
    TurnausClient client(turnaus_config);
    return client.pair(pairing_code) ? 0 : 1;
  }

  // #891: the same exchange at the third door. --pair-contest binds this board to one
  // Casual Contest -- somebody's knockabout, in a pub or in a garage -- and to nothing
  // else: no Station and no Organisation (ADR-0069). Two flags rather than one that
  // guesses, because the two codes are six digits apiece at two doors that do not read
  // each other's table, so nothing in the digits says which evening they are for. The
  // person holding the code knows, and this is where they say it.
  //
  // An Organisation pairing already on this board is kept. A club board is Station 3 on
  // Tuesday and a knockabout on Wednesday, and the machine bolted to it does not stop
  // working; the Contest binding wins while it lasts and the club's is still underneath
  // it when the evening ends.
  string contest_code = getArg(argc, argv, "--pair-contest", string(""));
  if (!contest_code.empty())
  {
    TurnausClient client(turnaus_config);
    return client.pairContest(contest_code) ? 0 : 1;
  }

  // #1473: this host runs one board, and the second one declines here.
  //
  // httplib asks for SO_REUSEPORT and not SO_REUSEADDR, so two boards on one host BOTH
  // bind 13520 successfully and the kernel shares the arriving connections between them:
  // nothing fails, nothing is logged, and a phone subscribing to the score stream sees
  // roughly half the darts. #1295 made a board that cannot listen refuse to announce
  // itself, and that refusal is untouched and still the right one for the other-program
  // case -- but it never fires here, because this bind succeeds.
  //
  // So the claim is taken on a lock instead, and one_board.hpp says why it is a lock and
  // not the flag. It is taken HERE, which is early on purpose:
  //
  //   * above the cameras and the calibration, so the second board is refused in the
  //     window a restarting board overlaps its predecessor in -- between process start
  //     and listen() -- rather than tens of seconds later;
  //   * below every flag that pairs, prints or asks and then exits (--version, --help,
  //     --setup, --show-token, --check-update, --clear-geometry-fault, --pair,
  //     --pair-contest), because none of those opens a socket and pairing a second board
  //     while the first one scores must go on working;
  //   * below the logging setup, so the refusal is a log line and not a silence.
  //
  // The claim is declared above the Scorer, so it is released after ~Scorer has given the
  // socket up rather than before.
  one_board::Claim one_board_claim;
  if (!one_board_claim.take(ScoreSocketSettings().port))
  {
    log_error(one_board::refusedBecause(one_board_claim, ScoreSocketSettings().port));
    log_error(one_board::refusalRemedy(one_board_claim));
    return 1;
  }
  log_info("one board per host: " + one_board_claim.detail());

  // #1259: the client is built here, before a camera opens, so an interactive start with no
  // credential can be paired from the console and go straight on. It is still STARTED where
  // #892 and #1247 put it, below; building it early starts nothing. pairing_prompt.hpp says
  // when it asks and why the question comes before the cameras.
  std::unique_ptr<TurnausClient> turnaus(new TurnausClient(turnaus_config));
  const bool interactive = console_prompt::isInteractiveConsole();
  if (interactive && !turnaus->isPaired())
  {
    console_prompt::StdConsole console;
    pairing_prompt::pairAtStart(console, *turnaus, turnaus_config.base_url);
  }

  // setup cams
  vector<string> cams;
  const bool cams_given = hasFlag(argc, argv, "--cams");
  if (useAuto)
  {
    cams = autocam::detectAndLock(/*max*/ 3, width, height, fps);
  }
  else if (cams_given)
  {
    // #1258: --cams on the command line is taken as given and never asked about. #1318
    // keeps that whole: what is typed here is opened, in this order, and nothing below
    // runs. It is the maintainer's --cams 1,2,3 and it is the answer to any disagreement
    // with the probe. Media Foundation has no filesystem name for a camera, so on
    // Windows a device is an index.
#ifdef _WIN32
    cams = getArgVector(argc, argv, "--cams", "0,1,2");
#else
    cams = getArgVector(argc, argv, "--cams", "/dev/video0,/dev/video1,/dev/video2");
#endif
  }
  else
  {
    // #1318: nothing was typed, which on a laptop used to mean 0,1,2 -- the built-in
    // webcam and two of the three board cameras. The three of them OPEN, so #1258's
    // question was never asked and the operator's face was calibrated as camera 1.
    //
    // So the cameras are found by looking through them. board_cameras.hpp holds the
    // mechanism and what it costs.
#ifdef _WIN32
    // A choice somebody already made at the console outranks the probe, the way --cams
    // does: it is the same human answering the same question, once, and #1258 saved it
    // precisely so it would not be asked again.
    std::vector<camera_choice::Remembered> remembered;
    const bool have_remembered =
        camera_choice::load(camera_choice::fileBeside(turnaus_config.credentials_path), remembered);
#else
    const bool have_remembered = false;
#endif
    if (!have_remembered)
      cams = board_cameras::choose(/*want*/ 3, width, height, fps);

    if (cams.empty())
    {
      // Nothing seen, or a remembered choice to honour. Fall back to what this platform
      // did before: on Windows the remembered choice, or the defaults and #1258's
      // question when one of them does not open; on Linux, the three defaults. A board
      // whose cameras are all covered or in a dark room lands here and then fails at
      // calibration by name, which is where the sentence a person can act on is.
#ifdef _WIN32
      cams = camera_setup::camerasAtStart(turnaus_config.credentials_path, width, height, fps);
#else
      cams = getArgVector(argc, argv, "--cams", "/dev/video0,/dev/video1,/dev/video2");
      log_warning("CAMERAS: no video device on this machine could see a dartboard; falling back to the defaults");
#endif
    }
    else
    {
      string list;
      for (size_t i = 0; i < cams.size(); i++)
        list += (i ? "," : "") + cams[i];
      log_info("CAMERAS: " + list + " (looked through; these are the ones that can see the dartboard)");
    }
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

  // #1189: where the announcement would be written, read here because this is where the
  // rest of the command line is read. Whether anything is written is decided below the
  // Scorer, once this board is known to have a socket to announce (#1274).
  string announce_dir = getArg(argc, argv, "--announce-dir", announce::kDefaultDir);

  // #892: the client is built and STARTED before the Scorer, so the beat covers the
  // startup it used to pass over in silence. Opening three cameras and calibrating on
  // thirty averaged frames is seconds of a board being up and not yet able to see, and
  // INITIALISING and CALIBRATING are two of the five words a board may say precisely so
  // that interval is not indistinguishable from a machine nobody switched on. start()
  // is idempotent, so Scorer::run()'s own call is unchanged and harmless.
  //
  // #1247: started after #1187's token rather than before it, because the token's failure
  // returns from main(), and a client started above that return would leave its thread
  // running into the process's exit.
  //
  // #1274: it used to be started after #1189's announcement as well, and is no longer,
  // because the announcement moved below the Scorer. Nothing is lost: the reason above is
  // the token's `return 1`, and publishing a service file returns from nothing.
  turnaus->start();
  TurnausClient *turnaus_view = turnaus.get(); // owned by the Scorer from here to the end of main

  // Initialise the scorer with debug mode if requested
  Scorer scorer(model_path, width, height, fps, cams, debug_mode, detector_type, socket);
  scorer.attachTurnaus(std::move(turnaus));

  // #1189: announced through the host's responder while - and only while - the
  // socket is on the network. A loopback-only start withdraws what an earlier
  // --listen run may have left, so the two states cannot disagree.
  //
  // #1274: and only when there is going to be a socket at all. This block ran above the
  // Scorer until #1274, where nothing yet knew whether the cameras would open; a board
  // whose cameras do not open takes #895's fault vigil, which deliberately never starts
  // the score socket, so a dark board started with --listen announced a socket that was
  // never opened and a phone that found it by the announcement connected to nothing.
  // scorer.canSee() is the same question run() asks before it starts the socket, so the
  // announcement cannot say something the listener contradicts, and a stale file from an
  // earlier --listen run is withdrawn on this path as it is on the loopback one.
  //
  // Asking it here rather than above also shortens the interval in which a board is
  // announced and not yet listening: opening three cameras and calibrating happen in the
  // Scorer's constructor, above this line, and the socket opens inside run(), below it.
  // #1295: and only when the socket really opened. canSee() is the right question for a
  // dark board and the wrong one for a socket that cannot bind: listen() fails later and
  // elsewhere - typically because the port is already in use - and a board that can see,
  // announces, and then fails to listen sends a phone to a socket that is not there.
  // #1274 could not ask it, because the socket opened inside run(), below, and the service
  // logged its failure from a thread nothing here could hear.
  //
  // So the socket is opened HERE, before the announcement, and the service is asked
  // whether it came up. scorer.run() opens the same socket through the same idempotent
  // call, so the board still gets one socket - and, as with canSee(), one condition with
  // two readers rather than two spellings that can drift apart.
  //
  // What this deliberately does not do is ask again on the cycle budget. #1274's carve-out
  // stands: a socket that dies mid-run keeps its announcement, because withdrawing on that
  // is the future/promise shape this issue weighed and rejected for cost.
  const bool can_see = scorer.canSee();
  const bool socket_open = scorer.openScoreSocket();
  const bool announcing = listen && socket_open;
  if (announcing)
  {
    announce::Outcome published = announce::publish(announce_dir, label, socket.port, version);
    if (published.done)
      log_info("announced as '" + label + "' (" + announce::kServiceType + ", port " + to_string(socket.port) +
               ") via " + published.detail);
    else
      log_warning("not announced: " + published.detail + " (--announce-dir names another directory)");
  }
  else if (listen)
  {
    // #1295: two reasons reach this branch and they want different remedies -- a camera
    // that did not open is not a port that is already in use -- so the log names which.
    const string why = can_see
                           ? "the score socket did not open on " + socket.bind_address + ":" +
                                 to_string(socket.port) + ", so there is nothing to announce"
                           : "this board cannot see, so the score socket is never opened";
    announce::Outcome withdrawn = announce::withdraw(announce_dir);
    if (withdrawn.done)
      log_warning("not announced: " + why + "; removed " + withdrawn.detail + " left by an earlier run");
    else
      log_warning("not announced: " + why);
  }
  else
  {
    announce::Outcome withdrawn = announce::withdraw(announce_dir);
    if (withdrawn.done)
      log_info("not announced: loopback only; removed " + withdrawn.detail + " left by an earlier --listen run");
    else
      log_info("not announced: loopback only");
  }

  // #825: the handler records the signal and returns; Scorer::run()'s loop is what
  // observes it. There is no callback here any more, because a callback called from a
  // signal context is a callback that runs while the interrupted thread holds locks.
  //
  // #1189 withdrew the announcement inside the old callback, because the old handler
  // exit()ed straight after it and the withdrawal below was never reached on SIGINT or
  // SIGTERM. Under #825 the loop leaves, run() returns and control reaches the
  // withdrawal below on a signal exactly as on the cycle-budget exit, still before
  // ~Scorer stops the socket. A second signal re-raises with the default disposition
  // and leaves the file behind, as a SIGKILL always did.
  signals::setupSignalHandlers();

  // #1259: an interactive board whose credential is refused while it runs says so and asks
  // for a new code on this thread's neighbour; a board with no console never starts it.
  // Joined before ~Scorer destroys the client it watches.
  std::atomic<bool> leaving{false};
  std::thread pairing_watcher;
  if (interactive)
  {
    const string address = turnaus_config.base_url;
    pairing_watcher = std::thread([turnaus_view, address, &leaving]
                                  { pairing_prompt::watchForUnpairing(*turnaus_view, address, leaving); });
  }

  // Run the scorer on this thread. It returns when the loop sees the shutdown flag.
  scorer.run();

  leaving = true;
  if (pairing_watcher.joinable())
    pairing_watcher.join();

  // #1189: the announcement does not outlive the socket. Reached on the cycle budget
  // and, since #825, on SIGINT and SIGTERM too.
  //
  // #1274: `announcing` rather than `listen`, because a start that refused to announce has
  // nothing to withdraw here - it withdrew above, before the vigil began - and asking
  // again would log a failure about a file this run was right not to write.
  if (announcing)
  {
    announce::Outcome withdrawn = announce::withdraw(announce_dir);
    log_info(withdrawn.done ? "announcement withdrawn: removed " + withdrawn.detail
                            : "announcement not withdrawn: " + withdrawn.detail);
  }

  // #825: and then this function ends normally, which is the whole point. ~Scorer runs
  // here -- joining the WebSocket worker, stopping the HTTP server and releasing the
  // cameras -- because main is unwound rather than skipped by exit().
  //
  // #1383: with the one status that is not 0. A run a tester bounded with OD_MAX_CYCLES
  // that spent that budget blind did none of what a scorer is for, and says so rather
  // than reporting success -- see Scorer::kCouldNotSee for what the number claims and
  // why it is not 0, not 78 and not a crash. Every other route out of run() -- the
  // scoring budget, the end of the footage, SIGINT, SIGTERM, a vigil a signal left --
  // returns 0 exactly as before. Taken after the withdrawal above, so the announcement
  // is gone whatever this answers.
  return scorer.endedBlindOnTheBudget() ? Scorer::kCouldNotSee : 0;
}