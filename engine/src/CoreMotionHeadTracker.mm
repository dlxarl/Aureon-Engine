// Head tracking from AirPods through Core Motion's CMHeadphoneMotionManager (macOS 14 or later).
//
// Compiled with ARC. Samples arrive on a private queue; everything they touch is behind the
// lock in HeadTrackerBase, and stop() waits for the queue, so no sample outlives the tracker.
#include "CoreMotionHeadTracker.h"

#import <CoreMotion/CoreMotion.h>
#import <Foundation/Foundation.h>

#include <functional>

API_AVAILABLE(macos(14.0))
@interface AureonHeadphoneDelegate : NSObject <CMHeadphoneMotionManagerDelegate>
@property(nonatomic, copy) void (^connected)(void);
@property(nonatomic, copy) void (^disconnected)(void);
@end

@implementation AureonHeadphoneDelegate
- (void)headphoneMotionManagerDidConnect:(CMHeadphoneMotionManager*)manager {
    if (self.connected) self.connected();
}
- (void)headphoneMotionManagerDidDisconnect:(CMHeadphoneMotionManager*)manager {
    if (self.disconnected) self.disconnected();
}
@end

namespace {
    const char* const NOT_SUPPORTED = "AirPods head tracking needs macOS 14 or later.";

    class CoreMotionHeadTracker : public HeadTrackerBase {
    public:
        CoreMotionHeadTracker() : HeadTrackerBase("AirPods (Core Motion)") {}
        ~CoreMotionHeadTracker() override { stop(); }

        const char* name() const override { return "AirPods"; }

        void start() override {
            if (@available(macOS 14.0, *)) {
                begin();
            } else {
                setState(HeadTrackingState::Unavailable, NOT_SUPPORTED);
            }
        }

        void stop() override {
            if (@available(macOS 14.0, *)) {
                end();
            }
        }

    private:
        // Held as plain ids so that the class itself needs no availability annotations.
        id manager = nil;
        id delegate = nil;
        NSOperationQueue* queue = nil;

        void begin() API_AVAILABLE(macos(14.0)) {
            if (manager) return;

            // Without this key macOS ends the process the moment motion is requested. A bare
            // executable has none, so say so instead of dying. (The key is necessary, not
            // sufficient: access is also judged against the program that launched this one, which
            // is why the Editor should be opened as an app, not from another app's terminal.)
            if (![[NSBundle mainBundle] objectForInfoDictionaryKey:@"NSMotionUsageDescription"]) {
                setState(HeadTrackingState::Error,
                         "This program has no NSMotionUsageDescription in its Info.plist, so macOS "
                         "would refuse motion access. Use the Editor app bundle.");
                return;
            }

            switch ([CMHeadphoneMotionManager authorizationStatus]) {
                case CMAuthorizationStatusDenied:
                case CMAuthorizationStatusRestricted:
                    setState(HeadTrackingState::Error,
                             "Motion & Fitness access is denied. Allow it in System Settings > "
                             "Privacy & Security > Motion & Fitness.");
                    return;
                default:
                    break;
            }

            CMHeadphoneMotionManager* created = [[CMHeadphoneMotionManager alloc] init];
            AureonHeadphoneDelegate* observer = [[AureonHeadphoneDelegate alloc] init];
            observer.connected = ^{ setState(HeadTrackingState::Waiting, "AirPods connected, waiting for motion data"); };
            observer.disconnected = ^{ setState(HeadTrackingState::Waiting, "AirPods disconnected"); };
            created.delegate = observer;   // weak in Core Motion, so this object keeps it alive

            resetStatistics();
            if (created.deviceMotionAvailable) {
                setState(HeadTrackingState::Waiting, "Waiting for AirPods motion data");
            } else {
                setState(HeadTrackingState::Waiting,
                         "No headphones with motion sensors reported. Connect AirPods (3rd generation), "
                         "AirPods Pro or AirPods Max.");
            }

            NSOperationQueue* motionQueue = [[NSOperationQueue alloc] init];
            motionQueue.maxConcurrentOperationCount = 1;
            motionQueue.name = @"aureon.headtracking";

            [created startConnectionStatusUpdates];
            [created startDeviceMotionUpdatesToQueue:motionQueue
                                         withHandler:^(CMDeviceMotion* motion, NSError* error) {
                if (error) {
                    setState(HeadTrackingState::Error, [[error localizedDescription] UTF8String]);
                    return;
                }
                if (!motion) return;
                const CMQuaternion q = motion.attitude.quaternion;
                publishSample(orientationFromRightForwardUp(q.x, q.y, q.z, q.w), motion.timestamp);
            }];

            manager = created;
            delegate = observer;
            queue = motionQueue;
        }

        void end() API_AVAILABLE(macos(14.0)) {
            if (!manager) return;
            CMHeadphoneMotionManager* active = manager;
            [active stopDeviceMotionUpdates];
            [active stopConnectionStatusUpdates];
            active.delegate = nil;
            [queue cancelAllOperations];
            [queue waitUntilAllOperationsAreFinished];
            manager = nil;
            delegate = nil;
            queue = nil;
            setState(HeadTrackingState::Stopped);
        }
    };
}

bool coreMotionHeadTrackingSupported() {
    if (@available(macOS 14.0, *)) return true;
    return false;
}

std::unique_ptr<HeadTracker> createCoreMotionHeadTracker() {
    if (!coreMotionHeadTrackingSupported()) return createUnavailableHeadTracker("AirPods", NOT_SUPPORTED);
    return std::make_unique<CoreMotionHeadTracker>();
}
