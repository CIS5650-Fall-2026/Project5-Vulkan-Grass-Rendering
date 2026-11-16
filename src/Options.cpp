#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include "Options.h"

namespace {
    const float DEFAULT_PIVOT_DISTANCE = 10.0f;

    const char* nextArg(int argc, char** argv, int& i) {
        if (i + 1 >= argc) {
            throw std::runtime_error(std::string("Missing value after ") + argv[i]);
        }
        return argv[++i];
    }

    ViewportPose parsePose(const char* text) {
        ViewportPose pose = {};
        if (sscanf(text, "%f,%f,%f,%f,%f", &pose.position.x, &pose.position.y, &pose.position.z, &pose.yaw, &pose.pitch) != 5) {
            throw std::runtime_error(std::string("Bad --pose ") + text + ", expected x,y,z,yaw,pitch");
        }
        pose.pivotDistance = DEFAULT_PIVOT_DISTANCE;
        return pose;
    }
}

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!strcmp(arg, "--blades")) {
            options.blades = static_cast<unsigned int>(strtoul(nextArg(argc, argv, i), nullptr, 10));
        } else if (!strcmp(arg, "--atomic")) {
            options.subgroupCompaction = false;
        } else if (!strcmp(arg, "--no-orientation")) {
            options.orientationCulling = false;
        } else if (!strcmp(arg, "--no-frustum")) {
            options.frustumCulling = false;
        } else if (!strcmp(arg, "--no-distance")) {
            options.distanceCulling = false;
        } else if (!strcmp(arg, "--no-cull")) {
            options.orientationCulling = options.frustumCulling = options.distanceCulling = false;
        } else if (!strcmp(arg, "--no-forces")) {
            options.forces = false;
        } else if (!strcmp(arg, "--no-wind")) {
            options.wind = false;
        } else if (!strcmp(arg, "--host-visible-culled")) {
            options.hostVisibleCulled = true;
        } else if (!strcmp(arg, "--pose")) {
            options.pose = parsePose(nextArg(argc, argv, i));
            options.hasPose = true;
        } else if (!strcmp(arg, "--bench")) {
            options.bench = true;
        } else if (!strcmp(arg, "--warmup")) {
            options.warmupFrames = atoi(nextArg(argc, argv, i));
        } else if (!strcmp(arg, "--timed-frames")) {
            options.timedFrames = atoi(nextArg(argc, argv, i));
        } else if (!strcmp(arg, "--csv")) {
            options.csvPath = nextArg(argc, argv, i);
        } else if (!strcmp(arg, "--tag")) {
            options.tag = nextArg(argc, argv, i);
        } else if (!strcmp(arg, "--capture")) {
            options.capturePath = nextArg(argc, argv, i);
        } else if (!strcmp(arg, "--record")) {
            options.recordDir = nextArg(argc, argv, i);
            options.recordFrames = atoi(nextArg(argc, argv, i));
        } else {
            throw std::runtime_error(std::string("Unknown option ") + arg);
        }
    }
    if (options.blades == 0) {
        throw std::runtime_error("--blades must be positive");
    }
    return options;
}

std::string FormatPose(const ViewportPose& pose) {
    char text[128];
    snprintf(text, sizeof(text), "%.3f,%.3f,%.3f,%.4f,%.4f", pose.position.x, pose.position.y, pose.position.z, pose.yaw, pose.pitch);
    return text;
}
