// CPU-only, explicitly opt-in investigation. Samples never alter FG acceptance.
bool CameraProbeEnabled() {
    static const bool enabled = [] {
        const auto* value = std::getenv("KYTY_GT7_FSR_FG_CAMERA_PROBE");
        return value != nullptr && std::string_view(value) == "1";
    }();
    return enabled;
}

struct CameraProbeSample {
    std::array<uint64_t, 2> signature {};
    ImageId depth {}, motion {};
    uint64_t frame = 0, address = 0;
    uint32_t draw = 0, user_data_base = 0;
    Gt7FrameGenCamera::Matrix view_projection {}, view {}, projection {}, tail {};
    Gt7FrameGenCamera::Parameters parameters {};
    Gt7FrameGenCamera::RasterState raster {};
    const char* failure = "camera bytes not read";
    const char* tail_failure = "tail not read";
    const char* observer_result = "not observed";
    bool read = false, decoded = false, tail_read = false;
    bool shader_proved = false, recognized = false, full_viewport = false;
    bool runtime_matrix_snapshot = false;
};

struct CameraProbeState {
    static constexpr size_t MaxSamples = 64;
    static constexpr size_t MaxSuccessfulSamples = 16;
    static constexpr size_t MaxSuccessfulTailClass = 8;
    static constexpr size_t MaxPerCategory = 4;
    static constexpr size_t MaxJsonBytes = 1024 * 1024;
    std::vector<CameraProbeSample> pending;
    struct Written {
        uint64_t vs;
        std::string category, bucket;
        bool recognized, successful_current, matching_tail;
    };
    std::vector<Written> written;
    std::filesystem::path directory;
    size_t bytes_written = 0;
    bool failed = false;
};

