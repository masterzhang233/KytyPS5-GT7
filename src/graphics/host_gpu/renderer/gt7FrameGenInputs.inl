// Included by gt7Dlss.cpp so source input images remain private to the SR helper.
void Gt7Dlss::DescribeFrameGenerationDraw(const ShaderStageRuntime& vertex, uint64_t pixel_shader,
                                        const RenderDepthInfo& depth,
                                        std::span<const RenderColorInfo> colors,
                                        bool full_viewport, bool storage_writes) {
    auto& s = *m_impl;
    if (!s.graphics.gt7_fsr_present_queue || !Gt7FrameGen::Requested() ||
        !vertex || !full_viewport || !depth.image_id) return;
    const RenderColorInfo* motion = nullptr;
    for (const auto& color : colors) if (color.target_slot == 1) motion = &color;
    if (!motion || !motion->image_id) return;
    auto& textures = s.context.GetTextureCache();
    const auto& source_depth = textures.GetImage(depth.image_id);
    const auto& source_motion = textures.GetImage(motion->image_id);
    if (!Shape(source_depth, InputWidth, InputHeight, vk::Format::eD32SfloatS8Uint) ||
        !Shape(source_motion, InputWidth, InputHeight, vk::Format::eR16G16Sfloat)) return;
    ++s.fg_candidate_draws;
    const std::array<uint64_t, 2> signature {vertex.program->shader_hash, pixel_shader};
    if (s.fg_draw_signatures.size() >= 16 ||
        std::find(s.fg_draw_signatures.begin(), s.fg_draw_signatures.end(), signature) != s.fg_draw_signatures.end()) return;
    s.fg_draw_signatures.push_back(signature);
    LOGF("GT7 FSR FG: producer signature #%zu vs=%016llx ps=%016llx depth=%u:%u motion=%u:%u depth_test=%u depth_write=%u depth_compare=%u full_viewport=%u storage_writes=%u base_views=%u/%u\n",
         s.fg_draw_signatures.size(), static_cast<unsigned long long>(signature[0]),
         static_cast<unsigned long long>(signature[1]), depth.image_id.index, depth.image_id.generation,
         motion->image_id.index, motion->image_id.generation, depth.depth_test_enable ? 1u : 0u,
         depth.depth_write_enable ? 1u : 0u, static_cast<uint32_t>(depth.depth_compare_op),
         full_viewport ? 1u : 0u, storage_writes ? 1u : 0u, BaseView(depth.desc) ? 1u : 0u,
         BaseView(motion->desc) ? 1u : 0u);
}

