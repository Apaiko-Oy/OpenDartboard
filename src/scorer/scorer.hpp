#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <opencv2/opencv.hpp>
#include "detector/detector_interface.hpp"
#include "../utils/capture.hpp"
#include "../communication/websocket_service.hpp"
#include "../communication/score_queue.hpp"
#include "../utils/streamer.hpp"
#include <memory>

using namespace std;

class Scorer
{
public:
  Scorer(const std::string &model, int width, int height, int fps,
         const std::vector<std::string> &cams, bool debug_mode = false,
         const std::string &detector_type = "geometry", bool setup_mode = false);
  ~Scorer();

  void run();
  void stop();

private:
  //  Result sending
  void sendResult(const DetectorResult &result);

  // Configuration
  string model_path;
  int width, height, fps;
  vector<string> camera_sources;
  bool debug_display;
  string detector_type_name;
  // #824: --setup. A view of what each camera sees, on loopback, and nothing published.
  bool setup_mode;

  // Hardware, behind the seam
  std::unique_ptr<camera::CaptureSource> capture;
  std::unique_ptr<DetectorInterface> detector;

  // Simple control
  atomic<bool> running{false};

  std::shared_ptr<ScoreQueue> score_queue_;
  std::unique_ptr<WebSocketService> websocket_service_;

  // #824: the setup view. Held here rather than in the detector because it shows the
  // frames the capture returned, which is the question a person aiming a camera has,
  // and because that keeps it out of every vision stage.
  std::unique_ptr<streamer> setup_streamer_;
};