std::string CameraProbeHex(uint64_t value) {
    char buffer[19] {};
    std::snprintf(buffer, sizeof(buffer), "0x%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

void CollectCameraProbe(CameraProbeState& probe, RenderContext& context,
                        const ShaderStageRuntime& vertex, uint64_t pixel_shader,
                        ImageId depth, ImageId motion, uint64_t frame, uint32_t draw,
                        bool recognized, bool full_viewport,
                        const Gt7FrameGenCamera::RasterState& raster) {
    // A bounded CPU snapshot every fifteenth guest frame is enough to catch a
    // sustained cockpit failure without reading constants on every draw.
    if (frame % 15 != 0 || probe.failed || probe.written.size() >= CameraProbeState::MaxSamples) return;
    // Unknown programs cannot consume the slots needed by the actual runtime
    // producers. Eviction is diagnostic-only and never touches fg_cameras.
    if (!recognized &&
        (std::count_if(probe.pending.begin(), probe.pending.end(), [](const auto& s) { return !s.recognized; }) >= 16 ||
         std::count_if(probe.written.begin(), probe.written.end(), [](const auto& s) { return !s.recognized; }) >= 16)) return;
    if (probe.pending.size() >= CameraProbeState::MaxSamples) {
        const auto unknown = std::find_if(probe.pending.begin(), probe.pending.end(), [](const auto& s) { return !s.recognized; });
        if (!recognized || unknown == probe.pending.end()) return;
        probe.pending.erase(unknown);
    }
    const auto& user = vertex.resources->user_data;
    const auto base = vertex.program->user_data_base;
    const bool has_address = base <= 10 && user.size() >= size_t(12 - base);
    const size_t reg = has_address ? 10 - base : 0;
    const uint64_t address = has_address ? uint64_t(user[reg]) | (uint64_t(user[reg + 1]) << 32u) : 0;
    const std::array<uint64_t, 2> signature {vertex.program->shader_hash, pixel_shader};
    if (std::any_of(probe.pending.begin(), probe.pending.end(), [&](const auto& entry) {
            return entry.signature == signature && entry.address == address &&
                   entry.depth == depth && entry.motion == motion;
        })) return;
    auto& sample = probe.pending.emplace_back();
    sample.signature = signature;
    sample.depth = depth; sample.motion = motion;
    sample.frame = frame; sample.draw = draw;
    sample.user_data_base = base; sample.address = address;
    sample.shader_proved = vertex.program->gt7_camera_position;
    sample.recognized = recognized; sample.full_viewport = full_viewport; sample.raster = raster;
    if (!has_address || !vertex.resources->gpu_fills.empty()) {
        sample.failure = "user-data registers or GPU fills"; return;
    }
    if (!address || (address & 3u) || address > std::numeric_limits<uint64_t>::max() - 272) {
        sample.failure = "camera pointer alignment/range"; return;
    }
    const auto read = [&](uint64_t offset, void* destination, uint64_t bytes, const char*& failure) {
        if (sample.address > std::numeric_limits<uint64_t>::max() - offset - bytes) {
            failure = "camera pointer range"; return false;
        }
        const auto source = sample.address + offset;
        auto& buffers = context.GetBufferCache();
        if (Libs::LibKernel::Memory::TryClampRangeSize(source, bytes) != bytes) {
            failure = "camera matrix range not mapped"; return false;
        }
        if (buffers.HasGpuDirtyBytes(source, bytes)) {
            failure = "camera matrix range GPU dirty"; return false;
        }
        if (!Libs::LibKernel::Memory::TryReadBacking(source, destination, bytes)) {
            failure = "camera backing read failed"; return false;
        }
        if (buffers.HasGpuDirtyBytes(source, bytes)) {
            failure = "camera matrix range became GPU dirty"; return false;
        }
        failure = nullptr;
        return true;
    };
    std::array<float, 48> current {};
    sample.read = read(80, current.data(), sizeof(current), sample.failure);
    if (sample.read) {
        std::copy_n(current.begin(), 16, sample.view_projection.begin());
        std::copy_n(current.begin() + 16, 16, sample.view.begin());
        std::copy_n(current.begin() + 32, 16, sample.projection.begin());
        sample.decoded = Gt7FrameGenCamera::Decode(sample.view_projection, sample.view,
            sample.projection, InputWidth, InputHeight, sample.parameters, &sample.failure);
    }
    // This unproved tail is diagnostic data only, including a failed read or NaN.
    // It is never consumed by the runtime camera decoder or acceptance decision.
    sample.tail_read = read(976, sample.tail.data(), sizeof(sample.tail), sample.tail_failure);
}

void NoteCameraProbeObservation(CameraProbeState& probe, const ShaderStageRuntime& vertex,
                                ImageId depth, ImageId motion, uint64_t frame, const char* result) {
    if (probe.pending.empty()) return;
    const auto& user = vertex.resources->user_data;
    const auto base = vertex.program->user_data_base;
    const bool has_address = base <= 10 && user.size() >= size_t(12 - base);
    const size_t reg = has_address ? 10 - base : 0;
    const uint64_t address = has_address ? uint64_t(user[reg]) | (uint64_t(user[reg + 1]) << 32u) : 0;
    for (auto& sample : probe.pending) {
        if (sample.frame == frame && sample.signature[0] == vertex.program->shader_hash &&
            sample.address == address && sample.depth == depth && sample.motion == motion &&
            (std::string_view(sample.observer_result) == "not observed" ||
             (std::string_view(sample.observer_result) == "accepted" && std::string_view(result) != "accepted"))) {
            sample.observer_result = result;
        }
    }
}

void NoteCameraProbeMatrices(CameraProbeState& probe, const ShaderStageRuntime& vertex,
                            ImageId depth, ImageId motion, uint64_t frame, uint64_t address,
                            const Gt7FrameGenCamera::Matrix& vp, const Gt7FrameGenCamera::Matrix& view,
                            const Gt7FrameGenCamera::Matrix& projection) {
    // If one address was updated between draws, retain the exact current bytes
    // that failed in the runtime observer, not its earlier diagnostic snapshot.
    for (auto& sample : probe.pending) {
        if (sample.frame != frame || sample.signature[0] != vertex.program->shader_hash ||
            sample.address != address || sample.depth != depth || sample.motion != motion) continue;
        sample.view_projection = vp; sample.view = view; sample.projection = projection;
        sample.read = sample.runtime_matrix_snapshot = true;
        sample.decoded = Gt7FrameGenCamera::Decode(vp, view, projection, InputWidth, InputHeight,
                                                  sample.parameters, &sample.failure);
    }
}

void WriteCameraProbes(CameraProbeState& probe, ImageId depth, ImageId motion,
                       uint64_t frame, float jitter_x, float jitter_y) {
    if (probe.failed || probe.written.size() >= CameraProbeState::MaxSamples) return;
    try {
        for (const auto& sample : probe.pending) {
            if (sample.frame != frame || sample.depth != depth || sample.motion != motion) continue;
            if (probe.written.size() >= CameraProbeState::MaxSamples) break;
            if (!sample.recognized && std::count_if(probe.written.begin(), probe.written.end(),
                    [](const auto& entry) { return !entry.recognized; }) >= 16) continue;
            const bool jitter_matches = sample.decoded && sample.parameters.jitter_ndc_x == jitter_x &&
                                                           sample.parameters.jitter_ndc_y == jitter_y;
            const bool raster_matches = sample.full_viewport && Gt7FrameGenCamera::CompatibleRaster(sample.raster);
            nlohmann::json tail_differences = nlohmann::json::array();
            if (sample.tail_read && sample.read) for (size_t i = 0; i < 16; ++i) {
                const float expected = (i == 8 || i == 9) ? 0.0f : sample.projection[i];
                if (sample.tail[i] != expected) tail_differences.push_back(i);
            }
            std::string category;
            if (!sample.read || !sample.decoded) category = sample.failure ? sample.failure : "current matrix invalid";
            else if (!raster_matches) category = "raster rejected";
            else if (!sample.recognized) category = "shader position unproved";
            else if (!jitter_matches) category = "TAA jitter mismatch";
            else if (std::string_view(sample.observer_result) != "accepted") category = sample.observer_result;
            else if (!sample.tail_read) category = "valid current camera with unreadable tail";
            else if (!tail_differences.empty()) category = "valid current camera with different tail";
            else category = "valid current camera and matching tail";
            // A different/unreadable +976 tail is not a current-camera failure.
            // Classify success from the actual contracts and observer outcome,
            // then cap all successful projection modes together. Reserve eight
            // examples for each tail class so neither menu animation nor an
            // arbitrary diagnostic tail can consume the failure budget.
            const bool successful_current = sample.read && sample.decoded && sample.recognized &&
                raster_matches && jitter_matches && std::string_view(sample.observer_result) == "accepted";
            const bool matching_tail = sample.tail_read && tail_differences.empty();
            if (successful_current) {
                const auto successes = std::count_if(probe.written.begin(), probe.written.end(),
                    [](const auto& entry) { return entry.successful_current; });
                const auto tail_class = std::count_if(probe.written.begin(), probe.written.end(),
                    [&](const auto& entry) { return entry.successful_current && entry.matching_tail == matching_tail; });
                if (successes >= CameraProbeState::MaxSuccessfulSamples ||
                    tail_class >= CameraProbeState::MaxSuccessfulTailClass) continue;
            }
            // Different lens/depth modes get separate small quotas. A title
            // camera's failures must not exhaust cockpit/race diagnostics.
            std::string bucket = category + (sample.recognized ? "/recognized" : "/unknown") +
                (raster_matches ? "/raster-valid" : "/raster-invalid");
            for (size_t i : {0u, 5u, 10u, 14u}) bucket += "/" + CameraProbeHex(std::bit_cast<uint32_t>(sample.projection[i]));
            const auto category_count = std::count_if(probe.written.begin(), probe.written.end(),
                [&](const auto& entry) { return entry.bucket == bucket; });
            const auto signature_count = std::count_if(probe.written.begin(), probe.written.end(),
                [&](const auto& entry) { return entry.bucket == bucket && entry.vs == sample.signature[0]; });
            if (category_count >= CameraProbeState::MaxPerCategory || signature_count >= 2) continue;
            const auto words = [](const Gt7FrameGenCamera::Matrix& matrix) {
                return std::bit_cast<std::array<uint32_t, 16>>(matrix);
            };
            nlohmann::json nonfinite = nlohmann::json::array();
            const std::array<const Gt7FrameGenCamera::Matrix*, 3> current {&sample.view_projection, &sample.view, &sample.projection};
            for (size_t m = 0; m < current.size(); ++m) for (size_t i = 0; i < 16; ++i)
                if (!std::isfinite((*current[m])[i])) nonfinite.push_back({{"matrix", m}, {"index", i}});
            const auto& r = sample.raster;
            nlohmann::json record {
                {"schema", 2}, {"diagnostic_only", true}, {"category", category},
                {"guest_frame", frame}, {"candidate_draw", sample.draw},
                {"vertex_shader", CameraProbeHex(sample.signature[0])}, {"pixel_shader", CameraProbeHex(sample.signature[1])},
                {"shader_position_proved", sample.shader_proved}, {"recognized_camera_shader", sample.recognized},
                {"runtime_observer_result", sample.observer_result},
                {"successful_current_diagnostic", successful_current},
                {"current_matrices_from_runtime_failure", sample.runtime_matrix_snapshot},
                {"depth_id", {depth.index, depth.generation}}, {"motion_id", {motion.index, motion.generation}},
                {"matched_current_taa_inputs", true}, {"extent", {InputWidth, InputHeight}},
                {"user_data_base", sample.user_data_base}, {"s10_camera_address", CameraProbeHex(sample.address)},
                {"camera_bytes_read", sample.read}, {"decode_ok", sample.decoded},
                {"failure", sample.failure ? sample.failure : "none"}, {"nonfinite_current_indices", nonfinite},
                {"taa_jitter_ndc", {jitter_x, jitter_y}}, {"taa_jitter_bits", {std::bit_cast<uint32_t>(jitter_x), std::bit_cast<uint32_t>(jitter_y)}},
                {"jitter_matches", jitter_matches}, {"near_plane", sample.parameters.near_plane},
                {"far_plane", sample.parameters.far_plane}, {"fov_y", sample.parameters.fov_y},
                {"camera_position", sample.parameters.position},
                {"view_projection_at_80", sample.view_projection}, {"view_at_144", sample.view}, {"projection_at_208", sample.projection},
                {"diagnostic_tail_at_976", sample.tail}, {"tail_read", sample.tail_read},
                {"tail_read_failure", sample.tail_failure ? sample.tail_failure : "none"}, {"tail_different_indices", tail_differences},
                {"raw_words", {words(sample.view_projection), words(sample.view), words(sample.projection), words(sample.tail)}},
                {"raster", {{"compatible", raster_matches}, {"full_viewport", sample.full_viewport},
                    {"z_scale", r.z_scale}, {"z_offset", r.z_offset},
                    {"z_bits", {std::bit_cast<uint32_t>(r.z_scale), std::bit_cast<uint32_t>(r.z_offset)}},
                    {"position_format", r.position_format}, {"dx_clip_space", r.dx_clip_space},
                    {"min_z_clip", r.min_z_clip_enabled}, {"max_z_clip", r.max_z_clip_enabled},
                    {"poly_offset_front", r.poly_offset_front}, {"poly_offset_back", r.poly_offset_back},
                    {"clip_space_conversion", r.clip_space_conversion}, {"indexed_viewport", r.indexed_viewport},
                    {"layer_output", r.layer_output}, {"layer_limit", r.layer_limit}}}
            };
            const auto json = record.dump(2) + "\n";
            if (json.size() > 16384 || probe.bytes_written + json.size() > CameraProbeState::MaxJsonBytes) {
                probe.failed = true;
                std::printf("GT7 FG camera probe: JSON budget reached; capture stopped.\n"); break;
            }
            if (probe.directory.empty()) {
                const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                probe.directory = std::filesystem::path("_fg-camera-probe") / std::to_string(stamp);
                std::filesystem::create_directories(probe.directory);
            }
            const auto path = probe.directory / ("camera-" + std::to_string(probe.written.size() + 1) + "-" +
                CameraProbeHex(sample.signature[0]) + ".json");
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.exceptions(std::ios::badbit | std::ios::failbit);
            stream.write(json.data(), static_cast<std::streamsize>(json.size())); stream.close();
            probe.bytes_written += json.size();
            probe.written.push_back({sample.signature[0], category, bucket, sample.recognized,
                                     successful_current, matching_tail});
            std::printf("GT7 FG camera probe: sample=%zu frame=%llu vs=%016llx category=%s path=%s\n",
                probe.written.size(), static_cast<unsigned long long>(frame),
                static_cast<unsigned long long>(sample.signature[0]), category.c_str(), path.string().c_str());
            std::fflush(stdout);
        }
    } catch (const std::exception& error) {
        probe.failed = true;
        std::printf("GT7 FG camera probe: capture disabled after write failure: %s\n", error.what());
    }
}
