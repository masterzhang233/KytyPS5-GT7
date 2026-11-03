#ifndef KYTY_DLSS_BACKEND_H
#define KYTY_DLSS_BACKEND_H

/* Versioned C ABI only. No NVIDIA SDK types, C++ ownership, or CRT allocation crosses it.
 * Host and DLL must be Windows x64 builds with the standard Vulkan C handle ABI.
 * All strings and arrays are borrowed for the duration of a call.
 * Calls into NGX are internally serialized. The host still owns handle lifetime:
 * no call using a context may race its destruction, or occur after destruction.
 * Record calls must match GPU execution order on one queue, with no other NGX
 * client using this process-global NGX initialization concurrently. */
#include <stdint.h>
#include <wchar.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KYTY_DLSS_ABI_VERSION 1u
#define KYTY_DLSS_EXTENSION_CAPACITY 32u
#define KYTY_DLSS_FEATURE_BUFFER_DEVICE_ADDRESS 1u
#define KYTY_DLSS_FEATURE_SHADER_INT64 2u

typedef struct KytyDlssContext KytyDlssContext;
typedef enum KytyDlssStatus {
    KYTY_DLSS_OK = 0,
    KYTY_DLSS_INVALID_ARGUMENT = 1,
    KYTY_DLSS_UNSUPPORTED = 2,
    KYTY_DLSS_NGX_FAILED = 3,
    KYTY_DLSS_INTERNAL_ERROR = 4,
    KYTY_DLSS_BUSY = 5,
    KYTY_DLSS_POISONED = 6
} KytyDlssStatus;
typedef enum KytyDlssQuality {
    KYTY_DLSS_QUALITY = 0,
    KYTY_DLSS_PERFORMANCE = 1,
    KYTY_DLSS_DLAA = 2
} KytyDlssQuality;
typedef struct KytyDlssError {
    uint32_t ngx_result;
    char message[512];
} KytyDlssError;
typedef struct KytyDlssPaths {
    uint32_t struct_size;
    const wchar_t* runtime_directory; /* absolute; contains nvngx_dlss.dll */
    const wchar_t* log_directory;     /* absolute; created if necessary */
} KytyDlssPaths;
typedef struct KytyDlssExtensions {
    uint32_t struct_size;
    uint32_t count;
    char names[KYTY_DLSS_EXTENSION_CAPACITY][VK_MAX_EXTENSION_NAME_SIZE];
} KytyDlssExtensions;
typedef struct KytyDlssCreateInfo {
    uint32_t struct_size;
    KytyDlssPaths paths;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    PFN_vkGetDeviceProcAddr get_device_proc_addr;
    uint32_t queue_family_index; /* graphics + compute; all evaluations use this family */
    uint32_t enabled_feature_flags; /* host attests features enabled at device creation */
    uint32_t enabled_device_extension_count;
    const char* const* enabled_device_extensions;
    uint32_t input_width;
    uint32_t input_height;
    uint32_t output_width;
    uint32_t output_height;
    KytyDlssQuality quality;
} KytyDlssCreateInfo;
typedef struct KytyDlssOptimalSettings {
    uint32_t struct_size;
    uint32_t optimal_width, optimal_height;
    uint32_t min_width, min_height;
    uint32_t max_width, max_height;
} KytyDlssOptimalSettings;
typedef struct KytyDlssImage {
    VkImage image;
    VkImageView view;
    VkFormat format;
    uint32_t width, height;
    /* 2D, mip 0, layer 0, one mip/layer, sample count 1, COLOR aspect.
     * Inputs: sampled images already in SHADER_READ_ONLY_OPTIMAL.
     * Output: storage image already in GENERAL. No resource ownership transfer occurs. */
} KytyDlssImage;
typedef struct KytyDlssEvaluateInfo {
    uint32_t struct_size;
    VkCommandBuffer command_buffer; /* recording primary buffer, outside rendering;
                                      call on the host's actual command-recording thread */
    KytyDlssImage color;   /* R16G16B16A16_SFLOAT, HDR, input extent */
    KytyDlssImage depth;   /* R32_SFLOAT, reverse Z [0,1], input extent */
    KytyDlssImage motion;  /* R32G32_SFLOAT, clean current->previous INPUT pixels */
    KytyDlssImage output;  /* R16G16B16A16_SFLOAT, output extent; RGB defined, alpha not */
    float jitter_x, jitter_y; /* actual projection displacement in input pixels */
    float frame_time_ms;
    float pre_exposure;
    float exposure_scale;
    uint32_t reset_history;
} KytyDlssEvaluateInfo;
typedef struct KytyDlssRecordResult {
    uint32_t struct_size;
    uint32_t feature_created;
    uint32_t reset_used;
    uint64_t evaluations_recorded;
} KytyDlssRecordResult;

typedef struct KytyDlssApi {
    uint32_t struct_size;
    uint32_t abi_version;
    KytyDlssStatus (*query_instance_extensions)(const KytyDlssPaths*, KytyDlssExtensions*, KytyDlssError*);
    KytyDlssStatus (*query_device_extensions)(const KytyDlssPaths*, VkInstance, VkPhysicalDevice, KytyDlssExtensions*, KytyDlssError*);
    KytyDlssStatus (*create_context)(const KytyDlssCreateInfo*, KytyDlssContext**, KytyDlssError*);
    KytyDlssStatus (*query_optimal)(KytyDlssContext*, KytyDlssOptimalSettings*, KytyDlssError*);
    /* Successful recording does not submit or complete GPU work. NGX clobbers
     * Vulkan pipeline/descriptor/push-constant/dynamic state; invalidate the host
     * command-state cache and rebind before its next draw/dispatch.
     * INVALID_ARGUMENT guarantees this call recorded nothing and did not poison
     * the context. EVERY OTHER non-OK status requires abandoning the buffer:
     * DO NOT SUBMIT it, even if it also contains earlier guest commands. The
     * buffer may be partially written after NGX/internal exceptions, and a
     * POISONED context may refer to a previous such failure. Discard it, finish
     * any earlier submitted feature work, then destroy/recreate the context.
     * A host unable to discard/recover its shared primary buffer must terminate
     * the experimental session rather than silently continue recording. */
    KytyDlssStatus (*record_evaluate)(KytyDlssContext*, const KytyDlssEvaluateInfo*, KytyDlssRecordResult*, KytyDlssError*);
    /* No waits. host_gpu_work_completed MUST equal 1: every command referencing the
     * feature has completed or was never submitted and has been discarded. */
    KytyDlssStatus (*destroy_context)(KytyDlssContext*, uint32_t host_gpu_work_completed, KytyDlssError*);
} KytyDlssApi;

typedef KytyDlssStatus (*PFN_KytyDlssGetApi)(uint32_t abi_version, KytyDlssApi* api);
#if defined(KYTY_DLSS_BACKEND_BUILD)
__declspec(dllexport)
#endif
KytyDlssStatus KytyDlssGetApi(uint32_t abi_version, KytyDlssApi* api);

#ifdef __cplusplus
}
#endif
#endif
