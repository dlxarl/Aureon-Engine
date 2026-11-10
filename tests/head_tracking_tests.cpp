// Head tracking without hardware: the mock provider, the recentering and filtering that turn raw
// sensor orientation into a camera rotation, and the axis conversion. The game camera's use of
// the rotation is tested in render_tests, which has an OpenGL context to build a player with.
#include "HeadTracking.h"
#include "TestSupport.h"

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <thread>

namespace {
    const glm::vec3 UP(0.0f, 1.0f, 0.0f);
    const glm::vec3 RIGHT(1.0f, 0.0f, 0.0f);

    glm::quat yawBy(float degrees) { return glm::angleAxis(glm::radians(degrees), UP); }
    glm::quat pitchBy(float degrees) { return glm::angleAxis(glm::radians(degrees), RIGHT); }

    bool near(float a, float b, float tolerance = 0.01f) { return std::fabs(a - b) <= tolerance; }

    bool sameRotation(const glm::quat& a, const glm::quat& b, float tolerance = 0.001f) {
        return std::fabs(glm::dot(a, b)) > 1.0f - tolerance;
    }

    // A controller fed by hand, with no smoothing, so that results are exact.
    struct Rig {
        HeadTrackingController controller;
        MockHeadTracker* mock = nullptr;
        double time = 0.0;

        Rig() {
            auto tracker = std::make_unique<MockHeadTracker>(false);
            mock = tracker.get();
            controller.setTracker(std::move(tracker));
            controller.settings.smoothing = 0.0f;
        }
        glm::quat feed(const glm::quat& orientation) {
            mock->injectSample(orientation, time);
            time += 1.0 / 60.0;
            return controller.update(1.0f / 60.0f);
        }
    };

    void testStates() {
        MockHeadTracker mock(false);
        CHECK(mock.diagnostics().state == HeadTrackingState::Stopped);
        mock.start();
        CHECK(mock.diagnostics().state == HeadTrackingState::Waiting);
        CHECK(mock.diagnostics().samples == 0);

        mock.injectSample(yawBy(10.0f), 0.0);
        CHECK(mock.diagnostics().state == HeadTrackingState::Tracking);
        CHECK(mock.diagnostics().samples == 1);
        CHECK(sameRotation(mock.diagnostics().orientation, yawBy(10.0f)));

        mock.simulateDisconnect(true);
        CHECK(mock.diagnostics().state == HeadTrackingState::Waiting);
        CHECK(!mock.diagnostics().error.empty());
        mock.injectSample(yawBy(20.0f), 1.0);   // dropped: the device is gone
        CHECK(mock.diagnostics().samples == 1);
        mock.simulateDisconnect(false);
        mock.injectSample(yawBy(20.0f), 1.0);
        CHECK(mock.diagnostics().state == HeadTrackingState::Tracking);

        mock.simulateError("sensor failed");
        CHECK(mock.diagnostics().state == HeadTrackingState::Error);
        CHECK(mock.diagnostics().error == "sensor failed");

        mock.stop();
        CHECK(mock.diagnostics().state == HeadTrackingState::Stopped);
    }

    void testUpdateRate() {
        MockHeadTracker mock(false);
        mock.start();
        for (int i = 0; i < 180; i++) mock.injectSample(glm::quat(1, 0, 0, 0), i / 60.0);   // 3 s at 60 Hz
        CHECK(near(mock.diagnostics().updateHz, 60.0f, 1.0f));

        MockHeadTracker slow(false);
        slow.start();
        for (int i = 0; i < 40; i++) slow.injectSample(glm::quat(1, 0, 0, 0), i / 20.0);   // 20 Hz
        CHECK(near(slow.diagnostics().updateHz, 20.0f, 1.0f));
    }

