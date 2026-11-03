// Device-creation-only diagnostic. No game, GPU submissions, or NGX feature.
#include "kyty_dlss_backend.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using Json=nlohmann::ordered_json;
namespace {
void VkCheck(VkResult r,const char* name){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(name)+" VkResult="+std::to_string(r));}
void NgxCheck(KytyDlssStatus s,const KytyDlssError& e){if(s!=KYTY_DLSS_OK)throw std::runtime_error(e.message);}
Json Modules() {
    Json result=Json::object();
    for(const wchar_t* name:{L"nvngx_dlss.dll",L"_nvngx.dll",L"nvngx.dll",L"nvcuda.dll",L"vulkan-1.dll",L"RTSSHooks64.dll",L"RTSSVkLayer64.dll"}) {
        const auto key=std::filesystem::path(name).string();const auto module=GetModuleHandleW(name);
        if(!module){result[key]=nullptr;continue;}
        std::vector<wchar_t> path(32768);const auto n=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
        const auto text=std::filesystem::path(std::wstring(path.data(),n)).u8string();result[key]=std::string(text.begin(),text.end());
    }
    return result;
}
struct Reservations {
    std::vector<void*> held;
    uint64_t bytes=0;
    ~Reservations(){for(auto p:held)VirtualFree(p,0,MEM_RELEASE);}
    void Reserve(uint64_t first,uint64_t end,uint64_t minimum) {
        for(uint64_t next=first;next<end;) {
            MEMORY_BASIC_INFORMATION m {};
            if(!VirtualQuery(reinterpret_cast<void*>(next),&m,sizeof(m)))throw std::runtime_error("VirtualQuery failed");
            const auto base=reinterpret_cast<uint64_t>(m.BaseAddress);
            const auto stop=std::min(end,base+uint64_t(m.RegionSize));
            if(stop<=next)throw std::runtime_error("VirtualQuery made no progress");
            const auto start=(std::max(base,next)+0xffffull)&~0xffffull;
            const auto aligned_end=stop&~0xffffull;
            if(m.State==MEM_FREE&&aligned_end>start&&aligned_end-start>minimum) {
                void* p=VirtualAlloc(reinterpret_cast<void*>(start),aligned_end-start,MEM_RESERVE,PAGE_NOACCESS);
                if(!p)throw std::runtime_error("VirtualAlloc reserve failed "+std::to_string(GetLastError()));
                held.push_back(p);bytes+=aligned_end-start;
            }
            next=stop;
        }
    }
};
struct Context {
    HMODULE backend {},loader {};VkInstance instance {};VkDevice device {};
    PFN_vkGetInstanceProcAddr gipa {};PFN_vkDestroyInstance destroy_instance {};PFN_vkDestroyDevice destroy_device {};
    ~Context(){if(device&&destroy_device)destroy_device(device,nullptr);if(instance&&destroy_instance)destroy_instance(instance,nullptr);
        if(backend)FreeLibrary(backend);if(loader)FreeLibrary(loader);}
    template<class T>T Get(const char* name){auto p=reinterpret_cast<T>(gipa(instance,name));if(!p)throw std::runtime_error(std::string("Missing ")+name);return p;}
};
void Run(const std::filesystem::path& dll,const std::filesystem::path& runtime,const Json& config,
         const std::filesystem::path& output,Json& report) {
    const auto mode=config.value("reservations",std::string("none"));
    const auto version=config.value("api_minor",3u);
    if(version!=2&&version!=3)throw std::runtime_error("api_minor must be 2 or 3");
    if(mode!="none"&&mode!="guest"&&mode!="early_cuda_guest")throw std::runtime_error("Unknown reservations mode");
    if(mode=="early_cuda_guest") {
        auto cuda=LoadLibraryExW(L"nvcuda.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!cuda)throw std::runtime_error("Cannot load nvcuda.dll");
        auto init=reinterpret_cast<int(__stdcall*)(unsigned int)>(GetProcAddress(cuda,"cuInit"));
        if(!init)throw std::runtime_error("No cuInit");
        Reservations temporary;temporary.Reserve(0x10000,1024ull<<30,0);
        report["early_cuda_temporary_reserved_bytes"]=temporary.bytes;
        report["early_cuda_result"]=init(0); // module intentionally retained for process lifetime
    }
    Reservations guest;
    if(mode!="none") {
        // Same address intervals and >16MiB free-region threshold as ReserveGuestRegions.
        // MEM_RESERVE rather than placeholders: this test concerns address availability only.
        guest.Reserve(0x40000,0x1000000000ull,0x1000000);
        guest.Reserve(0x1000000000ull,0xfc00000000ull,0x1000000);
        guest.Reserve(0x080000000000ull,0x088000000000ull,0x1000000);
    }
    report["guest_reserved_bytes"]=guest.bytes;report["guest_reservation_count"]=guest.held.size();
    report["modules_before_backend"]=Modules();
    Context c;c.backend=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!c.backend)throw std::runtime_error("Backend load failed");
    const auto get=reinterpret_cast<PFN_KytyDlssGetApi>(GetProcAddress(c.backend,"KytyDlssGetApi"));
    KytyDlssApi api {};api.struct_size=sizeof(api);
    if(!get||get(KYTY_DLSS_ABI_VERSION,&api)!=KYTY_DLSS_OK)throw std::runtime_error("Backend ABI failed");
    const auto logs=(output.parent_path()/L"device-probe-ngx-logs").wstring(),runtime_w=runtime.wstring();
    const KytyDlssPaths paths {sizeof(KytyDlssPaths),runtime_w.c_str(),logs.c_str()};KytyDlssError error {};
    KytyDlssExtensions ie {};ie.struct_size=sizeof(ie);
    NgxCheck(api.query_instance_extensions(&paths,&ie,&error),error);
    report["modules_after_query_instance"]=Modules();
    std::vector<const char*> instance_extensions;
    for(uint32_t n=0;n<ie.count;++n)instance_extensions.push_back(ie.names[n]);
    if(config.value("emulator_extensions",false)) {
        instance_extensions.push_back("VK_KHR_surface");instance_extensions.push_back("VK_KHR_win32_surface");
        instance_extensions.push_back("VK_EXT_debug_utils");
    }
    c.loader=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!c.loader)throw std::runtime_error("Vulkan loader failed");
    c.gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(c.loader,"vkGetInstanceProcAddr"));
    VkApplicationInfo app {};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_MAKE_API_VERSION(0,1,version,0);
    app.pApplicationName="Kyty device-only NGX diagnostic";
    if(config.value("emulator_identity",false)){app.pApplicationName="Kyty";app.pEngineName="Kyty";app.applicationVersion=1;app.engineVersion=1;}
    VkInstanceCreateInfo ici {};ici.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;ici.pApplicationInfo=&app;
    ici.enabledExtensionCount=static_cast<uint32_t>(instance_extensions.size());ici.ppEnabledExtensionNames=instance_extensions.data();
    VkCheck(c.Get<PFN_vkCreateInstance>("vkCreateInstance")(&ici,nullptr,&c.instance),"vkCreateInstance");
    c.destroy_instance=c.Get<PFN_vkDestroyInstance>("vkDestroyInstance");
    auto enumerate=c.Get<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");uint32_t count=0;
    VkCheck(enumerate(c.instance,&count,nullptr),"enumerate");std::vector<VkPhysicalDevice> devices(count);
    VkCheck(enumerate(c.instance,&count,devices.data()),"enumerate");VkPhysicalDevice physical {};
    for(auto p:devices) {VkPhysicalDeviceProperties prop {};c.Get<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(p,&prop);
        if(prop.vendorID==0x10de&&!physical){physical=p;report["gpu"]=prop.deviceName;report["driver_version"]=prop.driverVersion;}}
    if(!physical)throw std::runtime_error("No NVIDIA device");
    KytyDlssExtensions de {};de.struct_size=sizeof(de);
    NgxCheck(api.query_device_extensions(&paths,c.instance,physical,&de,&error),error);
    report["modules_after_query_device"]=Modules();
    std::vector<std::string> owned;
    if(config.value("emulator_extensions",false)) {
        auto enumerate_ext=c.Get<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
        VkCheck(enumerate_ext(physical,nullptr,&count,nullptr),"extension count");std::vector<VkExtensionProperties> available(count);
        VkCheck(enumerate_ext(physical,nullptr,&count,available.data()),"extensions");
        for(const char* e:{"VK_KHR_swapchain","VK_EXT_depth_clip_control","VK_EXT_image_view_min_lod","VK_KHR_push_descriptor",
            "VK_KHR_maintenance1","VK_EXT_depth_clip_enable","VK_EXT_color_write_enable","VK_EXT_memory_budget",
            "VK_NV_device_diagnostic_checkpoints","VK_EXT_device_fault","VK_KHR_shader_clock","VK_NV_device_diagnostics_config",
            "VK_EXT_robustness2","VK_EXT_shader_image_atomic_int64","VK_EXT_provoking_vertex","VK_EXT_mesh_shader",
            "VK_KHR_workgroup_memory_explicit_layout","VK_EXT_depth_range_unrestricted","VK_KHR_fragment_shader_barycentric",
            "VK_NV_framebuffer_mixed_samples","VK_NV_coverage_reduction_mode","VK_EXT_attachment_feedback_loop_layout",
            "VK_EXT_attachment_feedback_loop_dynamic_state"}) {
            if(std::any_of(available.begin(),available.end(),[&](const auto& a){return std::strcmp(a.extensionName,e)==0;}))owned.emplace_back(e);
        }
    }
    const auto omit=config.value("omit_extension",std::string());
    for(uint32_t n=0;n<de.count;++n)if(omit!=de.names[n]&&std::find(owned.begin(),owned.end(),de.names[n])==owned.end())owned.emplace_back(de.names[n]);
    if(!omit.empty())owned.erase(std::remove(owned.begin(),owned.end(),omit),owned.end());
    std::vector<const char*> extensions;for(const auto& e:owned)extensions.push_back(e.c_str());report["device_extensions"]=owned;
    const auto queues=c.Get<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
    queues(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);queues(physical,&count,families.data());uint32_t family=0;
    while(family<count&&(families[family].queueFlags&3)!=3)++family;if(family==count)throw std::runtime_error("No graphics/compute family");
    VkPhysicalDeviceVulkan12Features f12 {};f12.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;f12.bufferDeviceAddress=VK_TRUE;
    VkPhysicalDeviceFeatures core {};core.shaderInt64=VK_TRUE;core.shaderStorageImageExtendedFormats=VK_TRUE;
    VkDeviceQueueCreateInfo q {};q.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;q.queueFamilyIndex=family;q.queueCount=1;const float priority=1;q.pQueuePriorities=&priority;
    VkDeviceCreateInfo ci {};ci.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;ci.pNext=&f12;ci.pEnabledFeatures=&core;
    ci.queueCreateInfoCount=1;ci.pQueueCreateInfos=&q;ci.enabledExtensionCount=static_cast<uint32_t>(extensions.size());ci.ppEnabledExtensionNames=extensions.data();
    report["features"]={"bufferDeviceAddress","shaderInt64","shaderStorageImageExtendedFormats"};
    VkPhysicalDeviceVulkan11Features f11 {}; f11.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    VkPhysicalDeviceVulkan13Features f13 {}; f13.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceColorWriteEnableFeaturesEXT color_write {}; color_write.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT;
    VkPhysicalDeviceDepthClipEnableFeaturesEXT depth_clip_enable {}; depth_clip_enable.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT;
    VkPhysicalDeviceImageViewMinLodFeaturesEXT min_lod {}; min_lod.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT;
    VkPhysicalDeviceDepthClipControlFeaturesEXT depth_clip_control {}; depth_clip_control.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT;
    VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR workgroup {}; workgroup.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR;
    VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR barycentric {}; barycentric.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR;
    VkPhysicalDeviceRobustness2FeaturesEXT robust2 {}; robust2.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
    VkPhysicalDeviceMeshShaderFeaturesEXT mesh {}; mesh.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT feedback_dynamic {}; feedback_dynamic.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_FEATURES_EXT;
    VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT feedback_layout {}; feedback_layout.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT;
    VkPhysicalDeviceProvokingVertexFeaturesEXT provoking {}; provoking.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT;
    VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT atomic {}; atomic.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT;
    VkPhysicalDeviceShaderClockFeaturesKHR clock {}; clock.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR;
    VkPhysicalDeviceCoverageReductionModeFeaturesNV reduction {}; reduction.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COVERAGE_REDUCTION_MODE_FEATURES_NV;
    VkPhysicalDeviceFaultFeaturesEXT fault {}; fault.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
    std::vector<VkBaseOutStructure*> nodes;
    const auto add=[&](auto& node){nodes.push_back(reinterpret_cast<VkBaseOutStructure*>(&node));};
    const auto link=[&](){for(size_t n=0;n<nodes.size();++n)nodes[n]->pNext=n+1<nodes.size()?nodes[n+1]:nullptr;};
    VkDeviceQueueCreateInfo emu_queues[2] {};const float priorities[2] {1,1};
    if(config.value("emulator_features",false)) {
        if(version!=3||!config.value("emulator_extensions",false))throw std::runtime_error("Emulator features require API1.3 and emulator_extensions");
        add(fault);add(reduction);add(clock);add(atomic);add(provoking);add(feedback_layout);add(feedback_dynamic);add(mesh);
        add(f13);add(robust2);add(barycentric);add(f12);add(f11);add(workgroup);add(depth_clip_control);add(min_lod);add(depth_clip_enable);add(color_write);link();
        VkPhysicalDeviceFeatures2 supported {}; supported.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;supported.pNext=nodes.front();
        c.Get<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2")(physical,&supported);
        const auto supported12=f12;const auto supported13=f13;const auto supported_core=supported.features;
        const auto supported_robust=robust2;const auto supported_workgroup=workgroup;
        const auto supported_mesh=mesh;const auto supported_feedback=feedback_layout;const auto supported_feedback_dynamic=feedback_dynamic;
        const auto supported_provoking=provoking;const auto supported_atomic=atomic;const auto supported_clock=clock;
        const auto supported_reduction=reduction;const auto supported_fault=fault;const auto supported_barycentric=barycentric;
        // Clear every queried feature while preserving type/link, then set the emulator's requests.
        const auto clear=[](auto& node){const auto type=node.sType;const auto next=node.pNext;node={};node.sType=type;node.pNext=next;};
        clear(f11);clear(f12);clear(f13);clear(color_write);clear(depth_clip_enable);clear(min_lod);clear(depth_clip_control);
        clear(workgroup);clear(barycentric);clear(robust2);clear(mesh);clear(feedback_dynamic);clear(feedback_layout);
        clear(provoking);clear(atomic);clear(clock);clear(reduction);clear(fault);
        f11.storageBuffer16BitAccess=VK_TRUE;
        f12.samplerMirrorClampToEdge=VK_TRUE;f12.timelineSemaphore=VK_TRUE;f12.shaderOutputLayer=VK_TRUE;
        f12.shaderOutputViewportIndex=VK_TRUE;f12.bufferDeviceAddress=VK_TRUE;f12.shaderBufferInt64Atomics=VK_TRUE;
        f12.storageBuffer8BitAccess=VK_TRUE;f12.shaderSampledImageArrayNonUniformIndexing=VK_TRUE;
        f12.shaderSharedInt64Atomics=supported12.shaderSharedInt64Atomics;
        f12.runtimeDescriptorArray=supported12.runtimeDescriptorArray;
        f12.descriptorBindingPartiallyBound=supported12.descriptorBindingPartiallyBound;
        f12.descriptorBindingSampledImageUpdateAfterBind=supported12.descriptorBindingSampledImageUpdateAfterBind;
        f12.descriptorBindingUpdateUnusedWhilePending=supported12.descriptorBindingUpdateUnusedWhilePending;
        f13.dynamicRendering=VK_TRUE;f13.synchronization2=VK_TRUE;f13.robustImageAccess=supported13.robustImageAccess;
        f13.pipelineCreationCacheControl=supported13.pipelineCreationCacheControl;
        // Captured RTX3080 min/max subgroup are both32, so emulator does not enable size control.
        robust2.robustBufferAccess2=supported_robust.robustBufferAccess2;robust2.robustImageAccess2=supported_robust.robustImageAccess2;
        robust2.nullDescriptor=supported_robust.nullDescriptor;
        workgroup.workgroupMemoryExplicitLayout=supported_workgroup.workgroupMemoryExplicitLayout;
        barycentric.fragmentShaderBarycentric=supported_barycentric.fragmentShaderBarycentric;
        mesh.meshShader=supported_mesh.meshShader;feedback_layout.attachmentFeedbackLoopLayout=supported_feedback.attachmentFeedbackLoopLayout;
        feedback_dynamic.attachmentFeedbackLoopDynamicState=supported_feedback_dynamic.attachmentFeedbackLoopDynamicState;
        provoking.provokingVertexLast=supported_provoking.provokingVertexLast;atomic.shaderImageInt64Atomics=supported_atomic.shaderImageInt64Atomics;
        clock.shaderDeviceClock=supported_clock.shaderDeviceClock;reduction.coverageReductionMode=supported_reduction.coverageReductionMode;
        fault.deviceFault=supported_fault.deviceFault;
        color_write.colorWriteEnable=VK_TRUE;depth_clip_enable.depthClipEnable=VK_TRUE;min_lod.minLod=VK_TRUE;depth_clip_control.depthClipControl=VK_TRUE;
        core={};core.fragmentStoresAndAtomics=VK_TRUE;core.samplerAnisotropy=VK_TRUE;core.robustBufferAccess=VK_TRUE;
        core.depthBounds=VK_TRUE;core.depthClamp=VK_TRUE;core.shaderStorageImageWriteWithoutFormat=VK_TRUE;core.shaderImageGatherExtended=VK_TRUE;
        core.shaderResourceMinLod=supported_core.shaderResourceMinLod;core.independentBlend=VK_TRUE;core.dualSrcBlend=VK_TRUE;
        core.tessellationShader=VK_TRUE;core.sampleRateShading=VK_TRUE;core.depthBiasClamp=VK_TRUE;core.shaderClipDistance=VK_TRUE;
        core.shaderCullDistance=VK_TRUE;core.largePoints=VK_TRUE;core.multiViewport=VK_TRUE;core.fillModeNonSolid=VK_TRUE;
        core.vertexPipelineStoresAndAtomics=VK_TRUE;core.occlusionQueryPrecise=supported_core.occlusionQueryPrecise;
        core.pipelineStatisticsQuery=supported_core.pipelineStatisticsQuery;core.shaderInt64=VK_TRUE;
        core.shaderFloat64=supported_core.shaderFloat64;core.shaderStorageImageExtendedFormats=supported_core.shaderStorageImageExtendedFormats;
        ci.pNext=nodes.front();report["features"]="emulator RTX3080 feature-chain requests, no profiling/NV diagnostics";
        report["pnext_stypes"]=Json::array();for(const auto* n:nodes)report["pnext_stypes"].push_back(static_cast<int>(n->sType));
        emu_queues[0]=q;emu_queues[0].pQueuePriorities=priorities;ci.pQueueCreateInfos=emu_queues;
        uint32_t transfer=UINT32_MAX;
        for(uint32_t n=0;n<families.size();++n)if(n!=family&&families[n].queueCount&&(families[n].queueFlags&VK_QUEUE_TRANSFER_BIT)&&
            !(families[n].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))){transfer=n;break;}
        if(transfer!=UINT32_MAX){emu_queues[1]=q;emu_queues[1].queueFamilyIndex=transfer;ci.queueCreateInfoCount=2;}
        else if(families[family].queueCount>=2)emu_queues[0].queueCount=2;
        report["queue_families"]=Json::array();for(uint32_t n=0;n<ci.queueCreateInfoCount;++n)
            report["queue_families"].push_back({{"family",emu_queues[n].queueFamilyIndex},{"count",emu_queues[n].queueCount}});
    }
    // Persist the exact inputs even if a third-party layer crashes inside create.
    {std::ofstream f(output);f<<report.dump(2)<<'\n';}
    const auto result=c.Get<PFN_vkCreateDevice>("vkCreateDevice")(physical,&ci,nullptr,&c.device);
    report["vk_create_device_result"]=static_cast<int>(result);
    report["modules_after_create_device"]=Modules();
    if(c.device){auto gdpa=c.Get<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");c.destroy_device=reinterpret_cast<PFN_vkDestroyDevice>(gdpa(c.device,"vkDestroyDevice"));}
    report["passed"]=result==VK_SUCCESS;
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=5){std::wcerr<<L"Usage: device_probe backend.dll runtime_dir config.json output.json\n";return 2;}
    Json report={{"kind","device creation only; no game or GPU submission"},{"passed",false}};
    const std::filesystem::path output(argv[4]);
    try {std::ifstream f{std::filesystem::path(argv[3])};const auto config=Json::parse(f);report["configuration"]=config;
        std::filesystem::create_directories(output.parent_path());Run(argv[1],argv[2],config,output,report);
    } catch(const std::exception& e){report["error"]=e.what();}
    std::ofstream file(output);file<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';return report["passed"].get<bool>()?0:1;
}
