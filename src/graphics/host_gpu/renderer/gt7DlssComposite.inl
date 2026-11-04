// Included by gt7Dlss.cpp after Impl; private host resources stay in one translation unit.
bool Gt7Dlss::Composite(CommandBuffer& buffer, const TextureBinding& low_final,
                        const RenderColorInfo& high_target, uint32_t effect_count) {
    auto& s = *m_impl;
    if (!Active()) return false;
    const auto reject = [&](const char* reason) { CancelFrame(reason); return false; };
    if (effect_count >= s.composites_by_effect_count.size())
        return reject("unsupported scene effect count");
    const auto rgb_identity = [](const vk::ComponentMapping& mapping) {
        return (mapping.r == vk::ComponentSwizzle::eIdentity || mapping.r == vk::ComponentSwizzle::eR) &&
               (mapping.g == vk::ComponentSwizzle::eIdentity || mapping.g == vk::ComponentSwizzle::eG) &&
               (mapping.b == vk::ComponentSwizzle::eIdentity || mapping.b == vk::ComponentSwizzle::eB);
    };
    constexpr auto scene_format = vk::Format::eB10G11R11UfloatPack32;
    if (!low_final.image_id || low_final.image_id != s.scene_id || !high_target.image_id ||
        high_target.image_id == s.scene_id || !BaseView(low_final.desc) || !BaseView(high_target.desc) ||
        low_final.desc.type != TextureCache::BindingType::Texture ||
        low_final.desc.view_info.format != scene_format || high_target.desc.view_info.format != scene_format ||
        !rgb_identity(low_final.desc.view_info.mapping) || !high_target.export_mapping.IsIdentity() ||
        high_target.target_slot != 0 || high_target.guest_mip_level != 0 || high_target.guest_array_layer != 0 ||
        Overlap(low_final.desc.info.data, high_target.desc.info.data)) {
        return reject("unverified scale source or destination view");
    }
    auto& cache = s.context.GetTextureCache();
    auto& native_final = cache.GetImage(low_final.image_id);
    auto& target = cache.GetImage(high_target.image_id);
    if (!Shape(native_final, InputWidth, InputHeight, scene_format) ||
        !Shape(target, GuestWidth, GuestHeight, scene_format) ||
        !(target.backing.usage & vk::ImageUsageFlagBits::eTransferDst) ||
        !(s.composed->backing.usage & vk::ImageUsageFlagBits::eTransferSrc) ||
        !(s.graphics.GetFormatProperties(s.output->backing.format).optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear) ||
        !(s.graphics.GetFormatProperties(scene_format).optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst) ||
        !(s.graphics.GetFormatProperties(s.composed->backing.format).optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitSrc)) {
        return reject("unsupported scale backing or format conversion");
    }
    const auto native_state = native_final.backing.state;
    const auto target_state = target.backing.state;
    if (native_state.layout == vk::ImageLayout::eUndefined || target_state.layout == vk::ImageLayout::eUndefined)
        return reject("uninitialized scale image state");

    s.scheduler.EndRendering();
    const auto command = buffer.Handle(); // Drain recording and invalidate all cached Vulkan state.
    std::array<Image*, 4> sampled {s.output.get(), s.effect.get(), &native_final, s.invalid.get()};
    for (auto* image : sampled) Transit(*image, SampledLayout, ShaderRead, command);
    Transit(*s.composed, vk::ImageLayout::eGeneral, ShaderWrite, command);
    const std::array<uint32_t, 4> push {GuestWidth, GuestHeight, InputWidth, InputHeight};
    const std::array<Image*, 1> storage {s.composed.get()};
    Dispatch(command, s.composite, sampled, storage, s.linear, push.data(), GuestWidth, GuestHeight);
    Transit(*s.composed, vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead, command);
    Transit(target, vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits2::eTransferWrite, command);
    vk::ImageBlit region {};
    region.srcSubresource = vk::ImageSubresourceLayers {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    region.dstSubresource = region.srcSubresource;
    region.srcOffsets[1] = vk::Offset3D {int32_t(GuestWidth), int32_t(GuestHeight), 1};
    region.dstOffsets[1] = region.srcOffsets[1];
    command.blitImage(s.composed->backing.image, vk::ImageLayout::eTransferSrcOptimal,
                      target.backing.image, vk::ImageLayout::eTransferDstOptimal, 1, &region, vk::Filter::eNearest);
    // Original scale bindings are already prepared; restore their layouts before any guest use.
    Transit(native_final, native_state.layout, native_state.access_mask, command);
    Transit(target, target_state.layout, target_state.access_mask, command);
    cache.MarkGpuWritten(high_target.image_id);
    s.active = false;
    ++s.composites;
    const auto count_for_effects = ++s.composites_by_effect_count[effect_count];
    if (Report(s.composites) || count_for_effects == 1)
        LOGF("GT7 DLSS: composited #%llu (evaluated=%llu), reconstruction=2560x1440 guest_target=3840x2160, effects=%u totals_by_effects=[%llu,%llu,%llu]\n",
            static_cast<unsigned long long>(s.composites), static_cast<unsigned long long>(s.evaluations), effect_count,
            static_cast<unsigned long long>(s.composites_by_effect_count[0]),
            static_cast<unsigned long long>(s.composites_by_effect_count[1]),
            static_cast<unsigned long long>(s.composites_by_effect_count[2]));
    return true;
}
