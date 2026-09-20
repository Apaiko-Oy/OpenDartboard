#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <opencv2/opencv.hpp>
#include "detector/detector_interface.hpp"
#include "../utils/capture.hpp"
#include "../communication/websocket_service.hpp"
#include "../communication/score_queue.hpp"
#include "../communication/turnaus_client.hpp"
#include <memory>

using namespace std;

class Scorer
{
public:
  Scorer(const std::string &model, int width, int height, int fps,
         const std::vector<std::string> &cams, bool debug_mode = false,
         const std::string &detector_type = "geometry",
         const ScoreSocketSettings &socket = ScoreSocketSettings());
  ~Scorer();

  void run();
  void stop();

  /**
   * #1383: the exit status a run that ended blind carries, and what it claims.
   *
   * AN EXIT CODE IS A CLAIM, AND "I COULD NOT SEE" IS NOT "I CRASHED". A blind board
   * that was given no cycle budget makes no claim at all, because it does not exit --
   * that is #895's vigil and it is unchanged. This is the one case that was added: a run
   * a TESTER bounded, which ends having done none of what a scorer is for.
   *
   * 75 is EX_TEMPFAIL, sysexits.h's "temporary failure; the user is invited to retry",
   * which is exactly a camera that is not plugged in yet. The house already spells exit
   * statuses this way -- scorer.cpp exits 78, EX_CONFIG, for a motion fix it refuses to
   * run. It is deliberately none of the three codes a reader would otherwise have to tell
   * it from: 0 is "the run did what it was asked", which a blind run did not; 139 is the
   * SIGSEGV #892 measured on this exact path, and a fault that reads as a crash is what
   * #895 spent a slice replacing; 78 is somebody mistyping an environment variable.
   *
   * THE UNIT AND THE BINARY AGREE ABOUT IT. `Restart=always` restarts on any status, so
   * the template needs nothing for this and gets nothing; what it gets instead is a
   * comment saying that the case it looks like it covers -- a board that cannot see -- is
   * the one case that never exits at all. A deployment sets no cycle budget, and
   * testers/i1383_units.sh fails the tree if the shipped unit ever names one.
   */
  static constexpr int kCouldNotSee = 75;

  /**
   * Whether this run ended because a cycle budget ended a board that could not see.
   * Read once, by main, to decide the status above. False on every other route out --
   * including a vigil left by SIGTERM, which exits 0 as it always has.
   */
  bool endedBlindOnTheBudget() const { return ended_blind_on_the_budget_; }

  // #1274: whether this object has a detector it can score with -- and so whether run()
  // will open the score socket at all, or take #895's fault vigil, which deliberately
  // does not. main asks it before announcing the board on the network, so an announcement
  // and the listener it names cannot disagree about whether there is a socket. run()
  // branches on the same call: one condition, two readers.
  bool canSee() const;

  // #822: the outbound client, handed in rather than built here, because pairing must
  // be possible without opening a camera. Scorer owns it so that #825's exit path --
  // main unwound, ~Scorer run -- is what stops and joins it.
  void attachTurnaus(std::unique_ptr<TurnausClient> client);

private:
  //  Result sending
  void sendResult(const DetectorResult &result);

  // #895: what run() does instead of scoring when the constructor could not give this
  // object a detector to score with. It is a loop rather than a return, because the
  // board is still there and #892 gave it a word for exactly this.
  void runFaultVigil();

  // #899: one attempt at getting the board's sight back -- reopen the cameras, average a
  // few frames, and ask the detector whether the calibration it holds is still true of
  // what it can see.
  //
  // #1388 / ADR-0080: it returns the whole review rather than the verdict alone, and it
  // decides nothing. `Moved` used to fault the board from inside this call, which made
  // one measurement and one policy the same event; a budget cannot be spent by a function
  // that ends the run on the first disagreement. So the verdicts are now instructions to
  // run(): `Unchanged` resumes scoring on the held calibration, `Unreadable` means try
  // again on the backoff, and `Moved` means ask again -- until the budget is spent, and
  // then fault, with the account this carries back.
  GeometryReview attemptRecovery(int attempt, long blind_seconds);

  // Configuration
  string model_path;
  int width, height, fps;
  vector<string> camera_sources;
  bool debug_display;
  string detector_type_name;

  // Hardware, behind the seam
  std::unique_ptr<camera::CaptureSource> capture;
  std::unique_ptr<DetectorInterface> detector;

  // Simple control
  atomic<bool> running{false};

  // #1383: set by the fault vigil and by nothing else. Written and read on the thread
  // that calls run(), so it is a plain bool rather than an atomic.
  bool ended_blind_on_the_budget_{false};

  std::shared_ptr<ScoreQueue> score_queue_;
  std::unique_ptr<WebSocketService> websocket_service_;

  // Declared last, so member destruction in reverse order stops and joins the push
  // worker first -- before the queue it reads from and the cameras it never touches.
  std::unique_ptr<TurnausClient> turnaus_;
};