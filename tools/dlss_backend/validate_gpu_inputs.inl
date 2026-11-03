// Included by the SDK-free host smoke. Runs the renderer's prepare SPIR-V on
// captured raw texels and checks its outputs against the independent CPU converter.
struct PrepareReplay {
    Json sequence, constants;
    std::filesystem::path sequence_path, spirv;
};
std::filesystem::path Utf8Path(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(),s.end()));
}
std::vector<std::byte> ReadBytes(const std::filesystem::path& p, size_t bytes) {
    if(std::filesystem::file_size(p)!=bytes)throw std::runtime_error("Payload size mismatch: "+p.filename().string());
    std::vector<std::byte> result(bytes);std::ifstream f(p,std::ios::binary);f.exceptions(std::ios::failbit|std::ios::badbit);
    f.read(reinterpret_cast<char*>(result.data()),static_cast<std::streamsize>(bytes));return result;
}
Json ReadJson(const std::filesystem::path& p) {
    std::ifstream f(p);f.exceptions(std::ios::failbit|std::ios::badbit);return Json::parse(f);
}
struct PrepareGpu:SmokeGpu {
    VkDescriptorSetLayout set_layout {};
    VkPipelineLayout pipeline_layout {};
    VkPipeline pipeline {};
    VkShaderModule module {};
    VkDescriptorPool descriptors {};
    VkDescriptorSet set {};
    VkSampler sampler {};
    uint32_t local_x=0,local_y=0;
#define PREPARE_FUNCTIONS(X) \
    X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreateComputePipelines) X(DestroyPipeline) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CreateSampler) X(DestroySampler) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch)
#define DECLARE(name) PFN_vk##name name {};
    PREPARE_FUNCTIONS(DECLARE)
