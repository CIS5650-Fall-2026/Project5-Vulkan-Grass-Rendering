#pragma once

#include <string>
#include "Camera.h"

// Command-line settings; the defaults reproduce the interactive app.
struct Options {
    unsigned int blades = 1 << 20;

    // compute shader specialization constants
    bool subgroupCompaction = true;
    bool orientationCulling = true;
    bool frustumCulling = true;
    bool distanceCulling = true;
    bool forces = true;
    bool wind = true;

    // the starter code's memory type for the culled blade buffer
    bool hostVisibleCulled = false;

    bool hasPose = false;
    ViewportPose pose = {};

    bool bench = false;
    int warmupFrames = 300;
    int timedFrames = 500;
    std::string csvPath;
    std::string tag;

    std::string capturePath;
    std::string recordDir;
    int recordFrames = 0;

    // simulation steps a fixed 1/60 s per frame so runs and recordings repeat
    bool FixedStep() const {
        return bench || !capturePath.empty() || !recordDir.empty();
    }
};

Options ParseOptions(int argc, char** argv);

// "x,y,z,yaw,pitch", the format --pose reads and the P key prints
std::string FormatPose(const ViewportPose& pose);