void Gt7Dlss::ObserveFrameGenerationCamera(const ShaderStageRuntime& vertex,
                                         const RenderDepthInfo& depth,
                                         std::span<const RenderColorInfo> colors) {
    auto& s = *m_impl;
    if (!s.graphics.gt7_fsr_present_queue || !Gt7FrameGen::Requested() || !vertex ||
        vertex.program->stage != ShaderType::Vertex ||
        !IsFrameGenerationCameraShader(vertex.program->shader_hash)) return;
    auto& observations = s.fg_camera_observations;
    ++observations[CameraCalls];
    if (!depth.image_id || !depth.depth_test_enable || !depth.depth_write_enable ||
        depth.depth_compare_op != vk::CompareOp::eGreaterOrEqual ||
        depth.guest_array_layer != 0 || !BaseView(depth.desc)) { ++observations[CameraDepth]; return; }
    const RenderColorInfo* motion = nullptr;
    for (const auto& color : colors) if (color.target_slot == 1) motion = &color;
    if (!motion || !motion->image_id || !BaseView(motion->desc) ||
        motion->guest_mip_level || motion->guest_array_layer) { ++observations[CameraMotion]; return; }
    auto& textures = s.context.GetTextureCache();
    if (!Shape(textures.GetImage(depth.image_id), InputWidth, InputHeight, vk::Format::eD32SfloatS8Uint) ||
        !Shape(textures.GetImage(motion->image_id), InputWidth, InputHeight, vk::Format::eR16G16Sfloat)) {
        ++observations[CameraShape]; return;
    }
    // A new scene writer after the temporal evaluation breaks the input/final
    // color association even if no second TAA has run yet.
    if (s.fg_temporal_attempts != 0) {
        s.fg_camera_valid = false;
        s.fg_input_ambiguous = true;
        ++observations[CameraLate];
        return;
    }
    auto entry = std::find_if(s.fg_cameras.begin(), s.fg_cameras.end(), [&](const auto& camera) {
        return camera.depth_id == depth.image_id && camera.motion_id == motion->image_id;
    });
    if (entry == s.fg_cameras.end()) {
        if (s.fg_cameras.size() >= 8) { ++observations[CameraLimit]; return; }
        s.fg_cameras.emplace_back();
        entry = std::prev(s.fg_cameras.end());
        entry->depth_id = depth.image_id;
        entry->motion_id = motion->image_id;
    }
    if (entry->rejected) { ++observations[CameraAlreadyRejected]; return; }
    const auto reject = [&](CameraObservation observation, const char* reason) {
        ++observations[observation]; entry->valid = false; entry->rejected = true; entry->reject_reason = reason;
    };
    const auto& user = vertex.resources->user_data;
    const auto base = vertex.program->user_data_base;
    if (base > 10 || user.size() < size_t(12 - base) || !vertex.resources->gpu_fills.empty()) {
        reject(CameraUser, "user-data registers or GPU fills"); return;
    }
    const size_t reg = 10 - base;
    const uint64_t address = uint64_t(user[reg]) | (uint64_t(user[reg + 1]) << 32u);
    constexpr uint64_t LastByte = 1040;
    if (!address || (address & 3u) || address > std::numeric_limits<uint64_t>::max() - LastByte) {
        reject(CameraAddress, "camera pointer alignment/range"); return;
    }
    const auto read = [&](uint64_t offset, void* destination, uint64_t bytes) {
        const auto source = address + offset;
        if (Libs::LibKernel::Memory::TryClampRangeSize(source, bytes) != bytes) {
            reject(CameraUnmapped, "camera matrix range not mapped"); return false;
        }
        if (s.context.GetBufferCache().HasGpuDirtyBytes(source, bytes)) {
            reject(CameraGpuDirty, "camera matrix range GPU dirty"); return false;
        }
        if (!Libs::LibKernel::Memory::TryReadBacking(source, destination, bytes)) {
            reject(CameraRead, "camera matrix backing read failed"); return false;
        }
        return true;
    };
    std::array<float, 48> current {};
    Gt7FrameGenCamera::Matrix unjittered {};
    if (!read(80, current.data(), sizeof(current)) || !read(976, unjittered.data(), sizeof(unjittered))) {
        return;
    }
    Gt7FrameGenCamera::Matrix view_projection {}, view {}, projection {};
    std::copy_n(current.begin(), 16, view_projection.begin());
    std::copy_n(current.begin() + 16, 16, view.begin());
    std::copy_n(current.begin() + 32, 16, projection.begin());
    Gt7FrameGenCamera::Parameters parameters {};
    const char* decode_failure = nullptr;
    if (!Gt7FrameGenCamera::Decode(view_projection, view, projection, unjittered,
                                   InputWidth, InputHeight, parameters, &decode_failure)) {
        reject(CameraMatrix, decode_failure); return;
    }
    if (entry->valid && (entry->view_projection != view_projection || entry->view != view ||
                        entry->projection != projection || entry->unjittered != unjittered)) {
        reject(CameraChanged, "camera changed for the same depth/motion targets"); return;
    }
    entry->view_projection = view_projection;
    entry->view = view;
    entry->projection = projection;
    entry->unjittered = unjittered;
    entry->parameters = parameters;
    entry->valid = true;
    ++observations[CameraAccepted];
}

void Gt7Dlss::NoteFrameGenerationSceneTarget(const RenderColorInfo& target) {
    auto& s = *m_impl;
    if (s.graphics.gt7_fsr_present_queue && Gt7FrameGen::Requested())
        s.fg_scaled_scene_id = target.image_id;
}