    void testAnimatedMock() {
        MockHeadTracker mock(true);
        mock.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(1300));
        const HeadTrackingDiagnostics info = mock.diagnostics();
        CHECK(info.state == HeadTrackingState::Tracking);
        CHECK(info.samples > 30);
        CHECK(info.updateHz > 30.0f && info.updateHz < 90.0f);
        mock.stop();
        CHECK(mock.diagnostics().state == HeadTrackingState::Stopped);
    }

    void testRecenter() {
        Rig rig;
        // The first sample is "straight ahead", whatever direction the sensor thinks that is.
        glm::quat view = rig.feed(yawBy(90.0f));
        CHECK(sameRotation(view, glm::quat(1, 0, 0, 0)));

        view = rig.feed(yawBy(90.0f) * yawBy(25.0f));
        CHECK(sameRotation(view, yawBy(25.0f)));

        rig.controller.recenter();
        view = rig.feed(yawBy(90.0f) * yawBy(25.0f));
        CHECK(sameRotation(view, glm::quat(1, 0, 0, 0)));

        view = rig.feed(yawBy(90.0f) * yawBy(25.0f) * pitchBy(-10.0f));
        CHECK(sameRotation(view, pitchBy(-10.0f)));
    }

    void testSettings() {
        Rig rig;
        rig.feed(glm::quat(1, 0, 0, 0));
        const glm::quat head = yawBy(20.0f) * pitchBy(10.0f);

        rig.controller.settings.sensitivity = 2.0f;
        glm::quat view = rig.feed(head);
        CHECK(sameRotation(view, yawBy(40.0f) * pitchBy(20.0f)));

        rig.controller.settings.sensitivity = 1.0f;
        rig.controller.settings.pitch = false;
        view = rig.feed(head);
        CHECK(sameRotation(view, yawBy(20.0f)));

        rig.controller.settings.yaw = false;
        view = rig.feed(head);
        CHECK(sameRotation(view, glm::quat(1, 0, 0, 0)));
    }

    void testSmoothing() {
        Rig rig;
        rig.feed(glm::quat(1, 0, 0, 0));
        rig.controller.settings.smoothing = 0.1f;

        glm::quat view = rig.feed(yawBy(30.0f));
        const float firstStep = glm::degrees(glm::angle(view));
        CHECK(firstStep > 0.0f && firstStep < 30.0f);   // on its way, not there

        for (int i = 0; i < 120; i++) view = rig.feed(yawBy(30.0f));
        CHECK(near(glm::degrees(glm::angle(view)), 30.0f, 0.1f));
    }

    void testLossReturnsToCenter() {
        Rig rig;
        rig.feed(glm::quat(1, 0, 0, 0));
        rig.feed(yawBy(30.0f));
        rig.mock->simulateDisconnect(true);
        const glm::quat view = rig.controller.update(1.0f / 60.0f);
        CHECK(sameRotation(view, glm::quat(1, 0, 0, 0)));   // no tracking, no turn

        // A device that comes back is recentered, so a changed reference does not jerk the view.
        rig.mock->simulateDisconnect(false);
        const glm::quat back = rig.feed(yawBy(140.0f));
        CHECK(sameRotation(back, glm::quat(1, 0, 0, 0)));
    }

    void testAxisConversion() {
        // Sensor frame: x right, y forward, z up. Turning left is a positive turn about z.
        const float half = std::sin(glm::radians(10.0f));
        const glm::quat converted = orientationFromRightForwardUp(0.0, 0.0, half, std::cos(glm::radians(10.0f)));
        CHECK(sameRotation(converted, yawBy(20.0f)));

        // Nodding down is a turn about the sensor's x axis, and stays about the engine's x axis.
        const glm::quat nod = orientationFromRightForwardUp(-half, 0.0, 0.0, std::cos(glm::radians(10.0f)));
        CHECK(sameRotation(nod, pitchBy(-20.0f)));

        // A looked-at direction agrees in both frames: sensor "forward" (0,1,0) is engine -Z.
        const glm::vec3 forward = converted * glm::vec3(0.0f, 0.0f, -1.0f);
        CHECK(near(forward.x, -std::sin(glm::radians(20.0f))));
        CHECK(near(forward.z, -std::cos(glm::radians(20.0f))));
    }

    void testProviders() {
        CHECK(createHeadTracker(HeadTrackerKind::Off) == nullptr);
        CHECK(createHeadTracker(HeadTrackerKind::Custom) == nullptr);
        CHECK(createHeadTracker(HeadTrackerKind::Mock) != nullptr);

        // AirPods always yields a provider: a working one, or one that says why it cannot.
        auto airpods = createHeadTracker(HeadTrackerKind::AirPods);
        CHECK(airpods != nullptr);
        if (!airPodsTrackingSupported()) {
            airpods->start();
            const HeadTrackingDiagnostics info = airpods->diagnostics();
            CHECK(info.state == HeadTrackingState::Unavailable);
            CHECK(!info.available);
            CHECK(!info.error.empty());
        }

        HeadTrackingController controller;
        controller.setKind(HeadTrackerKind::Mock);
        CHECK(controller.getKind() == HeadTrackerKind::Mock);
        controller.setKind(HeadTrackerKind::Off);
        CHECK(controller.getTracker() == nullptr);
        CHECK(controller.diagnostics().state == HeadTrackingState::Stopped);
        CHECK(sameRotation(controller.update(0.016f), glm::quat(1, 0, 0, 0)));
    }
}

int main() {
    testStates();
    testUpdateRate();
    testAnimatedMock();
    testRecenter();
    testSettings();
    testSmoothing();
    testLossReturnsToCenter();
    testAxisConversion();
    testProviders();
    return testSummary("head_tracking_tests");
}
