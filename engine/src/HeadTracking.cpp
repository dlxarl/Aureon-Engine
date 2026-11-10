#include "HeadTracking.h"

#if defined(__APPLE__)
#include "CoreMotionHeadTracker.h"
#endif

#include <algorithm>
#include <cmath>

namespace {
    const double RATE_WINDOW = 0.5;           // seconds of samples behind each update rate
    const double STALE_AFTER = 1.0;           // seconds of silence before tracking counts as lost
    const float PI = 3.14159265358979f;

    // The rotation as yaw about Y, then pitch about X, then roll about Z (a head turns, nods, tilts).
    glm::vec3 toEuler(const glm::quat& q) {
        const glm::mat3 m = glm::mat3_cast(q);
        const float pitch = std::asin(std::clamp(-m[2][1], -1.0f, 1.0f));
        const float yaw = std::atan2(m[2][0], m[2][2]);
        const float roll = std::atan2(m[0][1], m[1][1]);
        return glm::vec3(yaw, pitch, roll);
    }

    glm::quat fromEuler(const glm::vec3& yawPitchRoll) {
        return glm::angleAxis(yawPitchRoll.x, glm::vec3(0.0f, 1.0f, 0.0f)) *
               glm::angleAxis(yawPitchRoll.y, glm::vec3(1.0f, 0.0f, 0.0f)) *
               glm::angleAxis(yawPitchRoll.z, glm::vec3(0.0f, 0.0f, 1.0f));
    }

    class UnavailableHeadTracker : public HeadTrackerBase {
    public:
        UnavailableHeadTracker(const std::string& provider, std::string why)
            : HeadTrackerBase(provider), reason(std::move(why)) {
            setState(HeadTrackingState::Unavailable, reason);
        }
        const char* name() const override { return "Unavailable"; }
        void start() override { setState(HeadTrackingState::Unavailable, reason); }
        void stop() override {}

    private:
        std::string reason;
    };
}

const char* toString(HeadTrackingState state) {
    switch (state) {
        case HeadTrackingState::Stopped: return "Stopped";
        case HeadTrackingState::Unavailable: return "Unavailable";
        case HeadTrackingState::Waiting: return "Waiting";
        case HeadTrackingState::Tracking: return "Tracking";
        case HeadTrackingState::Error: return "Error";
    }
    return "Unknown";
}

// ---- HeadTrackerBase ----

HeadTrackingDiagnostics HeadTrackerBase::diagnostics() const {
    std::lock_guard<std::mutex> guard(lock);
    HeadTrackingDiagnostics copy = info;
    copy.available = info.state != HeadTrackingState::Unavailable;

    if (copy.state == HeadTrackingState::Tracking && hasArrival) {
        const double silence = std::chrono::duration<double>(std::chrono::steady_clock::now() - lastArrival).count();
        if (silence > STALE_AFTER) {
            copy.state = HeadTrackingState::Waiting;
            copy.error = "No samples for " + std::to_string((int)silence) + " s";
        }
    }
    if (copy.state != HeadTrackingState::Tracking) copy.updateHz = 0.0f;
    return copy;
}

void HeadTrackerBase::publishSample(const glm::quat& orientation, double seconds) {
    std::lock_guard<std::mutex> guard(lock);
    info.orientation = glm::normalize(orientation);
    info.samples++;
    info.state = HeadTrackingState::Tracking;
    info.error.clear();
    lastArrival = std::chrono::steady_clock::now();
    hasArrival = true;

    if (windowCount == 0) windowStart = seconds;
    windowCount++;
    const double elapsed = seconds - windowStart;
    if (elapsed >= RATE_WINDOW) {
        info.updateHz = (float)((double)(windowCount - 1) / elapsed);
        windowStart = seconds;
        windowCount = 1;
    }
}

void HeadTrackerBase::setState(HeadTrackingState state, const std::string& message) {
    std::lock_guard<std::mutex> guard(lock);
    info.state = state;
    info.error = message;
    if (state != HeadTrackingState::Tracking) info.updateHz = 0.0f;
}

void HeadTrackerBase::resetStatistics() {
    std::lock_guard<std::mutex> guard(lock);
    info.samples = 0;
    info.updateHz = 0.0f;
    windowCount = 0;
    hasArrival = false;
}

// ---- MockHeadTracker ----

MockHeadTracker::MockHeadTracker(bool animatedMotion) : HeadTrackerBase("Mock"), animated(animatedMotion) {}

MockHeadTracker::~MockHeadTracker() {
    stop();
}

void MockHeadTracker::start() {
    if (running) return;
    resetStatistics();
    disconnected = false;
    setState(HeadTrackingState::Waiting, "Waiting for the first sample");
    running = true;
    if (animated) worker = std::thread([this] { run(); });
}

void MockHeadTracker::stop() {
    if (!running) return;
    running = false;
    if (worker.joinable()) worker.join();
    setState(HeadTrackingState::Stopped);
}

void MockHeadTracker::injectSample(const glm::quat& orientation, double seconds) {
    if (running && !disconnected) publishSample(orientation, seconds);
}