bool Gt7Dlss::FrameGenerationTonemapMatches(const ShaderVertexInputInfo& vertex,
                                           const PreparedBindings& pixel,
                                           const RenderColorInfo& target) {
    auto& s = *m_impl;
    if (!s.graphics.gt7_fsr_present_queue || !Gt7FrameGen::Requested() ||
        !s.fg_scaled_scene_id || s.composites != s.boundary_composites + 1 ||
        !pixel.runtime || !*pixel.runtime || !vertex.stage ||
        pixel.runtime->program->shader_hash != 0xedb9cdad0985bf44ULL ||
        vertex.stage.program->shader_hash != 0xf13bd2584bf580c0ULL) return false;
    // Exactly one final tone-map per scene. Never replace a prior snapshot after
    // UI has begun, even if a second superficially similar pass is encountered.
    ++s.fg_tonemap_attempts;
    s.fg_hudless_valid = false;
    const auto reject = [&](const char* reason) {
        if (++s.fg_tonemap_rejections <= 8 || s.fg_tonemap_rejections % 300 == 0)
            LOGF("GT7 FSR FG: HUD-less tone-map rejected #%llu frame=%llu: %s\n",
                 static_cast<unsigned long long>(s.fg_tonemap_rejections),
                 static_cast<unsigned long long>(s.fg_frame), reason);
        return false;
    };
    if (s.fg_tonemap_attempts != 1 || pixel.images.size() != 5 ||
        pixel.buffer_sources.size() != 1 || !pixel.bindless_heaps.empty() ||
        !pixel.bindless_patches.empty() || !pixel.runtime->resources->gpu_fills.empty() ||
        pixel.runtime->program->info.uses_dma || !target.image_id || target.target_slot != 0 ||
        target.guest_mip_level || target.guest_array_layer || !BaseView(target.desc) ||
        target.desc.view_info.format != vk::Format::eA2B10G10R10UnormPack32 ||
        !target.export_mapping.IsIdentity()) return reject("tone-map count, resource cardinality or target view");
    for (const auto& resource : pixel.runtime->program->info.images)
        if (resource.written || resource.bindless || resource.runtime) return reject("unbounded tone-map image resource");
    for (const auto& resource : pixel.runtime->program->info.buffers)
        if (resource.written) return reject("tone-map writes buffer storage");
    const auto& scene = pixel.images[0];
    if (scene.image_id != s.fg_scaled_scene_id || !BaseView(scene.desc) ||
        scene.desc.view_info.format != vk::Format::eB10G11R11UfloatPack32)
        return reject("tone-map source is not the current SR composite");
    const std::array<vk::Extent3D, 5> extents {
        vk::Extent3D {GuestWidth, GuestHeight, 1}, {960, 540, 1}, {1, 1, 1},
        {64, 64, 64}, {GuestWidth, GuestHeight, 1}};
    const std::array<vk::Format, 5> formats {
        vk::Format::eB10G11R11UfloatPack32, vk::Format::eB10G11R11UfloatPack32,
        vk::Format::eR8Unorm, vk::Format::eR16G16B16A16Sfloat, vk::Format::eR16G16B16A16Sfloat};
    auto& textures = s.context.GetTextureCache();
    const auto& output = textures.GetImage(target.image_id);
    if (!Shape(output, GuestWidth, GuestHeight, vk::Format::eA2B10G10R10UnormPack32) ||
        !Shape(textures.GetImage(scene.image_id), GuestWidth, GuestHeight,
               vk::Format::eB10G11R11UfloatPack32)) return reject("tone-map backing shape or format");
    for (size_t i = 0; i < pixel.images.size(); ++i) {
        const auto& texture = pixel.images[i];
        const auto& view = texture.desc.view_info;
        if (!texture.image_id || !texture.image_view ||
            texture.desc.type != TextureCache::BindingType::Texture ||
            texture.desc.info.extent != extents[i] || texture.desc.info.samples != 1 ||
            view.format != formats[i] || view.base_level || view.level_count != 1 ||
            view.base_layer || view.layer_count != 1 ||
            view.type != (i == 3 ? vk::ImageViewType::e3D : vk::ImageViewType::e2D) ||
            texture.image_id == target.image_id ||
            textures.GetImage(texture.image_id).backing.image == output.backing.image ||
            Overlap(texture.desc.info.data, target.desc.info.data)) return reject("tone-map texture shape, view or feedback");
    }
    // The captured f13b fullscreen fan has a 28-byte XYZ/UV vertex and an
    // identity vertex matrix. Validate it so a crop/zoom cannot masquerade as
    // the complete display-color image merely by sharing shader hashes.
    if (vertex.logical_stage != ShaderType::Vertex || vertex.prepare_failed ||
        !vertex.fetch_embedded || vertex.fetch_external || vertex.clip_space.enabled ||
        vertex.buffers_num != 1 || vertex.resources_num != 2 ||
        vertex.buffers[0].stride != 28 || vertex.buffers[0].num_records != 4 ||
        vertex.stage.resources->buffers.size() != 1 ||
        !vertex.stage.resources->gpu_fills.empty()) return reject("tone-map fullscreen vertex bindings");
    const auto address = vertex.buffers[0].addr;
    for (size_t i = 0; i < 2; ++i) {
        const auto& resource = vertex.resources[i];
        if (resource.Base48() != address + i * 12 || resource.Stride() != 28 ||
            resource.NumRecords() != 4 || resource.RawFormat() != (i == 0 ? 74u : 77u) ||
            resource.AddTid() || resource.SwizzleEnabled()) return reject("tone-map vertex attribute descriptor");
    }
    const auto matrix = DecodeNativeDescriptor<ShaderBufferResource>(vertex.stage.resources->buffers[0]);
    if (matrix.Stride() != 16 || matrix.NumRecords() != 4 || matrix.RawFormat() != 77 ||
        matrix.DstSelXYZW() != 0xfac || matrix.AddTid() || matrix.SwizzleEnabled())
        return reject("tone-map vertex matrix descriptor");
    constexpr std::array<float, 28> quad {
        -1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0,
        1, -1, 0, 1, 1, 0, 0, -1, -1, 0, 0, 1, 0, 0};
    constexpr std::array<float, 16> identity {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const auto equal_bytes = [&]<size_t N>(uint64_t base, const std::array<float, N>& expected) {
        if (!base || base > std::numeric_limits<uint64_t>::max() - sizeof(expected) ||
            Libs::LibKernel::Memory::TryClampRangeSize(base, sizeof(expected)) != sizeof(expected) ||
            s.context.GetBufferCache().HasGpuDirtyBytes(base, sizeof(expected))) return false;
        std::array<float, N> actual {};
        return Libs::LibKernel::Memory::TryReadBacking(base, actual.data(), sizeof(actual)) && actual == expected;
    };
    if (!equal_bytes(address, quad) || !equal_bytes(matrix.Base48(), identity))
        return reject("tone-map fullscreen quad or identity matrix changed");
    return true;
}

void Gt7Dlss::CaptureFrameGenerationHudless(CommandBuffer& buffer, const RenderColorInfo& target) {
    auto& s = *m_impl;
    s.fg_hudless_valid = false;
    if (s.fg_tonemap_attempts != 1 || !target.image_id) return;
    auto& source = s.context.GetTextureCache().GetImage(target.image_id);
    if (!(source.backing.usage & vk::ImageUsageFlagBits::eTransferSrc) ||
        source.backing.state.layout == vk::ImageLayout::eUndefined ||
        !ConfigureFrameGenerationImage(s.graphics, s.fg_hudless,
            vk::Format::eA2B10G10R10UnormPack32, GuestWidth, GuestHeight)) return;
    s.scheduler.EndRendering();
    const auto command = buffer.Handle();
    const auto saved = source.backing.state;
    Transit(source, vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead, command);
    TransitFrameGenerationImage(command, s.fg_hudless, vk::ImageLayout::eTransferDstOptimal,
                                 vk::AccessFlagBits2::eTransferWrite);
    const vk::ImageSubresourceLayers layer {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    const vk::ImageCopy region {layer, {}, layer, {}, {GuestWidth, GuestHeight, 1}};
    command.copyImage(source.backing.image, vk::ImageLayout::eTransferSrcOptimal,
                      s.fg_hudless.image, vk::ImageLayout::eTransferDstOptimal, 1, &region);
    Transit(source, saved.layout, saved.access_mask, command);
    TransitFrameGenerationImage(command, s.fg_hudless, SampledLayout, ShaderRead);
    s.fg_output_id = target.image_id;
    s.fg_output_image = source.backing.image;
    s.fg_output_range = source.info.data;
    s.fg_hudless_valid = true;
    if (Report(++s.fg_hudless_captures))
        LOGF("GT7 FSR FG: HUD-less capture #%llu frame=%llu target=%u:%u 3840x2160 format=%u after guest tone-map before UI\n",
             static_cast<unsigned long long>(s.fg_hudless_captures), static_cast<unsigned long long>(s.fg_frame),
             s.fg_output_id.index, s.fg_output_id.generation, unsigned(source.backing.format));
}

bool Gt7Dlss::WatchesFrameGenerationOutput() const { return m_impl->fg_hudless_valid; }

bool Gt7Dlss::PreservesFrameGenerationMetadata(const ShaderComputeInputInfo& input,
    const PreparedBindings& bindings, uint32_t group_x, uint32_t group_y,
    uint32_t group_z, uint32_t mode) {
    auto& s = *m_impl;
    if (s.active || !s.fg_hudless_valid) return false;
    const auto& program = *input.stage.program;
    const auto& resources = *input.stage.resources;
    if (program.shader_hash != Gt7FrameGenDcc::CopyShader) return false;
    const auto reject = [&](size_t reason, const char* text) {
        static std::array<uint64_t, 8> counts {};
        if (++counts[reason] <= 4)
            LOGF("GT7 FSR FG: DCC copy proof rejected frame=%llu: %s\n",
                 static_cast<unsigned long long>(s.fg_frame), text);
        return false;
    };
    if (program.has_address_writes ||
        input.workgroup_register != 9 || !input.group_id[0] || !input.group_id[1] ||
        input.thread_ids_num < 2 || input.dispatch_dimensions_indirect ||
        program.info.uses_dma || !program.info.images.empty() || program.info.buffers.size() != 2 ||
        resources.buffers.size() != 2 || !resources.images.empty() || !resources.gpu_fills.empty() ||
        program.user_data_base != 0 || resources.user_data.size() != 9 ||
        bindings.buffer_sources.size() != 2 || !bindings.images.empty() ||
        program.bindings.descriptors.empty() ||
        program.bindings.descriptors.front().resources.size() != 2 ||
        program.bindings.descriptors.front().resources[0] != 0 ||
        program.bindings.descriptors.front().resources[1] != 1 ||
        !program.info.buffers[0].read || program.info.buffers[0].written ||
        program.info.buffers[1].read || !program.info.buffers[1].written ||
        !program.info.buffers[0].formatted || !program.info.buffers[1].formatted ||
        program.info.buffers[0].atomic || program.info.buffers[1].atomic ||
        resources.buffers[0].dword_count != 4 || resources.buffers[1].dword_count != 4)
        return reject(0, "shader runtime/binding shape");
    Gt7FrameGenDcc::CopyContract copy;
    copy.shader = program.shader_hash;
    std::copy_n(resources.buffers[0].dwords.begin(), 4, copy.source.begin());
    std::copy_n(resources.buffers[1].dwords.begin(), 4, copy.destination.begin());
    if (!std::equal(copy.source.begin(), copy.source.end(), resources.user_data.begin() + 1) ||
        !std::equal(copy.destination.begin(), copy.destination.end(), resources.user_data.begin() + 5))
        return reject(1, "descriptor/user-data mismatch");
    copy.groups = {group_x, group_y, group_z};
    std::copy_n(input.threads_num, 3, copy.threads.begin());
    copy.mode = mode;
    copy.pitch = resources.user_data[0];
    copy.thread_dimensions = input.dispatch_thread_dimensions;
    if (!Gt7FrameGenDcc::Matches(copy)) return reject(2, "unsupported copy contract");
    const auto& source = bindings.buffer_sources[0];
    const auto& destination = bindings.buffer_sources[1];
    if (source.address != Gt7FrameGenDcc::Address(copy.source) ||
        destination.address != Gt7FrameGenDcc::Address(copy.destination) ||
        source.size != Gt7FrameGenDcc::Bytes || destination.size != Gt7FrameGenDcc::Bytes ||
        !source.id || !destination.id) return reject(3, "resolved buffer range mismatch");
    auto& textures = s.context.GetTextureCache();
    if (!textures.WriteIsOnlyDccMetadata({destination.address, destination.size},
                                        s.fg_output_id, s.fg_output_range) ||
        !textures.WriteIsOnlyDccMetadata({source.address, source.size},
                                        s.fg_output_id, s.fg_output_range))
        // RebindBuffers may synchronize a formatted source from an aliased
        // native image. Both sides must be isolated metadata, never pixels.
        return reject(4, "source/destination are not isolated exact display DCC ranges");
    const auto& output = textures.GetImage(s.fg_output_id);
    if (output.backing.image != s.fg_output_image || output.IsBufferModified() || output.IsCpuDirty() ||
        !Shape(output, GuestWidth, GuestHeight, vk::Format::eA2B10G10R10UnormPack32))
        return reject(5, "captured display image no longer current");
    auto& buffers = s.context.GetBufferCache();
    const auto clean = [&] {
        return !buffers.AddressWritesInFlight() &&
            !buffers.HasGpuDirtyBytes(source.address, source.size) &&
            buffers.IsCleanForConcurrentRead(source.address, source.size) &&
            !buffers.IsRegionCpuModified(source.address, source.size);
    };
    if (!clean() || Libs::LibKernel::Memory::TryClampRangeSize(source.address, source.size) != source.size)
        return reject(6, "source is not fully mapped and CPU/GPU clean");
    // Validate the complete current source, never a hash/sample or a GPU readback.
    // As with camera constants, guest submission synchronization owns its lifetime.
    s.fg_dcc_snapshot.resize(Gt7FrameGenDcc::Bytes);
    auto& bytes = s.fg_dcc_snapshot;
    if (!Libs::LibKernel::Memory::TryReadBacking(source.address, bytes.data(), bytes.size()) ||
        !Gt7FrameGenDcc::FullyExpanded(bytes) || !clean())
        return reject(7, "source is not proven entirely expanded");
    static uint64_t preserved = 0;
    if (Report(++preserved))
        LOGF("GT7 FSR FG: expanded DCC copy preserved #%llu frame=%llu source=0x%012llx destination=0x%012llx bytes=0x%x\n",
            static_cast<unsigned long long>(preserved), static_cast<unsigned long long>(s.fg_frame),
            static_cast<unsigned long long>(source.address), static_cast<unsigned long long>(destination.address),
            Gt7FrameGenDcc::Bytes);
    return true;
}

bool Gt7Dlss::MatchesFrameGenerationOutput(const RenderColorInfo& target) const {
    const auto& s = *m_impl;
    return s.fg_hudless_valid && ((target.image_id && target.image_id == s.fg_output_id) ||
                                 Overlap(target.desc.info.data, s.fg_output_range));
}

void Gt7Dlss::CheckFrameGenerationOutputDraw(std::span<const RenderColorInfo> targets,
                                            const PreparedBindings* pixel, uint64_t vs,
                                            uint64_t ps, bool ordinary_alpha_overlay) {
    auto& s = *m_impl;
    if (!s.fg_hudless_valid) return;
    bool writes_output = false;
    for (const auto& target : targets) writes_output |= MatchesFrameGenerationOutput(target);
    if (!writes_output) return;
    // All later writers in four captured races belong to these UI programs.
    constexpr std::array<std::array<uint64_t, 2>, 7> ui {
        {{0x352273e237eda017ULL, 0xbbc8d9b8b2bc2c32ULL},
         {0x76d0d62ac9602de2ULL, 0xc711b01042dcbfcfULL},
         {0xa2d70c2dc6a5de23ULL, 0x259b71bf59605f74ULL},
         {0x9a82023cd470256fULL, 0x326fe59634e8b213ULL},
         {0x76d0d62ac9602de2ULL, 0xb301208be2d07966ULL},
         {0x25b3b1f6485541d3ULL, 0x6b306f0c24b2242aULL},
         {0x19d44ebf7ab53326ULL, 0x76298aa99b1d5958ULL}}};
    bool allowed = ordinary_alpha_overlay && targets.size() == 1 && pixel &&
        std::find(ui.begin(), ui.end(), std::array<uint64_t, 2>{vs, ps}) != ui.end() &&
        targets[0].image_id == s.fg_output_id && BaseView(targets[0].desc) &&
        targets[0].target_slot == 0 && targets[0].export_mapping.IsIdentity() &&
        targets[0].desc.view_info.format == vk::Format::eA2B10G10R10UnormPack32;
    if (pixel) {
        if (!pixel->bindless_heaps.empty() || !pixel->bindless_patches.empty()) allowed = false;
        for (const auto& texture : pixel->images)
            if (!texture.image_id || texture.image_id == s.fg_output_id ||
                Overlap(texture.desc.info.data, s.fg_output_range)) allowed = false;
        for (const auto& source : pixel->buffer_sources)
            if (source.size && Overlap({source.address, source.size}, s.fg_output_range)) allowed = false;
    }
    if (allowed) { ++s.fg_ui_draws; return; }
    s.fg_hudless_valid = false;
    if (++s.fg_hudless_rejections <= 8 || s.fg_hudless_rejections % 300 == 0)
        LOGF("GT7 FSR FG: HUD-less rejected #%llu frame=%llu: unverified display writer vs=%016llx ps=%016llx overlay=%u targets=%zu after_ui=%u\n",
             static_cast<unsigned long long>(s.fg_hudless_rejections), static_cast<unsigned long long>(s.fg_frame),
             static_cast<unsigned long long>(vs), static_cast<unsigned long long>(ps),
             ordinary_alpha_overlay ? 1u : 0u, targets.size(), s.fg_ui_draws);
}

bool Gt7Dlss::CopyFrameGenerationInputs(CommandBuffer& buffer, Gt7FrameGen::FrameInputs& inputs,
                                       const Image& presented_source) {
    auto& s = *m_impl;
    inputs.valid = inputs.camera_valid = inputs.camera_pose_valid = inputs.hudless_valid = false;
    inputs.reset = true;
    if (!s.graphics.gt7_fsr_present_queue || !Gt7FrameGen::Requested()) return false;
    const auto reject = [&](const char* reason) {
        if (Report(++s.fg_rejected))
            LOGF("GT7 FSR FG: input snapshot skipped #%llu frame=%llu: %s\n",
                 static_cast<unsigned long long>(s.fg_rejected),
                 static_cast<unsigned long long>(s.fg_frame), reason);
        return false;
    };
    if (!s.initialized || s.active || s.fg_input_ambiguous || s.fg_temporal_attempts != 1 ||
        s.evaluations != s.boundary_evaluations + 1 || s.composites != s.boundary_composites + 1)
        return reject("no single verified DLSS scene composite in this guest frame");
    if (!s.fg_camera_valid) return reject("camera/depth/motion/projection-jitter association not verified");
    if (!s.fg_hudless_valid || s.fg_tonemap_attempts != 1 ||
        presented_source.self_id != s.fg_output_id || presented_source.backing.image != s.fg_output_image ||
        presented_source.info.data.address != s.fg_output_range.address ||
        presented_source.info.data.size != s.fg_output_range.size ||
        !Shape(presented_source, GuestWidth, GuestHeight, vk::Format::eA2B10G10R10UnormPack32))
        return reject("no verified HUD-less tone-map output for the presented image");
    if (s.fg_copied == s.fg_frame) return reject("guest frame already snapshotted");
    if (s.depth->backing.state.layout == vk::ImageLayout::eUndefined ||
        s.motion->backing.state.layout == vk::ImageLayout::eUndefined ||
        !(s.depth->backing.usage & vk::ImageUsageFlagBits::eTransferSrc) ||
        !(s.motion->backing.usage & vk::ImageUsageFlagBits::eTransferSrc))
        return reject("private temporal inputs cannot be copied");
    if (!ConfigureFrameGenerationImage(s.graphics, inputs.depth, vk::Format::eR32Sfloat) ||
        !ConfigureFrameGenerationImage(s.graphics, inputs.motion, vk::Format::eR32G32Sfloat) ||
        !ConfigureFrameGenerationImage(s.graphics, inputs.hudless_color,
            vk::Format::eA2B10G10R10UnormPack32, GuestWidth, GuestHeight))
        return reject("frame-owned temporal image allocation unavailable");

    buffer.EndRendering();
    const auto command = buffer.Handle();
    const std::array<Image*, 2> sources {s.depth.get(), s.motion.get()};
    const std::array<VulkanImage*, 2> destinations {&inputs.depth, &inputs.motion};
    const vk::ImageSubresourceLayers layer {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    const vk::ImageCopy region {layer, {}, layer, {}, {InputWidth, InputHeight, 1}};
    for (size_t i = 0; i < sources.size(); ++i) {
        auto& source = *sources[i];
        auto& destination = *destinations[i];
        const auto saved = source.backing.state;
        Transit(source, vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead, command);
        TransitFrameGenerationImage(command, destination, vk::ImageLayout::eTransferDstOptimal,
                                     vk::AccessFlagBits2::eTransferWrite);
        command.copyImage(source.backing.image, vk::ImageLayout::eTransferSrcOptimal,
                          destination.image, vk::ImageLayout::eTransferDstOptimal, 1, &region);
        TransitFrameGenerationImage(command, destination, SampledLayout, ShaderRead);
        Transit(source, saved.layout, saved.access_mask, command);
    }
    TransitFrameGenerationImage(command, s.fg_hudless, vk::ImageLayout::eTransferSrcOptimal,
                                 vk::AccessFlagBits2::eTransferRead);
    TransitFrameGenerationImage(command, inputs.hudless_color, vk::ImageLayout::eTransferDstOptimal,
                                 vk::AccessFlagBits2::eTransferWrite);
    const vk::ImageCopy hudless_region {layer, {}, layer, {}, {GuestWidth, GuestHeight, 1}};
    command.copyImage(s.fg_hudless.image, vk::ImageLayout::eTransferSrcOptimal,
                      inputs.hudless_color.image, vk::ImageLayout::eTransferDstOptimal, 1, &hudless_region);
    TransitFrameGenerationImage(command, s.fg_hudless, SampledLayout, ShaderRead);
    TransitFrameGenerationImage(command, inputs.hudless_color, SampledLayout, ShaderRead);
    // The presenter submits these copies before signaling its producer-ready
    // semaphore. No presentation-thread access to mutable SR resources remains.
    inputs.render_extent = vk::Extent2D {InputWidth, InputHeight};
    inputs.guest_frame = s.fg_frame;
    inputs.jitter_x = s.fg_jitter_x;
    inputs.jitter_y = s.fg_jitter_y;
    inputs.frame_time_ms = s.fg_frame_time_ms;
    inputs.near_plane = s.fg_camera.near_plane;
    inputs.far_plane = s.fg_camera.far_plane;
    inputs.fov_y = s.fg_camera.fov_y;
    std::copy(s.fg_camera.position.begin(), s.fg_camera.position.end(), inputs.camera_position);
    std::copy(s.fg_camera.right.begin(), s.fg_camera.right.end(), inputs.camera_right);
    std::copy(s.fg_camera.up.begin(), s.fg_camera.up.end(), inputs.camera_up);
    std::copy(s.fg_camera.forward.begin(), s.fg_camera.forward.end(), inputs.camera_forward);
    inputs.depth_infinite = false;
    inputs.camera_valid = inputs.camera_pose_valid = inputs.hudless_valid = inputs.valid = true;
    inputs.reset = s.fg_reset;
    s.fg_copied = s.fg_frame;
    if (Report(++s.fg_snapshots))
        LOGF("GT7 FSR FG: captured frame=%llu 1920x1080 reverse-Z finite near=%.9g far=%.9g fovY=%.9g jitter=(%.6f,%.6f) reset=%u\n",
             static_cast<unsigned long long>(s.fg_frame), inputs.near_plane, inputs.far_plane,
             inputs.fov_y, inputs.jitter_x, inputs.jitter_y, inputs.reset ? 1u : 0u);
    return true;
}
