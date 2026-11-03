// Deliberately built with /MD, without NGX includes/import libraries. All Vulkan
// objects and submission belong to this host; the external DLL only records NGX.
#include "kyty_dlss_backend.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::ordered_json;
void CheckVk(VkResult r, const char* op) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(op)+": VkResult "+std::to_string(r));
}
template<class T> T InstanceProc(PFN_vkGetInstanceProcAddr gipa, VkInstance instance, const char* name) {
    auto fn=reinterpret_cast<T>(gipa(instance,name));
    if (!fn) throw std::runtime_error(std::string("Missing ")+name);
    return fn;
}
void Check(KytyDlssStatus status, const KytyDlssError& error, const char* op) {
    if (status != KYTY_DLSS_OK) throw std::runtime_error(std::string(op)+": "+error.message);
}
struct HostContext {
    HMODULE loader {}, backend {};
    PFN_vkGetInstanceProcAddr gipa {};
    PFN_vkDestroyInstance destroy_instance {};
    PFN_vkDestroyDevice destroy_device {};
    PFN_vkDeviceWaitIdle device_wait_idle {};
    VkInstance instance {};
    VkDevice device {};
    KytyDlssApi api {};
    KytyDlssContext* ngx {};
    ~HostContext() {
        if (device && device_wait_idle) device_wait_idle(device); // shutdown only
        if (ngx) { KytyDlssError error {}; api.destroy_context(ngx,1,&error); }
        if (device && destroy_device) destroy_device(device,nullptr);
        if (instance && destroy_instance) destroy_instance(instance,nullptr);
        if (backend) FreeLibrary(backend);
        if (loader) FreeLibrary(loader);
    }
};
#include "smoke_gpu.inl"
#include "validate_gpu_inputs.inl"
#include "validate_gpu_composite.inl"

