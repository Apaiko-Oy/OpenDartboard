#pragma once
#include "detector_interface.hpp"
#include "geometry/geometry_detector.hpp"
#include "utils.hpp"
#include <memory>
#include <string>

// The third-party plugin path. POSIX loads a .so with dlopen; Windows loads a
// .dll with LoadLibrary. The names differ, the three probe paths do not.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace odplugin
{
    using handle_t = HMODULE;
    inline handle_t open(const std::string &path) { return ::LoadLibraryA(path.c_str()); }
    inline void *symbol(handle_t h, const char *name) { return reinterpret_cast<void *>(::GetProcAddress(h, name)); }
    inline void close(handle_t h) { ::FreeLibrary(h); }
    inline std::string lastError() { return "LoadLibrary failed, GetLastError=" + std::to_string((unsigned long)::GetLastError()); }
    inline const char *suffix() { return ".dll"; }
    inline const char *prefix() { return ""; }
}
#else
#include <dlfcn.h>
namespace odplugin
{
    using handle_t = void *;
    inline handle_t open(const std::string &path) { return ::dlopen(path.c_str(), RTLD_LAZY); }
    inline void *symbol(handle_t h, const char *name) { return ::dlsym(h, name); }
    inline void close(handle_t h) { ::dlclose(h); }
    inline std::string lastError() { const char *e = ::dlerror(); return e ? std::string(e) : std::string("(none)"); }
    inline const char *suffix() { return ".so"; }
    inline const char *prefix() { return "lib"; }
}
#endif

/**
 * Custom detector factory to create instances of dart detectors.
 * extern "C" DetectorInterface* create_detector(bool debug_mode, int width, int height, int fps) {
 *   return new MyCoolDetector(debug_mode, width, height, fps);
 * }
 */

enum class DetectorType
{
    GEOMETRY,
    AI,
    CUSTOM
};

class DetectorFactory
{
private:
    // Convert string to enum for switch statement
    static DetectorType stringToDetectorType(const std::string &detector_type)
    {
        if (detector_type == "geometry")
            return DetectorType::GEOMETRY;
        if (detector_type == "ai")
            return DetectorType::AI;
        return DetectorType::CUSTOM; // Everything else is treated as custom
    }

    // Dynamic loading for custom detectors
    static std::unique_ptr<DetectorInterface> loadCustomDetector(const std::string &name, bool debug_mode, int target_width, int target_height, int target_fps)
    {
        log_info("Attempting to load custom detector: " + name);

        const std::string pre = odplugin::prefix();
        const std::string suf = odplugin::suffix();

        // Try folder structure first: detectors/PluginName/libPluginName.so
        std::string folderLibPath = "detectors/" + name + "/" + pre + name + suf;
        odplugin::handle_t handle = odplugin::open(folderLibPath);

        if (!handle)
        {
            // Fallback to flat structure: detectors/libPluginName.so
            std::string flatLibPath = "detectors/" + pre + name + suf;
            handle = odplugin::open(flatLibPath);

            if (!handle)
            {
                // Try simple name: detectors/PluginName.so
                std::string simpleLibPath = "detectors/" + name + suf;
                handle = odplugin::open(simpleLibPath);

                if (!handle)
                {
                    std::string error = odplugin::lastError();
                    log_error("Cannot load detector library. Tried:");
                    log_error("  1. " + folderLibPath);
                    log_error("  2. " + flatLibPath);
                    log_error("  3. " + simpleLibPath);
                    log_error("Last error: " + error);
                    log_warning("Falling back to geometry detector");
                    return std::make_unique<GeometryDetector>(debug_mode, target_width, target_height, target_fps);
                }
            }
        }

        // Look for the factory function
        typedef DetectorInterface *(*create_detector_t)(bool, int, int, int);
        create_detector_t create_detector = (create_detector_t)odplugin::symbol(handle, "create_detector");

        if (!create_detector)
        {
            log_error("Cannot find 'create_detector' function in plugin");
            log_warning("Falling back to geometry detector");
            odplugin::close(handle);
            return std::make_unique<GeometryDetector>(debug_mode, target_width, target_height, target_fps);
        }

        log_info("Successfully loaded custom detector: " + name);
        DetectorInterface *detector = create_detector(debug_mode, target_width, target_height, target_fps);
        return std::unique_ptr<DetectorInterface>(detector);
    }

public:
    static std::unique_ptr<DetectorInterface> createDetector(const std::string &detector_type, bool debug_mode, int target_width, int target_height, int target_fps)
    {
        log_debug("Creating detector: " + detector_type);

        switch (stringToDetectorType(detector_type))
        {
        case DetectorType::GEOMETRY:
            log_info("Loading geometry-based dart detector");
            return std::make_unique<GeometryDetector>(debug_mode, target_width, target_height, target_fps);

        case DetectorType::AI:
            log_info("Loading AI-based dart detector");
            log_warning("AI detection not yet implemented, using geometry detector");
            return std::make_unique<GeometryDetector>(debug_mode, target_width, target_height, target_fps);

        case DetectorType::CUSTOM:
            // Try to load as a custom detector plugin
            return loadCustomDetector(detector_type, debug_mode, target_width, target_height, target_fps);

        default:
            log_error("Unknown detector type: " + detector_type + ", using geometry detector");
            return std::make_unique<GeometryDetector>(debug_mode, target_width, target_height, target_fps);
        }
    }
};