#undef DECLARE
    explicit PrepareGpu(HostContext& h,VkPhysicalDevice p):SmokeGpu(h,p) {
        auto gdpa=InstanceProc<PFN_vkGetDeviceProcAddr>(h.gipa,h.instance,"vkGetDeviceProcAddr");
#define LOAD(name) name=reinterpret_cast<PFN_vk##name>(gdpa(device,"vk" #name));if(!name)throw std::runtime_error("Missing vk" #name);
        PREPARE_FUNCTIONS(LOAD)
#undef LOAD
#undef PREPARE_FUNCTIONS
    }
    ~PrepareGpu() {
        if(queue)QueueWaitIdle(queue); // host exceptional-path/teardown only
        if(pipeline)DestroyPipeline(device,pipeline,nullptr);
        if(module)DestroyShaderModule(device,module,nullptr);
        if(descriptors)DestroyDescriptorPool(device,descriptors,nullptr);
        if(sampler)DestroySampler(device,sampler,nullptr);
        if(pipeline_layout)DestroyPipelineLayout(device,pipeline_layout,nullptr);
        if(set_layout)DestroyDescriptorSetLayout(device,set_layout,nullptr);
    }
    void MakePreparePipeline(const std::filesystem::path& spv,bool composite=false) {
        const auto bytes=ReadBytes(spv,static_cast<size_t>(std::filesystem::file_size(spv)));
        if(bytes.size()<20 || bytes.size()%4)throw std::runtime_error("Invalid prepare SPIR-V size");
        std::vector<uint32_t> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());
        if(words[0]!=0x07230203)throw std::runtime_error("Invalid prepare SPIR-V magic");
        for(size_t n=5;n<words.size();) {
            const auto count=words[n]>>16,op=words[n]&65535;
            if(!count || n+count>words.size())throw std::runtime_error("Invalid SPIR-V instruction");
            if(op==16 && count==6 && words[n+2]==17) {local_x=words[n+3];local_y=words[n+4];}
            n+=count;
        }
        if(!local_x||!local_y||local_x>1024||local_y>1024)throw std::runtime_error("Expected explicit prepare local size");
        const uint32_t sampled=composite?4:3,total=composite?5:7;
        std::array<VkDescriptorSetLayoutBinding,7> bindings {};
        for(uint32_t n=0;n<total;++n) {bindings[n].binding=n;bindings[n].descriptorCount=1;
            bindings[n].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;bindings[n].descriptorType=n<sampled?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;}
        VkDescriptorSetLayoutCreateInfo sl {};sl.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        sl.bindingCount=total;sl.pBindings=bindings.data();CheckVk(CreateDescriptorSetLayout(device,&sl,nullptr,&set_layout),"Prepare descriptor layout");
        const VkPushConstantRange range {VK_SHADER_STAGE_COMPUTE_BIT,0,16};
        VkPipelineLayoutCreateInfo pl {};pl.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount=1;pl.pSetLayouts=&set_layout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&range;
        CheckVk(CreatePipelineLayout(device,&pl,nullptr,&pipeline_layout),"Prepare pipeline layout");
        VkShaderModuleCreateInfo sm {};sm.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;sm.codeSize=bytes.size();sm.pCode=words.data();
        CheckVk(CreateShaderModule(device,&sm,nullptr,&module),"Prepare shader module");
        VkComputePipelineCreateInfo cp {};cp.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;cp.layout=pipeline_layout;
        cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=module;cp.stage.pName="main";
        CheckVk(CreateComputePipelines(device,VK_NULL_HANDLE,1,&cp,nullptr,&pipeline),"Prepare compute pipeline");
        const VkDescriptorPoolSize sizes[] {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,sampled},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,total-sampled}};
        VkDescriptorPoolCreateInfo dp {};dp.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;dp.maxSets=1;dp.poolSizeCount=2;dp.pPoolSizes=sizes;
        CheckVk(CreateDescriptorPool(device,&dp,nullptr,&descriptors),"Prepare descriptor pool");
        VkDescriptorSetAllocateInfo da {};da.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;da.descriptorPool=descriptors;da.descriptorSetCount=1;da.pSetLayouts=&set_layout;
        CheckVk(AllocateDescriptorSets(device,&da,&set),"Prepare descriptor set");
        VkSamplerCreateInfo si {};si.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;si.magFilter=si.minFilter=composite?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
        si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        CheckVk(CreateSampler(device,&si,nullptr,&sampler),"Prepare sampler");
        // Raw color/depth/MV followed by converted color/depth/MV and invalid mask.
        const std::array<unsigned,7> image_indices=composite?std::array<unsigned,7>{0,1,2,3,4,0,0}:std::array<unsigned,7>{6,4,5,0,1,2,7};
        std::array<VkDescriptorImageInfo,7> info {};std::array<VkWriteDescriptorSet,7> writes {};
        for(uint32_t n=0;n<total;++n) {
            info[n]={n<sampled?sampler:VK_NULL_HANDLE,images[image_indices[n]].view,n<sampled?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL};
            writes[n].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[n].dstSet=set;writes[n].dstBinding=n;
            writes[n].descriptorCount=1;writes[n].descriptorType=bindings[n].descriptorType;writes[n].pImageInfo=&info[n];
        }
        UpdateDescriptorSets(device,total,writes.data(),0,nullptr);
    }
    void Begin() {
        VkCommandBufferBeginInfo begin {};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CheckVk(BeginCommandBuffer(command,&begin),"Begin validation command buffer");
    }
    void SubmitAndReadbackWait() {
        CheckVk(EndCommandBuffer(command),"End validation command buffer");
        VkSubmitInfo submit {};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        CheckVk(QueueSubmit(queue,1,&submit,fence),"Host validation submit");
        CheckVk(WaitForFences(device,1,&fence,VK_TRUE,30'000'000'000ull),"Host validation fence readback");
    }
    void RecycleCompleted() {
        CheckVk(ResetFences(device,1,&fence),"Reset validation fence");CheckVk(ResetCommandPool(device,pool,0),"Reset completed validation pool");
    }
    void HostReadBarrier() {
        VkMemoryBarrier b {};b.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&b,0,nullptr,0,nullptr);
    }
    std::vector<std::byte> Readback(unsigned index) {
        void* p {};CheckVk(MapMemory(device,buffers[index].memory,0,VK_WHOLE_SIZE,0,&p),"Map validation output");
        std::vector<std::byte> bytes(static_cast<size_t>(buffers[index].size));std::memcpy(bytes.data(),p,bytes.size());
        UnmapMemory(device,buffers[index].memory);return bytes;
    }
};

