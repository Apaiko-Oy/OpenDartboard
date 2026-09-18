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
  // what it can see. The verdict is the caller's instruction: `Unchanged` resumes
  // scoring, `Moved` faults the board for good, `Unreadable` means try again later.
  GeometryReview::Verdict attemptRecovery(int attempt, long blind_seconds);

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

  std::shared_ptr<ScoreQueue> score_queue_;
  std::unique_ptr<WebSocketService> websocket_service_;

  // Declared last, so member destruction in reverse order stops and joins the push
  // worker first -- before the queue it reads from and the cameras it never touches.
  std::unique_ptr<TurnausClient> turnaus_;
};