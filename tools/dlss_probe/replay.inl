// Included in the standalone probe's private namespace. Never linked into the emulator.
// Same-resolution real capture replay validates GPU plumbing, not SR performance or quality.
std::filesystem::path ReplayPath(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

void ValidateReplay(const Json& sequence) {
    if (sequence.at("schema") != "gt7.ngx-replay.v1")
        throw std::runtime_error("Unsupported replay sequence schema");
    const auto width = sequence.at("width").get<unsigned>();
    const auto height = sequence.at("height").get<unsigned>();
    if (width < 64 || height < 64 || width > 4096 || height > 4096)
        throw std::runtime_error("Replay dimensions must be between 64 and 4096");
    const auto& frames = sequence.at("frames");
    if (!frames.is_array() || frames.size() < 2 || frames.size() > 8)
        throw std::runtime_error("Replay requires 2 to 8 consecutive captured frames");
    unsigned previous {};
    for (const auto& frame : frames) {
        const auto capture = frame.at("capture").get<unsigned>();
        if (!capture || (previous && capture != previous + 1))
            throw std::runtime_error("Replay capture numbers are not consecutive");
        previous = capture;
        const auto& jitter = frame.at("jitter");
        if (!jitter.is_array() || jitter.size() != 2)
            throw std::runtime_error("Replay needs both jitter coordinates");
        for (const auto& value : jitter) {
            const auto component = value.get<float>();
            if (!std::isfinite(component) || std::abs(component) > 0.5f)
                throw std::runtime_error("Replay jitter must be finite pixel offsets within [-0.5,0.5]");
        }
        const auto duration = frame.at("frame_time_ms").get<float>();
        if (!std::isfinite(duration) || duration <= 0.0f || duration > 1000.0f)
            throw std::runtime_error("Invalid replay frame time");
        if (frame.at("invalid_motion_pixels").get<uint64_t>() > uint64_t(width) * height)
            throw std::runtime_error("Invalid replay mask count");
        for (const auto* key : {"color", "depth", "motion", "bias"}) {
            const auto path = ReplayPath(frame.at(key).get<std::string>());
            // Generated sequence files use sibling payloads. Do not interpret arbitrary paths.
            if (path.empty() || path.has_parent_path() || path.has_root_path() || path.filename() != path)
                throw std::runtime_error("Replay payloads must be simple sibling filenames");
        }
    }
}

void ReadReplayPayload(const std::filesystem::path& path, void* destination, size_t bytes) {
    if (std::filesystem::file_size(path) != bytes)
        throw std::runtime_error("Replay payload size mismatch: " + path.filename().string());
    std::ifstream input(path, std::ios::binary);
    input.exceptions(std::ios::badbit | std::ios::failbit);
    input.read(static_cast<char*>(destination), static_cast<std::streamsize>(bytes));
}

void EvaluateReplay(Context& context, VkPhysicalDevice physical, uint32_t queue_family,
                    const Json& sequence, const std::filesystem::path& input_directory,
                    const std::filesystem::path& output_directory, Json& report) {
    ValidateReplay(sequence);
    const auto width = sequence.at("width").get<unsigned>();
    const auto height = sequence.at("height").get<unsigned>();
    const size_t pixels = size_t(width) * height;
    report["replay"] = {
        {"mode", "native-resolution DLAA diagnostic; no upscaling"},
        {"input", {width, height}}, {"output", {width, height}},
        {"gt7_data_used", true}, {"gt7_integration_tested", false},
        {"temporal_quality_verified", false}, {"performance_verified", false},
        {"requested_preset", "K"}, {"effective_preset_verified", false},
        {"persistent_feature", true}, {"reset_policy", "first frame only"},
        {"exposure", "experimental NGX auto exposure; pre-exposure assumed 1, game exposure unverified"},
        {"invalid_motion", "zero placeholder only; a separate invalid mask is validated and retained in the sequence, not supplied as BiasCurrentColor because preset K does not support it. Invalid regions are not quality-validated."},
        {"bias_current_color_supplied", false},
        {"frame_time", "nominal simulation interval from sequence; not measured GPU/capture time"},
        {"frames", Json::array()}};
    Synthetic gpu(context, physical);
    gpu.GetDeviceQueue(context.device, queue_family, 0, &gpu.queue);
    VkCommandPoolCreateInfo pool {};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.queueFamilyIndex = queue_family;
    CheckVk(gpu.CreateCommandPool(context.device, &pool, nullptr, &gpu.pool), "Create replay command pool");
    VkCommandBufferAllocateInfo allocate {};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = gpu.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    CheckVk(gpu.AllocateCommandBuffers(context.device, &allocate, &gpu.command), "Allocate replay command buffer");
    gpu.MakeImage(gpu.images[0], width, height, VK_FORMAT_R16G16B16A16_SFLOAT, false);
    gpu.MakeImage(gpu.images[1], width, height, VK_FORMAT_R32_SFLOAT, false);
    gpu.MakeImage(gpu.images[2], width, height, VK_FORMAT_R32G32_SFLOAT, false);
    gpu.MakeImage(gpu.images[3], width, height, VK_FORMAT_R16G16B16A16_SFLOAT, true);
    const std::array<VkDeviceSize, 4> offsets {0, pixels * 8, pixels * 12, pixels * 20};
    const std::array<size_t, 4> sizes {pixels * 8, pixels * 4, pixels * 8, pixels};
    const std::array<unsigned, 3> indices {0, 1, 2};
    const std::array<const char*, 4> keys {"color", "depth", "motion", "bias"};
    gpu.MakeBuffer(gpu.buffers[0], pixels * 20, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    gpu.MakeBuffer(gpu.buffers[1], pixels * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkFenceCreateInfo fence {};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    CheckVk(gpu.CreateFence(context.device, &fence, nullptr, &gpu.fence), "Create replay fence");
    CheckNgx(report, "AllocateParameters_replay", NVSDK_NGX_VULKAN_AllocateParameters(&gpu.parameters));
    NVSDK_NGX_Parameter_SetUI(gpu.parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
                             NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    bool first = true;
    for (const auto& frame : sequence.at("frames")) {
        const auto capture = frame.at("capture").get<unsigned>();
        if (!first) {
            CheckVk(gpu.ResetFences(context.device, 1, &gpu.fence), "Reset replay fence");
            CheckVk(gpu.ResetCommandPool(context.device, gpu.pool, 0), "Reset replay command pool");
        }
        // Load and validate before mapping GPU memory so errors never leave a mapping alive.
        std::vector<std::byte> upload(pixels * 21);
        for (size_t i = 0; i < keys.size(); ++i)
            ReadReplayPayload(input_directory / ReplayPath(frame.at(keys[i]).get<std::string>()),
                              upload.data() + offsets[i], sizes[i]);
        const auto* colors = reinterpret_cast<const uint16_t*>(upload.data());
        const auto* depth = reinterpret_cast<const float*>(upload.data() + offsets[1]);
        const auto* motion = reinterpret_cast<const float*>(upload.data() + offsets[2]);
        const auto* bias = reinterpret_cast<const uint8_t*>(upload.data() + offsets[3]);
        uint64_t masked {};
        for (size_t pixel = 0; pixel < pixels; ++pixel) {
            for (size_t channel = 0; channel < 4; ++channel)
                if (!std::isfinite(ReadHalf(colors[pixel * 4 + channel])))
                    throw std::runtime_error("Non-finite replay color input");
            if (!std::isfinite(depth[pixel]) || depth[pixel] < 0.0f || depth[pixel] > 1.0f ||
                !std::isfinite(motion[pixel * 2]) || !std::isfinite(motion[pixel * 2 + 1]))
                throw std::runtime_error("Invalid replay depth or motion input");
            if (bias[pixel] != 0 && bias[pixel] != 255)
                throw std::runtime_error("Replay bias mask is not binary");
            if (bias[pixel]) ++masked;
        }
        if (masked != frame.at("invalid_motion_pixels").get<uint64_t>())
            throw std::runtime_error("Replay mask count does not match manifest");
        void* mapped {};
        CheckVk(gpu.MapMemory(context.device, gpu.buffers[0].memory, 0, VK_WHOLE_SIZE, 0, &mapped), "Map replay upload");
        std::memcpy(mapped, upload.data(), pixels * 20);
        gpu.UnmapMemory(context.device, gpu.buffers[0].memory);
        VkCommandBufferBeginInfo begin {};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CheckVk(gpu.BeginCommandBuffer(gpu.command, &begin), "Begin replay command buffer");
        for (size_t i = 0; i < indices.size(); ++i) {
            const auto& texture = gpu.images[indices[i]];
            gpu.Transition(texture, first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, first ? 0 : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            const auto region = Synthetic::CopyRegion(texture, offsets[i]);
            gpu.CmdCopyBufferToImage(gpu.command, gpu.buffers[0].buffer, texture.image,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            gpu.Transition(texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        }
        gpu.Transition(gpu.images[3], first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, first ? 0 : VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkClearColorValue sentinel {};
        for (auto& value : sentinel.float32) value = std::numeric_limits<float>::quiet_NaN();
        const VkImageSubresourceRange range {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        gpu.CmdClearColorImage(gpu.command, gpu.images[3].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &sentinel, 1, &range);
        gpu.Transition(gpu.images[3], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT);
        if (first) {
            NVSDK_NGX_DLSS_Create_Params settings {};
            settings.Feature.InWidth = width; settings.Feature.InHeight = height;
            settings.Feature.InTargetWidth = width; settings.Feature.InTargetHeight = height;
            settings.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
            settings.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
                NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_DepthInverted |
                NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
            CheckNgx(report, "CREATE_DLAA_replay", NGX_VULKAN_CREATE_DLSS_EXT1(
                context.device, gpu.command, 1, 1, &gpu.feature, gpu.parameters, &settings));
        }
        auto color = Synthetic::Resource(gpu.images[0], false);
        auto depth_resource = Synthetic::Resource(gpu.images[1], false);
        auto mv = Synthetic::Resource(gpu.images[2], false);
        auto output = Synthetic::Resource(gpu.images[3], true);
        NVSDK_NGX_VK_DLSS_Eval_Params evaluate {};
        evaluate.Feature.pInColor = &color; evaluate.Feature.pInOutput = &output;
        evaluate.pInDepth = &depth_resource; evaluate.pInMotionVectors = &mv;
        evaluate.InRenderSubrectDimensions = {width, height};
        evaluate.InReset = first ? 1 : 0;
        evaluate.InJitterOffsetX = frame.at("jitter").at(0).get<float>();
        evaluate.InJitterOffsetY = frame.at("jitter").at(1).get<float>();
        evaluate.InMVScaleX = 1.0f; evaluate.InMVScaleY = 1.0f;
        evaluate.InPreExposure = 1.0f; evaluate.InExposureScale = 1.0f;
        evaluate.InFrameTimeDeltaInMsec = frame.at("frame_time_ms").get<float>();
        const auto label = "EVALUATE_DLAA_capture_" + std::to_string(capture);
        CheckNgx(report, label.c_str(), NGX_VULKAN_EVALUATE_DLSS_EXT(gpu.command, gpu.feature, gpu.parameters, &evaluate));
        gpu.Transition(gpu.images[3], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        const auto region = Synthetic::CopyRegion(gpu.images[3]);
        gpu.CmdCopyImageToBuffer(gpu.command, gpu.images[3].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                gpu.buffers[1].buffer, 1, &region);
        VkMemoryBarrier host_read {};
        host_read.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        host_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host_read.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        gpu.CmdPipelineBarrier(gpu.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                              0, 1, &host_read, 0, nullptr, 0, nullptr);
        CheckVk(gpu.EndCommandBuffer(gpu.command), "End replay command buffer");
        VkSubmitInfo submit {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &gpu.command;
        CheckVk(gpu.QueueSubmit(gpu.queue, 1, &submit, gpu.fence), "Submit replay frame");
        CheckVk(gpu.WaitForFences(context.device, 1, &gpu.fence, VK_TRUE, 30'000'000'000ull), "Wait for replay frame");
        std::vector<uint16_t> half(pixels * 4);
        CheckVk(gpu.MapMemory(context.device, gpu.buffers[1].memory, 0, VK_WHOLE_SIZE, 0, &mapped), "Map replay output");
        std::memcpy(half.data(), mapped, pixels * 8);
        gpu.UnmapMemory(context.device, gpu.buffers[1].memory);
        size_t finite {}, nonzero {};
        float minimum = std::numeric_limits<float>::infinity(), maximum = -minimum;
        double sum {};
        for (size_t pixel = 0; pixel < pixels; ++pixel) for (size_t channel = 0; channel < 3; ++channel) {
            const auto value = ReadHalf(half[pixel * 4 + channel]);
            if (!std::isfinite(value)) continue;
            ++finite; if (value != 0.0f) ++nonzero;
            minimum = std::min(minimum, value); maximum = std::max(maximum, value); sum += value;
        }
        const auto raw_path = output_directory / ("capture-" + std::to_string(capture) + ".rgba16f");
        report["replay"]["frames"].push_back({{"capture", capture}, {"reset", first},
            {"jitter", frame.at("jitter")}, {"invalid_motion_pixels", masked}, {"rgb_components", pixels * 3},
            {"finite_rgb_components", finite}, {"nonzero_rgb_components", nonzero},
            {"minimum", finite ? Json(minimum) : Json(nullptr)}, {"maximum", finite ? Json(maximum) : Json(nullptr)},
            {"mean", finite ? Json(sum / double(finite)) : Json(nullptr)}, {"raw_output", raw_path.u8string()}});
        if (finite != pixels * 3 || !nonzero || maximum - minimum < 0.001f)
            throw std::runtime_error("Replay output was non-finite, empty, or constant");
        std::ofstream raw(raw_path, std::ios::binary);
        raw.exceptions(std::ios::badbit | std::ios::failbit);
        raw.write(reinterpret_cast<const char*>(half.data()), static_cast<std::streamsize>(pixels * 8));
        first = false;
    }
    report["feature_evaluation_tested"] = true;
    report["replay_evaluation_passed"] = true;
}