void MockHeadTracker::simulateDisconnect(bool value) {
    disconnected = value;
    if (value) {
        setState(HeadTrackingState::Waiting, "Mock device disconnected");
    } else if (running) {
        setState(HeadTrackingState::Waiting, "Waiting for the first sample");
    }
}

void MockHeadTracker::simulateError(const std::string& message) {
    disconnected = true;
    setState(HeadTrackingState::Error, message);
}

void MockHeadTracker::run() {
    using clock = std::chrono::steady_clock;
    const auto begin = clock::now();
    auto next = begin;
    while (running) {
        const double t = std::chrono::duration<double>(clock::now() - begin).count();
        if (!disconnected) {
            // Sensors have no idea where "ahead" is, so the mock starts a quarter turn off.
            const float yaw = glm::radians(35.0f) * (float)std::sin(2.0 * PI * 0.15 * t);
            const float pitch = glm::radians(15.0f) * (float)std::sin(2.0 * PI * 0.11 * t + 1.0);
            const float roll = glm::radians(6.0f) * (float)std::sin(2.0 * PI * 0.07 * t);
            const glm::quat arbitrary = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            publishSample(arbitrary * fromEuler(glm::vec3(yaw, pitch, roll)), t);
        }
        next += std::chrono::microseconds(16667);
        std::this_thread::sleep_until(next);
    }
}

// ---- Factories ----

std::unique_ptr<HeadTracker> createUnavailableHeadTracker(const std::string& provider, const std::string& reason) {
    return std::make_unique<UnavailableHeadTracker>(provider, reason);
}

bool airPodsTrackingSupported() {
#if defined(__APPLE__)
    return coreMotionHeadTrackingSupported();
#else
    return false;
#endif
}

std::unique_ptr<HeadTracker> createHeadTracker(HeadTrackerKind kind) {
    switch (kind) {
        case HeadTrackerKind::Mock:
            return std::make_unique<MockHeadTracker>(true);
        case HeadTrackerKind::AirPods:
#if defined(__APPLE__)
            return createCoreMotionHeadTracker();
#else
            return createUnavailableHeadTracker("AirPods", "AirPods head tracking needs macOS 14 or later.");
#endif
        case HeadTrackerKind::Off:
        case HeadTrackerKind::Custom:
            break;
    }
    return nullptr;
}

glm::quat orientationFromRightForwardUp(double x, double y, double z, double w) {
    // A rotation turns the same way in any proper change of axes: its axis is converted like a vector.
    return glm::quat((float)w, (float)x, (float)z, (float)-y);
}

// ---- HeadTrackingController ----

HeadTrackingController::~HeadTrackingController() {
    if (tracker) tracker->stop();
}

void HeadTrackingController::setKind(HeadTrackerKind newKind) {
    if (tracker) tracker->stop();
    tracker = createHeadTracker(newKind);
    kind = tracker ? newKind : HeadTrackerKind::Off;
    recenterRequested = true;
    if (tracker) tracker->start();
}

void HeadTrackingController::setTracker(std::unique_ptr<HeadTracker> custom) {
    if (tracker) tracker->stop();
    tracker = std::move(custom);
    kind = tracker ? HeadTrackerKind::Custom : HeadTrackerKind::Off;
    recenterRequested = true;
    if (tracker) tracker->start();
}

HeadTrackingDiagnostics HeadTrackingController::diagnostics() const {
    if (tracker) return tracker->diagnostics();
    return HeadTrackingDiagnostics{"None", HeadTrackingState::Stopped, true, "", glm::quat(1.0f, 0.0f, 0.0f, 0.0f), 0.0f, 0};
}

glm::quat HeadTrackingController::update(float deltaTime) {
    glm::quat target(1.0f, 0.0f, 0.0f, 0.0f);

    if (tracker) {
        const HeadTrackingDiagnostics now = tracker->diagnostics();
        if (now.state == HeadTrackingState::Tracking) {
            if (recenterRequested) {
                reference = now.orientation;
                recenterRequested = false;
            }
            // The head's turn since "ahead", in the head's own axes, which is what a camera adds.
            const glm::quat relative = glm::inverse(reference) * now.orientation;
            glm::vec3 angles = toEuler(relative) * settings.sensitivity;
            if (!settings.yaw) angles.x = 0.0f;
            if (!settings.pitch) angles.y = 0.0f;
            if (!settings.roll) angles.z = 0.0f;
            target = fromEuler(angles);
        } else {
            // A device that comes back may have lost its sense of direction, so it is recentered.
            recenterRequested = true;
        }
    }

    if (settings.smoothing > 0.0f && deltaTime > 0.0f) {
        const float blend = 1.0f - std::exp(-deltaTime / settings.smoothing);
        rotation = glm::normalize(glm::slerp(rotation, target, blend));
    } else if (settings.smoothing <= 0.0f) {
        rotation = target;
    }
    return rotation;
}

glm::vec3 HeadTrackingController::getEulerDegrees() const {
    return glm::degrees(toEuler(rotation));
}
