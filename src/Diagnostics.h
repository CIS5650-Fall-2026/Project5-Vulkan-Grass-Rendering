#pragma once

#include "Renderer.h"
#include "CullingReference.h"
#include <fstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <limits>

class GrassDiagnostics {
    std::ofstream csv;
    std::vector<Blade> previous;
    std::string csvPath;
    bool savedReferenceDifference = false;
public:
    explicit GrassDiagnostics(const std::string& path) {
        if (path.empty()) return;
        csvPath = path;
        csv.open(path);
        if (!csv) throw std::runtime_error("Cannot create diagnostics: " + path);
        csv << "frame,simulation_steps,simulation_time,paused,force_mask,blades,non_finite,root_drift,packed_drift,min_tip_height,max_guide_lateral,max_estimated_length_error,max_sampled_length_error,mean_rest_distance,max_initial_delta,max_previous_delta,state_output_equal,draw_count_ok,culling_mask,visible_count,culled_count,cpu_frustum_rejected,cpu_orientation_rejected,cpu_distance_rejected,invalid_ids,duplicate_ids,output_mapping_ok,cpu_reference_mismatch,cpu_boundary_disagreements,visibility_hash,camera_x,camera_y,camera_z\n";
        csv << std::setprecision(10);
    }
    bool Enabled() const { return csv.is_open(); }
    void Record(uint32_t frame, const Scene& scene, const Blades& blades, const GrassSnapshot& snapshot) {
        double rootDrift = 0, packedDrift = 0, minHeight = std::numeric_limits<double>::max();
        double guideLateral = 0, estimatedError = 0, sampledError = 0, restDistance = 0;
        double initialDelta = 0, previousDelta = 0;
        uint32_t nonFinite = 0;
        for (size_t i = 0; i < snapshot.state.size(); ++i) {
            const Blade& b = snapshot.state[i];
            const Blade& original = blades.GetInitialBlades()[i];
            const glm::vec4 values[] = { b.v0, b.v1, b.v2, b.up };
            const glm::vec4 originals[] = { original.v0, original.v1, original.v2, original.up };
            bool finite = true;
            for (int j = 0; j < 4; ++j) for (int k = 0; k < 4; ++k) {
                finite = finite && std::isfinite(values[j][k]);
                initialDelta = std::max(initialDelta, static_cast<double>(std::abs(values[j][k] - originals[j][k])));
            }
            if (!finite) { ++nonFinite; continue; }
            glm::dvec3 root(b.v0), guide(b.v1), tip(b.v2), up = glm::normalize(glm::dvec3(b.up));
            const double height = b.v1.w;
            rootDrift = std::max(rootDrift, glm::length(root - glm::dvec3(original.v0)));
            for (int j = 0; j < 4; ++j) packedDrift = std::max(packedDrift, static_cast<double>(std::abs(values[j].w - originals[j].w)));
            packedDrift = std::max(packedDrift, glm::length(up - glm::normalize(glm::dvec3(original.up))));
            minHeight = std::min(minHeight, glm::dot(tip - root, up));
            guideLateral = std::max(guideLateral, glm::length((guide - root) - up * glm::dot(guide - root, up)));
            const double estimate = (2 * glm::length(tip - root) + glm::length(guide - root) + glm::length(tip - guide)) / 3;
            estimatedError = std::max(estimatedError, std::abs(estimate / height - 1));
            glm::dvec3 last = root;
            double length = 0;
            for (int sample = 1; sample <= 64; ++sample) {
                const double t = sample / 64.0;
                glm::dvec3 position = (1-t)*(1-t)*root + 2*(1-t)*t*guide + t*t*tip;
                length += glm::length(position - last);
                last = position;
            }
            sampledError = std::max(sampledError, std::abs(length / height - 1));
            restDistance += glm::length(tip - (root + height * up));
            if (!previous.empty()) previousDelta = std::max(previousDelta, glm::length(tip - glm::dvec3(previous[i].v2)));
        }
        const bool equal = snapshot.output.size() == snapshot.state.size()
            && memcmp(snapshot.state.data(), snapshot.output.data(), snapshot.state.size() * sizeof(Blade)) == 0;
        bool drawOK = snapshot.draw.vertexCount <= blades.GetBladeCount() && snapshot.draw.instanceCount == 1
            && snapshot.draw.firstVertex == 0 && snapshot.draw.firstInstance == 0;
        std::vector<uint8_t> seen(snapshot.state.size(), 0);
        uint32_t invalid = 0, duplicates = 0, mismatches = 0, boundaryDisagreements = 0, rejected[4] = {};
        bool mapping = snapshot.output.size() == snapshot.visibleIds.size();
        for (size_t slot = 0; slot < snapshot.visibleIds.size(); ++slot) {
            const uint32_t id = snapshot.visibleIds[slot];
            if (id >= seen.size()) { ++invalid; mapping = false; continue; }
            if (seen[id]) ++duplicates;
            seen[id] = 1;
            if (memcmp(&snapshot.output[slot], &snapshot.state[id], sizeof(Blade)) != 0) mapping = false;
        }
        uint64_t visibilityHash = 14695981039346656037ull;
        for (uint32_t id = 0; id < seen.size(); ++id) {
            bool orientationBoundary = false;
            const unsigned reason = CullingReference(snapshot.state[id], id, snapshot, &orientationBoundary);
            ++rejected[reason];
            if (bool(seen[id]) != (reason == 0)) {
                if (orientationBoundary) ++boundaryDisagreements;
                else ++mismatches;
            }
            visibilityHash = (visibilityHash ^ seen[id]) * 1099511628211ull;
        }
        if ((mismatches || boundaryDisagreements) && !savedReferenceDifference) {
            Dump(csvPath + ".reference-difference", blades, snapshot);
            savedReferenceDifference = true;
        }
        csv << frame << ',' << scene.GetSimulationSteps() << ',' << scene.GetSimulationTime() << ','
            << scene.IsPaused() << ',' << scene.GetSimulation().forceMask << ',' << snapshot.state.size() << ','
            << nonFinite << ',' << rootDrift << ',' << packedDrift << ',' << minHeight << ',' << guideLateral << ','
            << estimatedError << ',' << sampledError << ',' << restDistance / snapshot.state.size() << ','
            << initialDelta << ',' << previousDelta << ',' << equal << ',' << drawOK << ','
            << snapshot.simulation.cullingMask << ',' << snapshot.draw.vertexCount << ','
            << (int64_t(snapshot.state.size()) - snapshot.draw.vertexCount) << ','
            << rejected[1] << ',' << rejected[2] << ',' << rejected[3] << ',' << invalid << ',' << duplicates << ','
            << mapping << ',' << mismatches << ',' << boundaryDisagreements << ',' << visibilityHash << ','
            << snapshot.camera.position.x << ',' << snapshot.camera.position.y << ',' << snapshot.camera.position.z << '\n';
        csv.flush();
        previous = snapshot.state;
        if (!csv) throw std::runtime_error("Failed to write grass diagnostics");
    }
    static void Dump(const std::string& prefix, const Blades& blades, const GrassSnapshot& snapshot) {
        const std::vector<Blade>* data[] = { &blades.GetInitialBlades(), &snapshot.state, &snapshot.output };
        const char* suffix[] = { ".initial.bin", ".state.bin", ".output.bin" };
        for (int i = 0; i < 3; ++i) {
            std::ofstream file(prefix + suffix[i], std::ios::binary);
            file.write(reinterpret_cast<const char*>(data[i]->data()), data[i]->size() * sizeof(Blade));
            if (!file) throw std::runtime_error("Failed to dump grass state");
        }
        std::ofstream ids(prefix + ".ids.bin", std::ios::binary);
        ids.write(reinterpret_cast<const char*>(snapshot.visibleIds.data()), snapshot.visibleIds.size() * sizeof(uint32_t));
        if (!ids) throw std::runtime_error("Failed to dump visible IDs");
        std::ofstream metadata(prefix + ".json");
        metadata << std::setprecision(10) << "{\n\"blade_count\":" << blades.GetBladeCount()
            << ",\n\"visible_count\":" << snapshot.draw.vertexCount
            << ",\n\"culling_mask\":" << snapshot.simulation.cullingMask
            << ",\n\"distance_buckets\":" << snapshot.simulation.distanceBuckets
            << ",\n\"max_distance\":" << snapshot.simulation.maxDistance
            << ",\n\"orientation_threshold\":" << snapshot.simulation.orientationThreshold
            << ",\n\"frustum_tolerance\":" << snapshot.simulation.cullingSettings.x
            << ",\n\"lod_enabled\":" << snapshot.simulation.lodControls.x
            << ",\n\"lod_colors\":" << snapshot.simulation.lodControls.y
            << ",\n\"lod_near\":" << snapshot.simulation.lodDistances.x
            << ",\n\"lod_far\":" << snapshot.simulation.lodDistances.y
            << ",\n\"lod_max\":" << snapshot.simulation.lodDistances.z
            << ",\n\"lod_min\":" << snapshot.simulation.lodDistances.w
            << ",\n\"contact_enabled\":" << snapshot.simulation.contactControls.x
            << ",\n\"sphere_visible\":" << snapshot.simulation.contactControls.y
            << ",\n\"sphere_motion\":" << snapshot.simulation.contactControls.z
            << ",\n\"contact_strength\":" << snapshot.simulation.contactSettings.x
            << ",\n\"contact_skin\":" << snapshot.simulation.contactSettings.y
            << ",\n\"contact_step_cap\":" << snapshot.simulation.contactSettings.z
            << ",\n\"sphere_radius\":" << snapshot.simulation.spherePath[0].w
            << ",\n\"camera_position\":[" << snapshot.camera.position.x << ',' << snapshot.camera.position.y << ',' << snapshot.camera.position.z << ']';
        const auto sphere = snapshot.simulation.spherePath[snapshot.simulation.stepCount];
        metadata << ",\n\"sphere_position\":[" << sphere.x << ',' << sphere.y << ',' << sphere.z << ']';
        const auto matrix = [&](const char* name, const glm::mat4& value) {
            metadata << ",\n\"" << name << "\":[";
            for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row)
                metadata << (column || row ? "," : "") << value[column][row];
            metadata << "]";
        };
        matrix("view_column_major", snapshot.camera.viewMatrix);
        matrix("projection_column_major", snapshot.camera.projectionMatrix);
        matrix("model_column_major", snapshot.model);
        metadata << "\n}\n";
        if (!metadata) throw std::runtime_error("Failed to dump culling metadata");
    }
};
