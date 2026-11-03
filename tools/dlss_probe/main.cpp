// Independent native NGX/Vulkan capability probe. It does not enable DLSS in GT7.
#include <windows.h>
#include <vulkan/vulkan.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::ordered_json;
constexpr char ProjectId[] = "9410e655-dcb1-4bcc-aa6e-90584f4a3877";
constexpr char EngineVersion[] = "Kyty-GT7-DLSS-Probe-1";

std::string Hex(unsigned value) {
    char text[16] {};
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

void CheckVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " returned VkResult " + std::to_string(result));
}

void CheckNgx(Json& report, const char* operation, NVSDK_NGX_Result result) {
    const bool failed = (static_cast<unsigned>(result) & 0xfff00000u) ==
                        static_cast<unsigned>(NVSDK_NGX_Result_Fail);
    report["calls"].push_back({{"operation", operation}, {"result", Hex(static_cast<unsigned>(result))},
                              {"success", !failed}});
    if (failed)
        throw std::runtime_error(std::string(operation) + " returned " + Hex(static_cast<unsigned>(result)));
}

template <typename T>
T InstanceProc(PFN_vkGetInstanceProcAddr gipa, VkInstance instance, const char* name) {
    auto fn = reinterpret_cast<T>(gipa(instance, name));
    if (!fn) throw std::runtime_error(std::string("Missing Vulkan entry point: ") + name);
    return fn;
}

struct Context {
    HMODULE loader {};
    PFN_vkGetInstanceProcAddr gipa {};
    PFN_vkDestroyInstance destroy_instance {};
    PFN_vkDestroyDevice destroy_device {};
    VkInstance instance {};
    VkDevice device {};
    NVSDK_NGX_Parameter* capabilities {};
    bool ngx_initialized {};

    ~Context() {
        if (capabilities) NVSDK_NGX_VULKAN_DestroyParameters(capabilities);
        if (ngx_initialized) NVSDK_NGX_VULKAN_Shutdown1(device);
        if (device && destroy_device) destroy_device(device, nullptr);
        if (instance && destroy_instance) destroy_instance(instance, nullptr);
        if (loader) FreeLibrary(loader);
    }
};

std::vector<const char*> ValidateExtensions(VkExtensionProperties* required, uint32_t count,
                                          const std::vector<VkExtensionProperties>& available,
                                          Json& report, const char* key) {
    std::vector<const char*> result;
    report[key] = Json::array();
    for (uint32_t i = 0; i < count; ++i) {
        const char* name = required[i].extensionName;
        const bool found = std::any_of(available.begin(), available.end(), [name](const auto& value) {
            return std::string(value.extensionName) == name;
        });
        report[key].push_back({{"name", name}, {"available", found}});
        if (!found) throw std::runtime_error(std::string("Missing required Vulkan extension: ") + name);
        if (std::none_of(result.begin(), result.end(), [name](const char* other) {
                return std::string(other) == name;
            })) result.push_back(name);
    }
    return result;
}

unsigned Dimension(const wchar_t* value) {
    std::size_t consumed {};
    auto parsed = std::stoul(value, &consumed);
    if (consumed != std::wcslen(value) || parsed < 64 || parsed > 16384)
        throw std::runtime_error("Output dimensions must be integers between 64 and 16384");
    return static_cast<unsigned>(parsed);
}

// The synthetic path owns all of its GPU state independently from the emulator.
struct Synthetic {
    VkDevice device;
    VkPhysicalDeviceMemoryProperties memory_properties {};
    VkCommandPool pool {};
    VkCommandBuffer command {};
    VkFence fence {};
    VkQueue queue {};
    NVSDK_NGX_Handle* feature {};
    NVSDK_NGX_Parameter* parameters {};
    struct Image {
        VkImage image {};
        VkImageView view {};
        VkDeviceMemory memory {};
        VkFormat format {};
        uint32_t width {}, height {};
    };
    struct Buffer { VkBuffer buffer {}; VkDeviceMemory memory {}; VkDeviceSize size {}; };
    std::array<Image, 4> images {};
    std::array<Buffer, 2> buffers {};

#define SYNTHETIC_VULKAN_FUNCTIONS(X) \
    X(CreateCommandPool) X(DestroyCommandPool) X(ResetCommandPool) X(AllocateCommandBuffers) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(QueueSubmit) X(QueueWaitIdle) \
    X(GetDeviceQueue) X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences) \
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) \
    X(CreateImageView) X(DestroyImageView) X(CreateBuffer) X(DestroyBuffer) \
    X(GetBufferMemoryRequirements) X(BindBufferMemory) X(AllocateMemory) X(FreeMemory) \
    X(MapMemory) X(UnmapMemory) X(CmdPipelineBarrier) X(CmdCopyBufferToImage) \
    X(CmdCopyImageToBuffer) X(CmdClearColorImage)
