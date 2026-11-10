#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Head tracking: where a person's head is turned, from whatever sensor is at hand.
//
// A HeadTracker only reports orientation. It knows nothing about cameras, so AirPods, a webcam
// or a headset can each be a provider, and the camera code never changes. Orientation is in
// engine axes (+X right, +Y up, -Z forward) and is relative to an arbitrary reference: sensors
// do not know where "straight ahead" is, so HeadTrackingController recenters.
//
// Nothing in this header depends on a platform. The AirPods provider lives behind
// createHeadTracker() and is built only on macOS.

enum class HeadTrackingState {
    Stopped,       // not started
    Unavailable,   // this provider cannot work here: wrong platform or system version
    Waiting,       // started, but the device is absent or not sending
    Tracking,      // samples are arriving
    Error          // failed; see HeadTrackingDiagnostics::error
};

const char* toString(HeadTrackingState state);

// Everything a diagnostics display wants to know, copied under a lock so it is always consistent.
struct HeadTrackingDiagnostics {
    std::string provider;
    HeadTrackingState state = HeadTrackingState::Stopped;
    bool available = true;           // false when the provider can never work on this system
    std::string error;               // the last failure or reason for waiting; empty when none
    glm::quat orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);   // the latest sample, as sent
    float updateHz = 0.0f;           // measured from the samples' own timestamps
    unsigned long long samples = 0;  // received since start
};

class HeadTracker {
public:
    virtual ~HeadTracker() = default;

    virtual const char* name() const = 0;

    // Start and stop are cheap and may be called repeatedly. Failures do not throw: they are
    // reported through diagnostics().
    virtual void start() = 0;
    virtual void stop() = 0;

    // Safe to call from any thread, while samples arrive on another.
    virtual HeadTrackingDiagnostics diagnostics() const = 0;
};

// The part every provider repeats: a lock around the latest sample, the state, the error and
// a measured update rate. Providers call publishSample() and setState() from their own thread.
class HeadTrackerBase : public HeadTracker {
public:
    HeadTrackingDiagnostics diagnostics() const override;

protected:
    explicit HeadTrackerBase(std::string providerName) { info.provider = std::move(providerName); }

    // seconds is the sample's own timestamp, on any clock that only moves forward.
    void publishSample(const glm::quat& orientation, double seconds);
    void setState(HeadTrackingState state, const std::string& message = std::string());
    void resetStatistics();

private:
    mutable std::mutex lock;
    HeadTrackingDiagnostics info;
    double windowStart = 0.0;
    unsigned long long windowCount = 0;
    std::chrono::steady_clock::time_point lastArrival{};
    bool hasArrival = false;
};

// A provider that needs no hardware: a head that looks around by itself at 60 Hz, so the whole
// pipeline can be exercised without AirPods. Tests drive it with injectSample() instead.
class MockHeadTracker : public HeadTrackerBase {
public:
    // Runs the background thread when true; false leaves the samples to injectSample().
    explicit MockHeadTracker(bool animated = true);
    ~MockHeadTracker() override;

    const char* name() const override { return "Mock"; }
    void start() override;
    void stop() override;

    void injectSample(const glm::quat& orientation, double seconds);

    // What a device being taken out, or failing, looks like to the rest of the engine.
    void simulateDisconnect(bool disconnected);
    void simulateError(const std::string& message);

private:
    const bool animated;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> disconnected{false};

    void run();
};

// A provider that reports itself as unusable and says why.
std::unique_ptr<HeadTracker> createUnavailableHeadTracker(const std::string& provider, const std::string& reason);

enum class HeadTrackerKind { Off, Mock, AirPods, Custom };

// Off and Custom give nothing. AirPods is the platform's provider, or an unavailable one
// that explains why not.
std::unique_ptr<HeadTracker> createHeadTracker(HeadTrackerKind kind);

// Whether this build can talk to AirPods at all (macOS 14 or later, with Core Motion).
bool airPodsTrackingSupported();

// Sensors in the "right, forward, up" convention (x to the right, y forward, z up, which is what
// Core Motion's headphone frame uses) into engine axes.
glm::quat orientationFromRightForwardUp(double x, double y, double z, double w);

struct HeadTrackingSettings {
    float sensitivity = 1.0f;   // 1 follows the head exactly; 2 turns the view twice as far
    float smoothing = 0.05f;    // seconds the view takes to catch up; 0 is none
    bool yaw = true;
    bool pitch = true;
    bool roll = true;
};

// Owns a tracker and turns its raw orientation into the rotation a camera should add to its own:
// recentered, scaled, masked and smoothed. One per game; call update() once a frame.
class HeadTrackingController {
public:
    HeadTrackingController() = default;
    ~HeadTrackingController();
    HeadTrackingController(const HeadTrackingController&) = delete;
    HeadTrackingController& operator=(const HeadTrackingController&) = delete;

    HeadTrackingSettings settings;

    // Replaces the provider; the old one is stopped and the new one started.
    void setKind(HeadTrackerKind kind);
    // For providers that live outside the engine (a webcam tracker, a headset). Kind becomes Custom.
    void setTracker(std::unique_ptr<HeadTracker> custom);
    HeadTrackerKind getKind() const { return kind; }
    HeadTracker* getTracker() { return tracker.get(); }

    // Makes the current head direction "straight ahead".
    void recenter() { recenterRequested = true; }

    // The rotation to apply in the camera's own space; the identity unless samples are arriving.
    glm::quat update(float deltaTime);
    glm::quat getRotation() const { return rotation; }

    HeadTrackingDiagnostics diagnostics() const;

    // The rotation as yaw, pitch and roll in degrees, for display.
    glm::vec3 getEulerDegrees() const;

private:
    std::unique_ptr<HeadTracker> tracker;
    HeadTrackerKind kind = HeadTrackerKind::Off;
    glm::quat reference = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    bool recenterRequested = true;   // the first sample of each run sets the reference
};
