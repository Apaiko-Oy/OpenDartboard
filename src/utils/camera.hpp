#pragma once

// The capture layer is now a seam and one implementation behind it. This header exists
// so that everything that used to reach the three free functions through utils.hpp
// reaches CaptureSource and Frame instead.
#include "frame.hpp"
#include "capture.hpp"
#include "capture_opencv.hpp"
