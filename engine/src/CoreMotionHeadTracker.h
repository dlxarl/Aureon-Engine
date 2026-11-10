#pragma once
#include "HeadTracking.h"

#include <memory>

// The AirPods provider. Only HeadTracking.cpp includes this, so no Apple type leaves the .mm file.
bool coreMotionHeadTrackingSupported();
std::unique_ptr<HeadTracker> createCoreMotionHeadTracker();
