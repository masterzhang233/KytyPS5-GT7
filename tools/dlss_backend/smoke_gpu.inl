// Host-owned Vulkan smoke-test helpers, adapted from the independent probe. No NGX SDK.
struct SmokeGpu {
    VkDevice device;
    VkPhysicalDeviceMemoryProperties memory_properties {};
    VkCommandPool pool {};
    VkCommandBuffer command {};
    VkFence fence {};
    VkQueue queue {};
    struct Image {
        VkImage image {};
        VkImageView view {};
        VkDeviceMemory memory {};
        VkFormat format {};
        uint32_t width {}, height {};
    };
    struct Buffer { VkBuffer buffer {}; VkDeviceMemory memory {}; VkDeviceSize size {}; };
    std::array<Image, 8> images {};
    std::array<Buffer, 3> buffers {};

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

    explicit SmokeGpu(HostContext& context, VkPhysicalDevice physical): device(context.device) {
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

    ~SmokeGpu() {
        // Includes the exceptional path after a submit or timeout. Resources and
        // NGX handles must never be freed while queued GPU commands still use them.
        if (queue) QueueWaitIdle(queue);
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

    static KytyDlssImage Resource(const Image& image) {
        return {image.image, image.view, image.format, image.width, image.height};
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
