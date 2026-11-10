// aureon_headtrack: prints what the mock head tracker reports, without the Editor.
//
//   aureon_headtrack [--seconds 10]
//
// One line a second: state, update rate, the raw quaternion and the view offset after recentering.
// Use it to check that the tracking pipeline runs on this machine. AirPods are tried from the Editor
// (Window > Head Tracking), not here: macOS ends a bare tool that asks for motion data when the program
// that started it has no NSMotionUsageDescription, and only the Editor app bundle has one.
#include "HeadTracking.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

int main(int argc, char** argv) {
    int seconds = 10;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) {
            seconds = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "usage: aureon_headtrack [--seconds N]\n");
            return 2;
        }
    }

    HeadTrackingController head;
    head.settings.smoothing = 0.0f;
    head.setKind(HeadTrackerKind::Mock);

    auto next = std::chrono::steady_clock::now();
    for (int tick = 0; tick < seconds * 20; tick++) {
        head.update(0.05f);
        if (tick % 20 == 0) {
            const HeadTrackingDiagnostics d = head.diagnostics();
            const glm::vec3 angles = head.getEulerDegrees();
            std::printf("%-9s %5.1f Hz  q(w %.3f x %.3f y %.3f z %.3f)  yaw %6.1f pitch %6.1f roll %6.1f  %s\n",
                        toString(d.state), d.updateHz, d.orientation.w, d.orientation.x, d.orientation.y,
                        d.orientation.z, angles.x, angles.y, angles.z, d.error.c_str());
            std::fflush(stdout);
        }
        next += std::chrono::milliseconds(50);
        std::this_thread::sleep_until(next);
    }
    return head.diagnostics().state == HeadTrackingState::Tracking ? 0 : 1;
}