void RunPrepareReplay(HostContext& h,VkPhysicalDevice physical,uint32_t family,const PrepareReplay& input,
                      const std::filesystem::path& output,Json& report) {
    const auto& seq=input.sequence;
    const uint32_t w=seq.at("width"),height=seq.at("height"),ow=seq.at("output_width"),oh=seq.at("output_height");
    const size_t pixels=size_t(w)*height;
    PrepareGpu gpu(h,physical);gpu.GetDeviceQueue(h.device,family,0,&gpu.queue);
    VkCommandPoolCreateInfo pc {};pc.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pc.queueFamilyIndex=family;
    CheckVk(gpu.CreateCommandPool(h.device,&pc,nullptr,&gpu.pool),"Validation command pool");
    VkCommandBufferAllocateInfo ca {};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandPool=gpu.pool;
    ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;CheckVk(gpu.AllocateCommandBuffers(h.device,&ca,&gpu.command),"Validation command buffer");
    VkFenceCreateInfo fc {};fc.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;CheckVk(gpu.CreateFence(h.device,&fc,nullptr,&gpu.fence),"Validation fence");
    gpu.MakeImage(gpu.images[0],w,height,VK_FORMAT_R16G16B16A16_SFLOAT,true);
    gpu.MakeImage(gpu.images[1],w,height,VK_FORMAT_R32_SFLOAT,true);
    gpu.MakeImage(gpu.images[2],w,height,VK_FORMAT_R32G32_SFLOAT,true);
    gpu.MakeImage(gpu.images[3],ow,oh,VK_FORMAT_R16G16B16A16_SFLOAT,true);
    gpu.MakeImage(gpu.images[4],w,height,VK_FORMAT_R32_SFLOAT,false);
    gpu.MakeImage(gpu.images[5],w,height,VK_FORMAT_R16G16_SFLOAT,false);
    gpu.MakeImage(gpu.images[6],w,height,VK_FORMAT_B10G11R11_UFLOAT_PACK32,false);
    gpu.MakeImage(gpu.images[7],w,height,VK_FORMAT_R8_UNORM,true);
    gpu.MakeBuffer(gpu.buffers[0],pixels*12,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    gpu.MakeBuffer(gpu.buffers[1],pixels*21,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    gpu.MakeBuffer(gpu.buffers[2],size_t(ow)*oh*8,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    gpu.MakePreparePipeline(input.spirv);
    const auto spv_utf8=input.spirv.u8string();
    report["prepare_spirv"]=std::string(spv_utf8.begin(),spv_utf8.end());report["local_size"]={gpu.local_x,gpu.local_y};
    report["frames"]=Json::array();report["gt7_data_used"]=true;
    report["kind"]="real capture GPU input conversion + external backend host-device replay";
    report["raw_depth_note"]="Captured D32 aspect bytes uploaded as R32F; tests numeric conversion, not original guest depth view lifetime";
    report["validation_policy"]="Every color/depth/MV/mask element must match the independent CPU converter bitwise before NGX submission";
    report["invalid_motion_policy"]="Zero motion remains an untrusted placeholder; mask is validated separately and is not supplied to preset K; output has no invalid-region fallback composite";
    report["shader_storage_image_extended_formats_enabled"]=true;
    const std::array<size_t,4> offset {0,pixels*8,pixels*12,pixels*20};
    const std::array<size_t,4> sizes {pixels*8,pixels*4,pixels*8,pixels};
    const std::array<const char*,4> keys {"color","depth","motion","bias"};
    bool first=true;unsigned expected_capture=1;
    for(const auto& frame:seq.at("frames")) {
        if(!first)gpu.RecycleCompleted();
        const unsigned capture=frame.at("capture");
        if(capture!=expected_capture++)throw std::runtime_error("Expected consecutive captures 1 through 4");
        const auto constants=std::find_if(input.constants.at("frames").begin(),input.constants.at("frames").end(),
            [capture](const Json& c){return c.at("capture").get<unsigned>()==capture;});
        if(constants==input.constants.at("frames").end()||constants->at("op")!=frame.at("source_op"))throw std::runtime_error("Capture/op constants mismatch");
        const auto& active=frame.at("input_active_rectangle");
        if(active.at("x")!=0||active.at("y")!=0||active.at("width")!=w||active.at("height")!=height)
            throw std::runtime_error("Prepare requires complete origin-zero active images");
        for(unsigned n=0;n<2;++n) {
            const float j=constants->at("jitter_ndc").at(n).get<float>();
            const float pixel=j*(n==0?-.5f*float(w):.5f*float(height));
            if(!std::isfinite(j)||std::abs(pixel-frame.at("jitter").at(n).get<float>())>1e-6f)
                throw std::runtime_error("Projection jitter does not match source constants");
        }
        std::vector<std::byte> raw(pixels*12);
        for(unsigned n=0;n<3;++n) {
            const auto& source=frame.at("sources").at(std::to_string(n));
            const auto meta=ReadJson(Utf8Path(source.at("metadata").get<std::string>()));
            const unsigned formats[] {130,83,122};
            if(meta.at("host").at("width")!=w||meta.at("host").at("height")!=height||!meta.at("complete").get<bool>()||
               meta.at("host").at("vk_format")!=formats[n]||meta.at("capture")!=capture||meta.at("operation")!=frame.at("source_op")||
               !meta.at("selection").at("complete_selected_mip_aspect").get<bool>())throw std::runtime_error("Raw source is not a matching complete input image");
            const auto b=ReadBytes(Utf8Path(source.at("binary").get<std::string>()),pixels*4);
            std::memcpy(raw.data()+pixels*4*n,b.data(),b.size());
        }
        void* mapped {};CheckVk(gpu.MapMemory(h.device,gpu.buffers[0].memory,0,VK_WHOLE_SIZE,0,&mapped),"Map raw upload");
        std::memcpy(mapped,raw.data(),raw.size());gpu.UnmapMemory(h.device,gpu.buffers[0].memory);
        gpu.Begin();
        for(unsigned n=0;n<3;++n) {
            auto& image=gpu.images[4+n];
            gpu.Transition(image,first?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                first?0:VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
            auto region=PrepareGpu::CopyRegion(image,pixels*4*n);
            gpu.CmdCopyBufferToImage(gpu.command,gpu.buffers[0].buffer,image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
            gpu.Transition(image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
        }
        for(unsigned n:{0u,1u,2u,7u}) {
            gpu.Transition(gpu.images[n],first?VK_IMAGE_LAYOUT_UNDEFINED:(n==7?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,first?0:VK_ACCESS_MEMORY_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
            VkClearColorValue clear {};for(auto& v:clear.float32)v=n==7?.5f:std::numeric_limits<float>::quiet_NaN();
            const VkImageSubresourceRange range {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            gpu.CmdClearColorImage(gpu.command,gpu.images[n].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
            gpu.Transition(gpu.images[n],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT);
        }
        struct Push {uint32_t width,height;float jitter_x,jitter_y;};
        const Push push {w,height,constants->at("jitter_ndc").at(0).get<float>(),constants->at("jitter_ndc").at(1).get<float>()};
        gpu.CmdBindPipeline(gpu.command,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipeline);
        gpu.CmdBindDescriptorSets(gpu.command,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipeline_layout,0,1,&gpu.set,0,nullptr);
        gpu.CmdPushConstants(gpu.command,gpu.pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);
        gpu.CmdDispatch(gpu.command,(w+gpu.local_x-1)/gpu.local_x,(height+gpu.local_y-1)/gpu.local_y,1);
        for(unsigned n=0;n<4;++n) {
            auto& image=gpu.images[n==3?7:n];
            gpu.Transition(image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
            auto region=PrepareGpu::CopyRegion(image,offset[n]);
            gpu.CmdCopyImageToBuffer(gpu.command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,gpu.buffers[1].buffer,1,&region);
            if(n<3)gpu.Transition(image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
        }
        gpu.HostReadBarrier();gpu.SubmitAndReadbackWait();
        const auto computed=gpu.Readback(1);
        Json fr={{"capture",capture},{"source_op",frame.at("source_op")},{"prepare",Json::object()}};
        bool matched=true;
        for(unsigned n=0;n<4;++n) {
            const auto reference=ReadBytes(input.sequence_path.parent_path()/Utf8Path(frame.at(keys[n]).get<std::string>()),sizes[n]);
            const size_t element=n==0?2:(n==3?1:4);size_t mismatch=0;double max_abs=0;
            Json first_mismatch=nullptr;
            for(size_t j=0;j<sizes[n];j+=element) {
                if(std::memcmp(reference.data()+j,computed.data()+offset[n]+j,element)) {
                    ++mismatch;
                    if(first_mismatch.is_null()) {
                        uint32_t expected=0,actual=0;
                        std::memcpy(&expected,reference.data()+j,element);std::memcpy(&actual,computed.data()+offset[n]+j,element);
                        first_mismatch={{"element",j/element},{"expected_bits",expected},{"gpu_bits",actual}};
                    }
                }
                if(n==2) {float a,b;std::memcpy(&a,reference.data()+j,4);std::memcpy(&b,computed.data()+offset[n]+j,4);
                    max_abs=std::max(max_abs,std::isfinite(b)?double(std::abs(a-b)):std::numeric_limits<double>::infinity());}
            }
            fr["prepare"][keys[n]]={{"elements",sizes[n]/element},{"bit_mismatches",mismatch}};
            if(!first_mismatch.is_null())fr["prepare"][keys[n]]["first_mismatch"]=first_mismatch;
            if(n==2)fr["prepare"][keys[n]]["max_absolute_difference"]=max_abs;
            matched &= mismatch==0;
        }
        report["frames"].push_back(fr);
        if(!matched)throw std::runtime_error("GPU prepare differs from independent CPU converter; NGX not submitted for this frame");
        gpu.RecycleCompleted();gpu.Begin();
        gpu.Transition(gpu.images[3],first?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            first?0:VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkClearColorValue clear {};for(auto& v:clear.float32)v=std::numeric_limits<float>::quiet_NaN();
        const VkImageSubresourceRange range {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        gpu.CmdClearColorImage(gpu.command,gpu.images[3].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
        gpu.Transition(gpu.images[3],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT);
        KytyDlssEvaluateInfo eval {};eval.struct_size=sizeof(eval);eval.command_buffer=gpu.command;
        eval.color=PrepareGpu::Resource(gpu.images[0]);eval.depth=PrepareGpu::Resource(gpu.images[1]);
        eval.motion=PrepareGpu::Resource(gpu.images[2]);eval.output=PrepareGpu::Resource(gpu.images[3]);
        eval.jitter_x=frame.at("jitter").at(0);eval.jitter_y=frame.at("jitter").at(1);
        eval.frame_time_ms=frame.at("frame_time_ms");eval.pre_exposure=1;eval.exposure_scale=1;
        KytyDlssRecordResult result {};result.struct_size=sizeof(result);KytyDlssError error {};
        Check(h.api.record_evaluate(h.ngx,&eval,&result,&error),error,"NGX on GPU-prepared real inputs");
        if(result.feature_created!=first||result.reset_used!=first||result.evaluations_recorded!=capture)throw std::runtime_error("Replay feature/reset contract failed");
        gpu.Transition(gpu.images[3],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        auto region=PrepareGpu::CopyRegion(gpu.images[3]);
        gpu.CmdCopyImageToBuffer(gpu.command,gpu.images[3].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,gpu.buffers[2].buffer,1,&region);
        gpu.HostReadBarrier();gpu.SubmitAndReadbackWait();const auto rendered=gpu.Readback(2);
        size_t finite=0,nonzero=0;float low=std::numeric_limits<float>::infinity(),high=-low;
        for(size_t p=0;p<size_t(ow)*oh;++p)for(unsigned c=0;c<3;++c) {
            uint16_t half;std::memcpy(&half,rendered.data()+p*8+c*2,2);const float v=ReadHalf(half);
            if(std::isfinite(v)){++finite;if(v!=0)++nonzero;low=std::min(low,v);high=std::max(high,v);}
        }
        report["frames"].back()["ngx"]={{"feature_created",result.feature_created},{"reset",result.reset_used},
            {"finite_rgb",finite},{"rgb_components",size_t(ow)*oh*3},{"nonzero_rgb",nonzero},{"min",low},{"max",high}};
        if(finite!=size_t(ow)*oh*3||!nonzero||high-low<.001f)throw std::runtime_error("Invalid NGX replay output");
        std::ofstream file(output/("capture-"+std::to_string(capture)+".gpu-prepare-ngx.rgba16f"),std::ios::binary);
        file.exceptions(std::ios::failbit|std::ios::badbit);file.write(reinterpret_cast<const char*>(rendered.data()),static_cast<std::streamsize>(rendered.size()));
        first=false;
    }
    report["input"]={w,height};report["output"]={ow,oh};report["passed"]=true;
}