std::vector<const char*> ExtensionNames(const KytyDlssExtensions& list) {
    std::vector<const char*> result;
    for (uint32_t i=0;i<list.count;++i) result.push_back(list.names[i]);
    return result;
}
void Run(const std::filesystem::path& dll, const std::filesystem::path& runtime,
         const std::filesystem::path& output, Json& report, const PrepareReplay* prepare=nullptr,
         const std::filesystem::path* composite=nullptr) {
    HostContext h;
    h.backend=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h.backend) throw std::runtime_error("Cannot load external backend DLL");
    auto get_api=reinterpret_cast<PFN_KytyDlssGetApi>(GetProcAddress(h.backend,"KytyDlssGetApi"));
    if (!get_api) throw std::runtime_error("Missing backend C ABI entry point");
    h.api.struct_size=sizeof(h.api);
    if (get_api(KYTY_DLSS_ABI_VERSION,&h.api)!=KYTY_DLSS_OK) throw std::runtime_error("C ABI mismatch");
    KytyDlssApi rejected {}; rejected.struct_size=sizeof(rejected);
    if (get_api(999,&rejected)!=KYTY_DLSS_INVALID_ARGUMENT) throw std::runtime_error("ABI version rejection failed");
    const auto logs=(output/L"ngx-logs").wstring(), runtime_w=runtime.wstring();
    KytyDlssPaths paths {sizeof(KytyDlssPaths),runtime_w.c_str(),logs.c_str()};
    KytyDlssError error {};
    KytyDlssExtensions instance_ext {}; instance_ext.struct_size=sizeof(instance_ext);
    Check(h.api.query_instance_extensions(&paths,&instance_ext,&error),error,"Instance extensions");
    h.loader=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h.loader) throw std::runtime_error("Cannot load system Vulkan loader");
    h.gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(h.loader,"vkGetInstanceProcAddr"));
    const auto create_instance=InstanceProc<PFN_vkCreateInstance>(h.gipa,nullptr,"vkCreateInstance");
    auto ie=ExtensionNames(instance_ext);
    VkApplicationInfo app {}; app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName="Kyty external backend reuse-host smoke"; app.apiVersion=VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici {}; ici.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; ici.pApplicationInfo=&app;
    ici.enabledExtensionCount=static_cast<uint32_t>(ie.size()); ici.ppEnabledExtensionNames=ie.data();
    CheckVk(create_instance(&ici,nullptr,&h.instance),"Create host instance");
    h.destroy_instance=InstanceProc<PFN_vkDestroyInstance>(h.gipa,h.instance,"vkDestroyInstance");
    const auto enumerate=InstanceProc<PFN_vkEnumeratePhysicalDevices>(h.gipa,h.instance,"vkEnumeratePhysicalDevices");
    const auto properties=InstanceProc<PFN_vkGetPhysicalDeviceProperties>(h.gipa,h.instance,"vkGetPhysicalDeviceProperties");
    const auto queues=InstanceProc<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(h.gipa,h.instance,"vkGetPhysicalDeviceQueueFamilyProperties");
    const auto features=InstanceProc<PFN_vkGetPhysicalDeviceFeatures2>(h.gipa,h.instance,"vkGetPhysicalDeviceFeatures2");
    uint32_t count {}; CheckVk(enumerate(h.instance,&count,nullptr),"Physical devices");
    std::vector<VkPhysicalDevice> devices(count); CheckVk(enumerate(h.instance,&count,devices.data()),"Physical devices");
    VkPhysicalDevice physical {};
    for (auto p:devices) { VkPhysicalDeviceProperties prop {}; properties(p,&prop);
        if (!physical && prop.vendorID==0x10de) {physical=p;report["gpu"]=prop.deviceName;} }
    if (!physical) throw std::runtime_error("No NVIDIA device");
    KytyDlssExtensions device_ext {}; device_ext.struct_size=sizeof(device_ext);
    Check(h.api.query_device_extensions(&paths,h.instance,physical,&device_ext,&error),error,"Device extensions");
    auto de=ExtensionNames(device_ext);
    report["instance_extensions"]=ie; report["device_extensions"]=de;
    queues(physical,&count,nullptr); std::vector<VkQueueFamilyProperties> qp(count); queues(physical,&count,qp.data());
    uint32_t family=0;
    while (family<count && (!qp[family].queueCount || (qp[family].queueFlags&3)!=3)) ++family;
    if (family==count) throw std::runtime_error("No graphics+compute queue");
    VkPhysicalDeviceVulkan12Features supported12 {}; supported12.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceFeatures2 supported {}; supported.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; supported.pNext=&supported12;
    features(physical,&supported);
    if (!supported12.bufferDeviceAddress || !supported.features.shaderInt64) throw std::runtime_error("Missing required device features");
    VkPhysicalDeviceVulkan12Features enabled12 {}; enabled12.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    enabled12.bufferDeviceAddress=VK_TRUE; VkPhysicalDeviceFeatures enabled {}; enabled.shaderInt64=VK_TRUE;
    if (prepare||composite) {
        if (!supported.features.shaderStorageImageExtendedFormats) throw std::runtime_error("Prepare requires shaderStorageImageExtendedFormats");
        enabled.shaderStorageImageExtendedFormats=VK_TRUE;
    }
    const float priority=1;
    VkDeviceQueueCreateInfo qci {}; qci.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;
    VkDeviceCreateInfo dci {}; dci.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; dci.pNext=&enabled12;dci.pEnabledFeatures=&enabled;
    dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;dci.enabledExtensionCount=static_cast<uint32_t>(de.size());dci.ppEnabledExtensionNames=de.data();
    auto create_device=InstanceProc<PFN_vkCreateDevice>(h.gipa,h.instance,"vkCreateDevice");
    CheckVk(create_device(physical,&dci,nullptr,&h.device),"Create host device");
    auto gdpa=InstanceProc<PFN_vkGetDeviceProcAddr>(h.gipa,h.instance,"vkGetDeviceProcAddr");
    h.destroy_device=reinterpret_cast<PFN_vkDestroyDevice>(gdpa(h.device,"vkDestroyDevice"));
    h.device_wait_idle=reinterpret_cast<PFN_vkDeviceWaitIdle>(gdpa(h.device,"vkDeviceWaitIdle"));
    if(composite) {RunCompositeValidation(h,physical,family,*composite,output,report);return;}
    const uint32_t iw=prepare?prepare->sequence.at("width").get<uint32_t>():320;
    const uint32_t ih=prepare?prepare->sequence.at("height").get<uint32_t>():180;
    const uint32_t ow=prepare?prepare->sequence.at("output_width").get<uint32_t>():640;
    const uint32_t oh=prepare?prepare->sequence.at("output_height").get<uint32_t>():360;
    KytyDlssCreateInfo ci {};ci.struct_size=sizeof(ci);ci.paths=paths;
    ci.instance=h.instance;ci.physical_device=physical;ci.device=h.device;
    ci.get_instance_proc_addr=h.gipa;ci.get_device_proc_addr=gdpa;ci.queue_family_index=family;
    ci.enabled_feature_flags=3;ci.enabled_device_extension_count=static_cast<uint32_t>(de.size());ci.enabled_device_extensions=de.data();
    ci.input_width=iw;ci.input_height=ih;ci.output_width=ow;ci.output_height=oh;ci.quality=KYTY_DLSS_PERFORMANCE;
    auto bad_ci=ci;bad_ci.enabled_device_extension_count=0;KytyDlssContext* rejected_ctx {};
    if (h.api.create_context(&bad_ci,&rejected_ctx,&error)!=KYTY_DLSS_UNSUPPORTED || rejected_ctx)
        throw std::runtime_error("Missing enabled extension rejection failed");
    Check(h.api.create_context(&ci,&h.ngx,&error),error,"Create context on host device");
    KytyDlssOptimalSettings optimal {};optimal.struct_size=sizeof(optimal);
    Check(h.api.query_optimal(h.ngx,&optimal,&error),error,"Optimal settings");
    report["recommended_input"]={optimal.optimal_width,optimal.optimal_height};
    if (h.api.destroy_context(h.ngx,0,&error)!=KYTY_DLSS_INVALID_ARGUMENT) throw std::runtime_error("Unsafe destroy was not rejected");
    if (prepare) {
        RunPrepareReplay(h,physical,family,*prepare,output,report);
        Check(h.api.destroy_context(h.ngx,1,&error),error,"Destroy after host replay fence completion");h.ngx=nullptr;
        return;
    }
    SmokeGpu gpu(h,physical);gpu.GetDeviceQueue(h.device,family,0,&gpu.queue);
    VkCommandPoolCreateInfo pci {}; pci.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pci.queueFamilyIndex=family;
    CheckVk(gpu.CreateCommandPool(h.device,&pci,nullptr,&gpu.pool),"Host command pool");
    VkCommandBufferAllocateInfo cai {}; cai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool=gpu.pool;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;
    CheckVk(gpu.AllocateCommandBuffers(h.device,&cai,&gpu.command),"Host command buffer");
    VkFenceCreateInfo fci {}; fci.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;CheckVk(gpu.CreateFence(h.device,&fci,nullptr,&gpu.fence),"Host fence");
    gpu.MakeImage(gpu.images[0],iw,ih,VK_FORMAT_R16G16B16A16_SFLOAT,false);
    gpu.MakeImage(gpu.images[1],iw,ih,VK_FORMAT_R32_SFLOAT,false);
    gpu.MakeImage(gpu.images[2],iw,ih,VK_FORMAT_R32G32_SFLOAT,false);
    gpu.MakeImage(gpu.images[3],ow,oh,VK_FORMAT_R16G16B16A16_SFLOAT,true);
    const size_t pixels=size_t(iw)*ih;
    const std::array<VkDeviceSize,3> offsets {0,pixels*8,pixels*12};
    gpu.MakeBuffer(gpu.buffers[0],pixels*20,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    gpu.MakeBuffer(gpu.buffers[1],size_t(ow)*oh*8,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    const std::array<std::array<float,2>,4> jitter {{{.125f,-.125f},{-.125f,.375f},{.375f,.125f},{-.375f,-.375f}}};
    report["frames"]=Json::array();
    for (unsigned frame=0;frame<4;++frame) {
        if (frame) {CheckVk(gpu.ResetFences(h.device,1,&gpu.fence),"Reset host fence");CheckVk(gpu.ResetCommandPool(h.device,gpu.pool,0),"Reset completed host command pool");}
        void* mapped {};CheckVk(gpu.MapMemory(h.device,gpu.buffers[0].memory,0,VK_WHOLE_SIZE,0,&mapped),"Map upload");
        auto* color=static_cast<uint16_t*>(mapped);
        auto* depth=reinterpret_cast<float*>(static_cast<std::byte*>(mapped)+offsets[1]);
        auto* motion=reinterpret_cast<float*>(static_cast<std::byte*>(mapped)+offsets[2]);
        for (uint32_t y=0;y<ih;++y) for(uint32_t x=0;x<iw;++x) {
            const size_t p=size_t(y)*iw+x;
            // A static smooth scene sampled at positions displaced by the actual
            // projection jitter. Clean motion is exactly zero for this scene.
            const float u=(float(x)+.5f-jitter[frame][0])/iw;
            const float v=(float(y)+.5f-jitter[frame][1])/ih;
            color[p*4]=TestHalf(.1f+.6f*u);color[p*4+1]=TestHalf(.1f+.6f*v);
            color[p*4+2]=TestHalf(.3f+.15f*std::sin(u*30)*std::cos(v*20));color[p*4+3]=TestHalf(1);
            depth[p]=.5f;motion[p*2]=0;motion[p*2+1]=0;
        }
        gpu.UnmapMemory(h.device,gpu.buffers[0].memory);
        VkCommandBufferBeginInfo begin {}; begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CheckVk(gpu.BeginCommandBuffer(gpu.command,&begin),"Begin host command buffer");
        for(unsigned n=0;n<3;++n) {
            gpu.Transition(gpu.images[n],frame?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,frame?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
            const auto region=SmokeGpu::CopyRegion(gpu.images[n],offsets[n]);
            gpu.CmdCopyBufferToImage(gpu.command,gpu.buffers[0].buffer,gpu.images[n].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
            gpu.Transition(gpu.images[n],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
        }
        gpu.Transition(gpu.images[3],frame?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            frame?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkClearColorValue nan {};for(auto& v:nan.float32)v=std::numeric_limits<float>::quiet_NaN();
        const VkImageSubresourceRange range {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        gpu.CmdClearColorImage(gpu.command,gpu.images[3].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&nan,1,&range);
        gpu.Transition(gpu.images[3],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT);
        KytyDlssEvaluateInfo eval {};eval.struct_size=sizeof(eval);eval.command_buffer=gpu.command;
        eval.color=SmokeGpu::Resource(gpu.images[0]);eval.depth=SmokeGpu::Resource(gpu.images[1]);
        eval.motion=SmokeGpu::Resource(gpu.images[2]);eval.output=SmokeGpu::Resource(gpu.images[3]);
        eval.jitter_x=jitter[frame][0];eval.jitter_y=jitter[frame][1];eval.frame_time_ms=16.6667f;eval.pre_exposure=1;eval.exposure_scale=1;
        KytyDlssRecordResult result {};result.struct_size=sizeof(result);
        if (!frame) {
            auto invalid=eval;invalid.motion.format=VK_FORMAT_R16G16_SFLOAT;
            if(h.api.record_evaluate(h.ngx,&invalid,&result,&error)!=KYTY_DLSS_INVALID_ARGUMENT)
                throw std::runtime_error("Wrong motion format rejection failed");
        }
        Check(h.api.record_evaluate(h.ngx,&eval,&result,&error),error,"Record into host command buffer");
        if(result.feature_created!=(frame==0) || result.reset_used!=(frame==0) || result.evaluations_recorded!=frame+1)
            throw std::runtime_error("Persistent feature/reset contract failed");
        gpu.Transition(gpu.images[3],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        const auto region=SmokeGpu::CopyRegion(gpu.images[3]);
        gpu.CmdCopyImageToBuffer(gpu.command,gpu.images[3].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,gpu.buffers[1].buffer,1,&region);
        VkMemoryBarrier barrier {}; barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        gpu.CmdPipelineBarrier(gpu.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        CheckVk(gpu.EndCommandBuffer(gpu.command),"End host command buffer");
        VkSubmitInfo submit {}; submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&gpu.command;
        CheckVk(gpu.QueueSubmit(gpu.queue,1,&submit,gpu.fence),"Submit by host");
        CheckVk(gpu.WaitForFences(h.device,1,&gpu.fence,VK_TRUE,30'000'000'000ull),"Host fence wait for diagnostic readback");
        CheckVk(gpu.MapMemory(h.device,gpu.buffers[1].memory,0,VK_WHOLE_SIZE,0,&mapped),"Map output");
        std::vector<uint16_t> rgba(size_t(ow)*oh*4);std::memcpy(rgba.data(),mapped,rgba.size()*2);gpu.UnmapMemory(h.device,gpu.buffers[1].memory);
        size_t finite=0,nonzero=0;float low=std::numeric_limits<float>::infinity(),high=-low;double sum=0;
        for(size_t n=0;n<rgba.size();++n) if(n%4!=3) {float v=ReadHalf(rgba[n]);if(std::isfinite(v)){++finite;if(v!=0)++nonzero;low=std::min(low,v);high=std::max(high,v);sum+=v;}}
        if(finite!=size_t(ow)*oh*3 || !nonzero || high-low<.01f)throw std::runtime_error("Output has unwritten/nonfinite/empty/constant RGB");
        report["frames"].push_back({{"frame",frame},{"feature_created",result.feature_created},{"reset",result.reset_used},
            {"finite_rgb",finite},{"nonzero_rgb",nonzero},{"min",low},{"max",high},{"mean",sum/double(finite)}});
        const auto raw_path=output/("smoke-"+std::to_string(frame)+".rgba16f");
        std::ofstream raw(raw_path,std::ios::binary);raw.exceptions(std::ios::failbit|std::ios::badbit);
        raw.write(reinterpret_cast<const char*>(rgba.data()),static_cast<std::streamsize>(rgba.size()*2));
    }
    Check(h.api.destroy_context(h.ngx,1,&error),error,"Destroy after host fence completion");h.ngx=nullptr;
    report["input"]={iw,ih};report["output"]={ow,oh};report["passed"]=true;
}
} // namespace

int wmain(int argc,wchar_t** argv) {
    if(argc!=4 && !(argc==7 && std::wstring(argv[4])==L"--prepare") && !(argc==6 && std::wstring(argv[4])==L"--composite")) {
        std::wcerr<<L"Usage: kyty_dlss_backend_smoke <absolute backend DLL> <absolute runtime directory> <absolute output directory> [--prepare <absolute sequence.json> <absolute prepare.spv> | --composite <absolute composite.spv>]\n";return 2;
    }
    Json report={{"schema",1},{"kind","external C ABI reuse-host Vulkan synthetic smoke"},{"passed",false},
        {"gt7_data_used",false},{"in_game_integration_tested",false},{"validation_layer_enabled",false},
        {"backend_submits_or_waits",false},{"host_readback_waits_on_fences",true},
        {"host_crt","MD"},{"backend_crt","MT"},{"requested_preset","K"},{"effective_preset_verified",false}};
    const std::filesystem::path dll(argv[1]),runtime(argv[2]),output(argv[3]);
    try {
        if(!dll.is_absolute()||!runtime.is_absolute()||!output.is_absolute())throw std::runtime_error("All paths must be absolute");
        PrepareReplay prepare;std::filesystem::path composite;
        if(argc==7) {
            prepare.sequence_path=argv[5];prepare.spirv=argv[6];
            if(!prepare.sequence_path.is_absolute()||!prepare.spirv.is_absolute())throw std::runtime_error("Prepare paths must be absolute");
            prepare.sequence=ReadJson(prepare.sequence_path);
            const auto& seq=prepare.sequence;
            if(seq.at("schema")!="gt7.ngx-replay.v2"||seq.at("quality")!="performance"||seq.at("width")!=1920||seq.at("height")!=1080||
               seq.at("output_width")!=3840||seq.at("output_height")!=2160||seq.at("frames").size()!=4)
                throw std::runtime_error("Prepare mode requires the verified four-frame 1080p to 4K Performance capture sequence");
            prepare.constants=ReadJson(Utf8Path(seq.at("constants_source").get<std::string>()));
        }
        if(argc==6) {composite=argv[5];if(!composite.is_absolute())throw std::runtime_error("Composite path must be absolute");}
        std::filesystem::create_directories(output);Run(dll,runtime,output,report,argc==7?&prepare:nullptr,argc==6?&composite:nullptr);
    }catch(const std::exception& e){report["error"]=e.what();}
    try {std::ofstream file(output/L"smoke-report.json");file.exceptions(std::ios::failbit|std::ios::badbit);file<<report.dump(2)<<'\n';}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    std::cout<<report.dump(2)<<'\n';return report["passed"].get<bool>()?0:1;
}