#define DECLARE_VK(name) PFN_vk##name name {};
    SYNTHETIC_VULKAN_FUNCTIONS(DECLARE_VK)
#undef DECLARE_VK

    explicit Synthetic(Context& context, VkPhysicalDevice physical): device(context.device) {
        const auto gdpa = InstanceProc<PFN_vkGetDeviceProcAddr>(context.gipa, context.instance, "vkGetDeviceProcAddr");
#define LOAD_VK(name) name = reinterpret_cast<PFN_vk##name>(gdpa(device, "vk" #name)); \
        if (!name) throw std::runtime_error("Missing vk" #name);
        SYNTHETIC_VULKAN_FUNCTIONS(LOAD_VK)
#undef LOAD_VK
#undef SYNTHETIC_VULKAN_FUNCTIONS
        const auto get_memory = InstanceProc<PFN_vkGetPhysicalDeviceMemoryProperties>(
            context.gipa, context.instance, "vkGetPhysicalDeviceMemoryProperties");
        get_memory(physical, &memory_properties);
    }

    ~Synthetic() {
        // Includes the exceptional path after a submit or timeout. Resources and
        // NGX handles must never be freed while queued GPU commands still use them.
        if (queue) QueueWaitIdle(queue);
        if (feature) NVSDK_NGX_VULKAN_ReleaseFeature(feature);
        if (parameters) NVSDK_NGX_VULKAN_DestroyParameters(parameters);
        for (auto& image: images) {
            if (image.view) DestroyImageView(device, image.view, nullptr);
            if (image.image) DestroyImage(device, image.image, nullptr);
            if (image.memory) FreeMemory(device, image.memory, nullptr);
        }
        for (auto& buffer: buffers) {
            if (buffer.buffer) DestroyBuffer(device, buffer.buffer, nullptr);
            if (buffer.memory) FreeMemory(device, buffer.memory, nullptr);
        }
        if (fence) DestroyFence(device, fence, nullptr);
        if (pool) DestroyCommandPool(device, pool, nullptr);
    }

    uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const {
        for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memory_properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("No compatible synthetic probe memory type");
    }

    void Allocate(VkDeviceMemory& memory, const VkMemoryRequirements& requirements, VkMemoryPropertyFlags flags) {
        VkMemoryAllocateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        info.allocationSize = requirements.size;
        info.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, flags);
        CheckVk(AllocateMemory(device, &info, nullptr, &memory), "Allocate synthetic memory");
    }

    void MakeImage(Image& image, uint32_t width, uint32_t height, VkFormat format, bool output) {
        image.width = width; image.height = height; image.format = format;
        VkImageCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, height, 1};
        info.mipLevels = 1; info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (output) info.usage |= VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVk(CreateImage(device, &info, nullptr, &image.image), "Create synthetic image");
        VkMemoryRequirements requirements {};
        GetImageMemoryRequirements(device, image.image, &requirements);
        Allocate(image.memory, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CheckVk(BindImageMemory(device, image.image, image.memory, 0), "Bind synthetic image memory");
        VkImageViewCreateInfo view {};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        CheckVk(CreateImageView(device, &view, nullptr, &image.view), "Create synthetic image view");
    }

    void MakeBuffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage) {
        buffer.size = size;
        VkBufferCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size; info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVk(CreateBuffer(device, &info, nullptr, &buffer.buffer), "Create synthetic buffer");
        VkMemoryRequirements requirements {};
        GetBufferMemoryRequirements(device, buffer.buffer, &requirements);
        Allocate(buffer.memory, requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        CheckVk(BindBufferMemory(device, buffer.buffer, buffer.memory, 0), "Bind synthetic buffer memory");
    }

    void Transition(const Image& image, VkImageLayout from, VkImageLayout to,
                    VkAccessFlags src, VkAccessFlags dst) const {
        VkImageMemoryBarrier barrier {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = src; barrier.dstAccessMask = dst;
        barrier.oldLayout = from; barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        CmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    static VkBufferImageCopy CopyRegion(const Image& image, VkDeviceSize offset = 0) {
        VkBufferImageCopy copy {};
        copy.bufferOffset = offset;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {image.width, image.height, 1};
        return copy;
    }

    static NVSDK_NGX_Resource_VK Resource(const Image& image, bool writable) {
        return NVSDK_NGX_Create_ImageView_Resource_VK(image.view, image.image,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, image.format, image.width, image.height, writable);
    }
};

// Test input values are nonnegative and in [0, 1]. Round through IEEE half
// precision without requiring the build machine to support F16C instructions.
uint16_t TestHalf(float value) {
    const auto bits = std::bit_cast<uint32_t>(value);
    const auto exponent = static_cast<int>((bits >> 23) & 255u) - 127 + 15;
    if (exponent <= 0) return 0;
    return static_cast<uint16_t>((static_cast<uint32_t>(exponent) << 10) | ((bits >> 13) & 1023u));
}

float ReadHalf(uint16_t value) {
    const auto exponent = (value >> 10) & 31;
    const auto fraction = value & 1023;
    const float sign = (value & 0x8000) ? -1.0f : 1.0f;
    if (exponent == 31) return fraction ? std::numeric_limits<float>::quiet_NaN() : sign * std::numeric_limits<float>::infinity();
    if (exponent == 0) return sign * std::ldexp(static_cast<float>(fraction), -24);
    return sign * std::ldexp(1.0f + static_cast<float>(fraction) / 1024.0f, exponent - 15);
}

void EvaluateSynthetic(Context& context, VkPhysicalDevice physical, uint32_t queue_family,
                       unsigned input_width, unsigned input_height, unsigned width, unsigned height,
                       const std::filesystem::path& output_directory, Json& report) {
    if (width > 4096 || height > 4096) throw std::runtime_error("Synthetic evaluation is limited to 4096 by 4096");
    Synthetic gpu(context, physical);
    gpu.GetDeviceQueue(context.device, queue_family, 0, &gpu.queue);
    VkCommandPoolCreateInfo pool {};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.queueFamilyIndex = queue_family;
    CheckVk(gpu.CreateCommandPool(context.device, &pool, nullptr, &gpu.pool), "Create synthetic command pool");
    VkCommandBufferAllocateInfo allocate {};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = gpu.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    CheckVk(gpu.AllocateCommandBuffers(context.device, &allocate, &gpu.command), "Allocate synthetic command buffer");
    gpu.MakeImage(gpu.images[0], input_width, input_height, VK_FORMAT_R16G16B16A16_SFLOAT, false);
    gpu.MakeImage(gpu.images[1], input_width, input_height, VK_FORMAT_R32_SFLOAT, false);
    gpu.MakeImage(gpu.images[2], input_width, input_height, VK_FORMAT_R16G16_SFLOAT, false);
    gpu.MakeImage(gpu.images[3], width, height, VK_FORMAT_R16G16B16A16_SFLOAT, true);
    const auto pixels = static_cast<VkDeviceSize>(input_width) * input_height;
    const std::array<VkDeviceSize, 3> offsets {0, pixels * 8, pixels * 12};
    gpu.MakeBuffer(gpu.buffers[0], pixels * 16, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    gpu.MakeBuffer(gpu.buffers[1], static_cast<VkDeviceSize>(width) * height * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    void* mapped {};
    CheckVk(gpu.MapMemory(context.device, gpu.buffers[0].memory, 0, VK_WHOLE_SIZE, 0, &mapped), "Map synthetic upload");
    auto* colors = static_cast<uint16_t*>(mapped);
    auto* depth = reinterpret_cast<float*>(static_cast<std::byte*>(mapped) + offsets[1]);
    auto* motion = reinterpret_cast<uint16_t*>(static_cast<std::byte*>(mapped) + offsets[2]);
    for (unsigned y = 0; y < input_height; ++y) for (unsigned x = 0; x < input_width; ++x) {
        const auto i = static_cast<size_t>(y) * input_width + x;
        const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(input_width);
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(input_height);
        const bool rectangle = u > 0.30f && u < 0.70f && v > 0.25f && v < 0.75f;
        const float checker = ((x / 16 + y / 16) & 1) ? 0.08f : 0.0f;
        colors[i * 4] = TestHalf(rectangle ? 0.85f : 0.10f + 0.55f * u);
        colors[i * 4 + 1] = TestHalf(rectangle ? 0.15f : 0.10f + 0.55f * v);
        colors[i * 4 + 2] = TestHalf(rectangle ? 0.08f : 0.20f + checker);
        colors[i * 4 + 3] = TestHalf(1.0f);
        depth[i] = rectangle ? 0.25f : 0.75f;
        motion[i * 2] = 0; motion[i * 2 + 1] = 0;
    }
    gpu.UnmapMemory(context.device, gpu.buffers[0].memory);
    VkCommandBufferBeginInfo begin {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(gpu.BeginCommandBuffer(gpu.command, &begin), "Begin synthetic command buffer");
    for (unsigned i = 0; i < 3; ++i) {
        gpu.Transition(gpu.images[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        const auto region = Synthetic::CopyRegion(gpu.images[i], offsets[i]);
        gpu.CmdCopyBufferToImage(gpu.command, gpu.buffers[0].buffer, gpu.images[i].image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        gpu.Transition(gpu.images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }
    gpu.Transition(gpu.images[3], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkClearColorValue sentinel {};
    for (auto& value: sentinel.float32) value = std::numeric_limits<float>::quiet_NaN();
    const VkImageSubresourceRange range {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    gpu.CmdClearColorImage(gpu.command, gpu.images[3].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &sentinel, 1, &range);
    gpu.Transition(gpu.images[3], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    CheckNgx(report, "AllocateParameters_synthetic", NVSDK_NGX_VULKAN_AllocateParameters(&gpu.parameters));
    NVSDK_NGX_Parameter_SetUI(gpu.parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                             NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    NVSDK_NGX_DLSS_Create_Params settings {};
    settings.Feature.InWidth = input_width; settings.Feature.InHeight = input_height;
    settings.Feature.InTargetWidth = width; settings.Feature.InTargetHeight = height;
    settings.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_MaxQuality;
    settings.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    CheckNgx(report, "CREATE_DLSS_synthetic", NGX_VULKAN_CREATE_DLSS_EXT1(
        context.device, gpu.command, 1, 1, &gpu.feature, gpu.parameters, &settings));
    auto color = Synthetic::Resource(gpu.images[0], false);
    auto depth_resource = Synthetic::Resource(gpu.images[1], false);
    auto mv = Synthetic::Resource(gpu.images[2], false);
    auto output = Synthetic::Resource(gpu.images[3], true);
    NVSDK_NGX_VK_DLSS_Eval_Params evaluate {};
    evaluate.Feature.pInColor = &color; evaluate.Feature.pInOutput = &output;
    evaluate.pInDepth = &depth_resource; evaluate.pInMotionVectors = &mv;
    evaluate.InRenderSubrectDimensions = {input_width, input_height};
    evaluate.InReset = 1;
    evaluate.InMVScaleX = 1.0f; evaluate.InMVScaleY = 1.0f;
    evaluate.InPreExposure = 1.0f; evaluate.InExposureScale = 1.0f;
    evaluate.InFrameTimeDeltaInMsec = 16.6667f;
    CheckNgx(report, "EVALUATE_DLSS_synthetic", NGX_VULKAN_EVALUATE_DLSS_EXT(gpu.command, gpu.feature, gpu.parameters, &evaluate));
    gpu.Transition(gpu.images[3], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    const auto output_region = Synthetic::CopyRegion(gpu.images[3]);
    gpu.CmdCopyImageToBuffer(gpu.command, gpu.images[3].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             gpu.buffers[1].buffer, 1, &output_region);
    VkMemoryBarrier host_read {};
    host_read.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    host_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host_read.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    gpu.CmdPipelineBarrier(gpu.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                           0, 1, &host_read, 0, nullptr, 0, nullptr);
    CheckVk(gpu.EndCommandBuffer(gpu.command), "End synthetic command buffer");
    VkFenceCreateInfo fence {};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    CheckVk(gpu.CreateFence(context.device, &fence, nullptr, &gpu.fence), "Create synthetic fence");
    VkSubmitInfo submit {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &gpu.command;
    CheckVk(gpu.QueueSubmit(gpu.queue, 1, &submit, gpu.fence), "Submit synthetic DLSS");
    CheckVk(gpu.WaitForFences(context.device, 1, &gpu.fence, VK_TRUE, 30'000'000'000ull), "Wait for synthetic DLSS");
    CheckVk(gpu.MapMemory(context.device, gpu.buffers[1].memory, 0, VK_WHOLE_SIZE, 0, &mapped), "Map synthetic output");
    std::vector<uint16_t> output_half(static_cast<size_t>(width) * height * 4);
    std::memcpy(output_half.data(), mapped, output_half.size() * sizeof(uint16_t));
    gpu.UnmapMemory(context.device, gpu.buffers[1].memory);
    size_t finite {}, nonzero {};
    float minimum = std::numeric_limits<float>::infinity(), maximum = -minimum;
    double sum {};
    for (size_t i = 0; i < output_half.size(); ++i) {
        if (i % 4 == 3) continue; // Alpha is unspecified without AlphaUpscaling.
        const auto value = ReadHalf(output_half[i]);
        if (!std::isfinite(value)) continue;
        ++finite; if (value != 0) ++nonzero;
        minimum = std::min(minimum, value); maximum = std::max(maximum, value); sum += value;
    }
    const auto expected = static_cast<size_t>(width) * height * 3;
    report["synthetic"] = {{"input", {input_width, input_height}}, {"output", {width, height}},
        {"frames", 1}, {"jitter", {0.0, 0.0}}, {"motion", "zero: static scene"}, {"reset", true},
        {"requested_preset", "K"}, {"effective_preset_verified", false},
        {"rgb_component_count", expected}, {"finite_rgb_components", finite}, {"nonzero_rgb_components", nonzero},
        {"minimum", finite ? Json(minimum) : Json(nullptr)}, {"maximum", finite ? Json(maximum) : Json(nullptr)},
        {"mean", finite ? Json(sum / static_cast<double>(finite)) : Json(nullptr)},
        {"temporal_quality_tested", false}, {"gt7_data_used", false}};
    report["feature_evaluation_tested"] = true;
    if (finite != expected || nonzero == 0 || maximum - minimum < 0.05f)
        throw std::runtime_error("Synthetic DLSS output was non-finite, empty, or constant");
    const auto raw_path = output_directory / L"synthetic-output.rgba16f";
    const auto preview_path = output_directory / L"synthetic-output.ppm";
    std::ofstream raw(raw_path, std::ios::binary);
    raw.exceptions(std::ios::badbit | std::ios::failbit);
    raw.write(reinterpret_cast<const char*>(output_half.data()), static_cast<std::streamsize>(output_half.size() * sizeof(uint16_t)));
    std::ofstream preview(preview_path, std::ios::binary);
    preview.exceptions(std::ios::badbit | std::ios::failbit);
    preview << "P6\n" << width << ' ' << height << "\n255\n";
    for (size_t i = 0; i < output_half.size(); ++i) {
        if (i % 4 == 3) continue;
        const auto value = static_cast<unsigned char>(std::clamp(ReadHalf(output_half[i]), 0.0f, 1.0f) * 255.0f + 0.5f);
        preview.put(static_cast<char>(value));
    }
    report["synthetic"]["raw_output"] = raw_path.u8string();
    report["synthetic"]["preview"] = preview_path.u8string();
    report["synthetic_evaluation_passed"] = true;
}

#include "replay.inl"

void Probe(const std::filesystem::path& runtime_directory, const std::filesystem::path& log_directory,
           unsigned width, unsigned height, bool evaluate_synthetic, const Json* replay,
           const std::filesystem::path& replay_directory, Json& report) {
    if (!std::filesystem::is_regular_file(runtime_directory / L"nvngx_dlss.dll"))
        throw std::runtime_error("The supplied runtime directory does not contain nvngx_dlss.dll");
    std::filesystem::create_directories(log_directory);
    const auto runtime_wide = runtime_directory.wstring();
    const auto log_wide = log_directory.wstring();
    const wchar_t* path_list[] = {runtime_wide.c_str()};
    NVSDK_NGX_FeatureCommonInfo common {};
    common.PathListInfo.Path = path_list;
    common.PathListInfo.Length = 1;
    NVSDK_NGX_FeatureDiscoveryInfo discovery {};
    discovery.SDKVersion = NVSDK_NGX_Version_API;
    discovery.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    discovery.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    discovery.Identifier.v.ProjectDesc = {ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, EngineVersion};
    discovery.ApplicationDataPath = log_wide.c_str();
    discovery.FeatureInfo = &common;

    Context context;
    context.loader = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!context.loader) throw std::runtime_error("Cannot load the system Vulkan loader");
    context.gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(context.loader, "vkGetInstanceProcAddr"));
    if (!context.gipa) throw std::runtime_error("The Vulkan loader has no vkGetInstanceProcAddr");
    const auto enumerate_instance_extensions = InstanceProc<PFN_vkEnumerateInstanceExtensionProperties>(context.gipa, nullptr, "vkEnumerateInstanceExtensionProperties");
    const auto create_instance = InstanceProc<PFN_vkCreateInstance>(context.gipa, nullptr, "vkCreateInstance");
    uint32_t count {};
    CheckVk(enumerate_instance_extensions(nullptr, &count, nullptr), "Enumerate instance extensions");
    std::vector<VkExtensionProperties> available(count);
    CheckVk(enumerate_instance_extensions(nullptr, &count, available.data()), "Enumerate instance extensions");
    uint32_t required_count {};
    VkExtensionProperties* required {};
    CheckNgx(report, "GetFeatureInstanceExtensionRequirements",
             NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&discovery, &required_count, &required));
    auto instance_extensions = ValidateExtensions(required, required_count, available, report, "instance_extensions");
    VkApplicationInfo application {};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Kyty DLSS capability probe";
    application.pEngineName = EngineVersion;
    application.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo instance_info {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &application;
    instance_info.enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size());
    instance_info.ppEnabledExtensionNames = instance_extensions.data();
    CheckVk(create_instance(&instance_info, nullptr, &context.instance), "Create instance");
    context.destroy_instance = InstanceProc<PFN_vkDestroyInstance>(context.gipa, context.instance, "vkDestroyInstance");
    const auto enumerate_devices = InstanceProc<PFN_vkEnumeratePhysicalDevices>(context.gipa, context.instance, "vkEnumeratePhysicalDevices");
    const auto get_properties = InstanceProc<PFN_vkGetPhysicalDeviceProperties>(context.gipa, context.instance, "vkGetPhysicalDeviceProperties");
    const auto get_queues = InstanceProc<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(context.gipa, context.instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    const auto enumerate_device_extensions = InstanceProc<PFN_vkEnumerateDeviceExtensionProperties>(context.gipa, context.instance, "vkEnumerateDeviceExtensionProperties");
    const auto create_device = InstanceProc<PFN_vkCreateDevice>(context.gipa, context.instance, "vkCreateDevice");
    const auto gdpa = InstanceProc<PFN_vkGetDeviceProcAddr>(context.gipa, context.instance, "vkGetDeviceProcAddr");
    CheckVk(enumerate_devices(context.instance, &count, nullptr), "Enumerate physical devices");
    std::vector<VkPhysicalDevice> devices(count);
    CheckVk(enumerate_devices(context.instance, &count, devices.data()), "Enumerate physical devices");
    VkPhysicalDevice physical {};
    report["devices"] = Json::array();
    for (auto candidate: devices) {
        VkPhysicalDeviceProperties properties {};
        get_properties(candidate, &properties);
        report["devices"].push_back({{"name", properties.deviceName}, {"vendor_id", Hex(properties.vendorID)},
                                     {"device_id", Hex(properties.deviceID)}, {"driver_version_raw", properties.driverVersion}});
        if (!physical && properties.vendorID == 0x10de) {
            physical = candidate;
            report["selected_device"] = report["devices"].back();
        }
    }
    if (!physical) throw std::runtime_error("No NVIDIA Vulkan physical device found");
    NVSDK_NGX_FeatureRequirement requirements {};
    CheckNgx(report, "GetFeatureRequirements", NVSDK_NGX_VULKAN_GetFeatureRequirements(context.instance, physical, &discovery, &requirements));
    report["feature_support_flags"] = static_cast<unsigned>(requirements.FeatureSupported);
    if (requirements.FeatureSupported != 0) throw std::runtime_error("NGX reports DLSS is unsupported on this device");
    CheckNgx(report, "GetFeatureDeviceExtensionRequirements",
             NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(context.instance, physical, &discovery, &required_count, &required));
    CheckVk(enumerate_device_extensions(physical, nullptr, &count, nullptr), "Enumerate device extensions");
    available.resize(count);
    CheckVk(enumerate_device_extensions(physical, nullptr, &count, available.data()), "Enumerate device extensions");
    auto device_extensions = ValidateExtensions(required, required_count, available, report, "device_extensions");
    get_queues(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    get_queues(physical, &count, queues.data());
    auto queue = std::find_if(queues.begin(), queues.end(), [](const auto& value) {
        return value.queueCount && (value.queueFlags & VK_QUEUE_GRAPHICS_BIT) && (value.queueFlags & VK_QUEUE_COMPUTE_BIT);
    });
    if (queue == queues.end()) throw std::runtime_error("No Vulkan graphics/compute queue found");
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = static_cast<uint32_t>(queue - queues.begin());
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    report["queue_family"] = queue_info.queueFamilyIndex;
    VkDeviceCreateInfo device_info {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
    device_info.ppEnabledExtensionNames = device_extensions.data();
    VkPhysicalDeviceVulkan12Features supported12 {};
    supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceFeatures2 supported_features {};
    supported_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supported_features.pNext = &supported12;
    const auto get_features = InstanceProc<PFN_vkGetPhysicalDeviceFeatures2>(context.gipa, context.instance, "vkGetPhysicalDeviceFeatures2");
    get_features(physical, &supported_features);
    VkPhysicalDeviceVulkan12Features enabled12 {};
    enabled12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    enabled12.bufferDeviceAddress = supported12.bufferDeviceAddress;
    VkPhysicalDeviceFeatures enabled {};
    enabled.shaderInt64 = supported_features.features.shaderInt64;
    device_info.pNext = &enabled12;
    device_info.pEnabledFeatures = &enabled;
    report["enabled_features"] = {{"bufferDeviceAddress", enabled12.bufferDeviceAddress != 0}, {"shaderInt64", enabled.shaderInt64 != 0}};
    if ((evaluate_synthetic || replay) && !enabled12.bufferDeviceAddress)
        throw std::runtime_error("DLSS evaluation requires bufferDeviceAddress");
    CheckVk(create_device(physical, &device_info, nullptr, &context.device), "Create device");
    context.destroy_device = reinterpret_cast<PFN_vkDestroyDevice>(gdpa(context.device, "vkDestroyDevice"));
    CheckNgx(report, "Init_with_ProjectID", NVSDK_NGX_VULKAN_Init_with_ProjectID(
        ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, EngineVersion, log_wide.c_str(), context.instance,
        physical, context.device, context.gipa, gdpa, &common));
    context.ngx_initialized = true;
    CheckNgx(report, "GetCapabilityParameters", NVSDK_NGX_VULKAN_GetCapabilityParameters(&context.capabilities));
    if (const auto module = GetModuleHandleW(L"nvngx_dlss.dll")) {
        std::vector<wchar_t> module_path(32768);
        const auto length = GetModuleFileNameW(module, module_path.data(), static_cast<DWORD>(module_path.size()));
        if (length && length < module_path.size())
            report["loaded_runtime"] = std::filesystem::path(std::wstring(module_path.data(), length)).u8string();
    }
    int supported {}, needs_driver {};
    CheckNgx(report, "SuperSampling.Available", NVSDK_NGX_Parameter_GetI(context.capabilities, NVSDK_NGX_Parameter_SuperSampling_Available, &supported));
    CheckNgx(report, "SuperSampling.NeedsUpdatedDriver", NVSDK_NGX_Parameter_GetI(context.capabilities, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver));
    report["dlss_available"] = supported != 0;
    report["needs_updated_driver"] = needs_driver != 0;
    if (!supported || needs_driver) throw std::runtime_error("DLSS capability check failed");
    unsigned optimal_width {}, optimal_height {}, max_width {}, max_height {}, min_width {}, min_height {};
    float sharpness {};
    NVSDK_NGX_Parameter_SetUI(context.capabilities, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    CheckNgx(report, "DLSS_GET_OPTIMAL_SETTINGS_Quality", NGX_DLSS_GET_OPTIMAL_SETTINGS(
        context.capabilities, width, height, NVSDK_NGX_PerfQuality_Value_MaxQuality,
        &optimal_width, &optimal_height, &max_width, &max_height, &min_width, &min_height, &sharpness));
    report["quality"] = {{"output", {width, height}}, {"recommended_input", {optimal_width, optimal_height}},
                          {"minimum_input", {min_width, min_height}}, {"maximum_input", {max_width, max_height}},
                          {"requested_preset", "K"}, {"effective_preset_verified", false}};
    report["capability_probe_passed"] = true;
    if (evaluate_synthetic)
        EvaluateSynthetic(context, physical, queue_info.queueFamilyIndex, optimal_width, optimal_height,
                           width, height, log_directory.parent_path(), report);
    if (replay)
        EvaluateReplay(context, physical, queue_info.queueFamilyIndex, *replay, replay_directory,
                       log_directory.parent_path(), report);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const bool evaluate = (argc == 4 || argc == 6) && std::wstring(argv[argc - 1]) == L"--evaluate";
    const bool replay_mode = argc == 5 && std::wstring(argv[3]) == L"--replay";
    if (argc != 3 && argc != 5 && !evaluate) {
        std::cerr << "Usage: kyty_dlss_probe <runtime-directory> <report.json> [output-width output-height] [--evaluate]\n"
                     "       kyty_dlss_probe <runtime-directory> <report.json> --replay <sequence.json>\n";
        return 2;
    }
    Json report { {"schema", 1}, {"probe", "native NGX Vulkan DLSS SR capability"},
                  {"gt7_integration_tested", false}, {"feature_evaluation_tested", false},
                  {"capability_probe_passed", false}, {"calls", Json::array()} };
    int exit_code = 1;
    const auto output = std::filesystem::absolute(argv[2]);
    try {
        Json replay;
        std::filesystem::path replay_path;
        if (replay_mode) {
            replay_path = std::filesystem::absolute(argv[4]);
            std::ifstream stream(replay_path, std::ios::binary);
            if (!stream) throw std::runtime_error("Cannot open replay sequence");
            stream >> replay;
            ValidateReplay(replay);
            report["replay_sequence"] = replay_path.u8string();
        }
        const unsigned width = replay_mode ? ReplayOutputDimension(replay, "width", "output_width") :
            (argc >= 5 ? Dimension(argv[3]) : (evaluate ? 640u : 2560u));
        const unsigned height = replay_mode ? ReplayOutputDimension(replay, "height", "output_height") :
            (argc >= 5 ? Dimension(argv[4]) : (evaluate ? 360u : 1440u));
        const auto runtime = std::filesystem::absolute(argv[1]);
        report["runtime_directory"] = runtime.u8string();
        Probe(runtime, output.parent_path() / L"ngx-logs", width, height, evaluate,
              replay_mode ? &replay : nullptr, replay_path.parent_path(), report);
        exit_code = 0;
    } catch (const std::exception& error) {
        report["error"] = error.what();
    }
    report["exit_code"] = exit_code;
    try {
        std::filesystem::create_directories(output.parent_path());
        std::ofstream stream(output, std::ios::binary);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream << report.dump(2) << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Cannot save report: " << error.what() << '\n';
        return 2;
    }
    std::cout << report.dump(2) << '\n';
    return exit_code;
}
