// Deterministic synthetic-effect test of the renderer's actual composite SPIR-V.
// Reference sampling is double-precision CPU bilinear/clamp, independent of GPU.
uint16_t ReferenceHalf(double value) {
    if(!std::isfinite(value)||std::abs(value)>65504)throw std::runtime_error("Reference expects finite half range");
    if(value<0)return static_cast<uint16_t>(ReferenceHalf(-value)|0x8000);
    uint32_t lo=0,hi=0x7bff;
    while(lo<hi) {const auto mid=(lo+hi)/2;if(double(ReadHalf(static_cast<uint16_t>(mid)))<value)lo=mid+1;else hi=mid;}
    if(!lo)return 0;
    const double above=double(ReadHalf(static_cast<uint16_t>(lo)))-value;
    const double below=value-double(ReadHalf(static_cast<uint16_t>(lo-1)));
    return static_cast<uint16_t>(below<above||(below==above&&((lo-1)&1)==0)?lo-1:lo);
}
void RunCompositeValidation(HostContext& h,VkPhysicalDevice physical,uint32_t family,
                            const std::filesystem::path& spirv,const std::filesystem::path& output,Json& report) {
    // Odd low extents and non-multiple-of-eight output exercise all dispatch borders.
    constexpr uint32_t lw=17,lh=11,w=34,height=22;
    constexpr size_t low_pixels=size_t(lw)*lh,pixels=size_t(w)*height;
    PrepareGpu gpu(h,physical);gpu.GetDeviceQueue(h.device,family,0,&gpu.queue);
    VkCommandPoolCreateInfo pc {};pc.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pc.queueFamilyIndex=family;
    CheckVk(gpu.CreateCommandPool(h.device,&pc,nullptr,&gpu.pool),"Composite test command pool");
    VkCommandBufferAllocateInfo ca {};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandPool=gpu.pool;
    ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;CheckVk(gpu.AllocateCommandBuffers(h.device,&ca,&gpu.command),"Composite test command buffer");
    VkFenceCreateInfo fc {};fc.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;CheckVk(gpu.CreateFence(h.device,&fc,nullptr,&gpu.fence),"Composite test fence");
    gpu.MakeImage(gpu.images[0],w,height,VK_FORMAT_R16G16B16A16_SFLOAT,false);
    gpu.MakeImage(gpu.images[1],lw,lh,VK_FORMAT_R16G16B16A16_SFLOAT,false);
    gpu.MakeImage(gpu.images[2],lw,lh,VK_FORMAT_R16G16B16A16_SFLOAT,false);
    gpu.MakeImage(gpu.images[3],lw,lh,VK_FORMAT_R8_UNORM,false);
    gpu.MakeImage(gpu.images[4],w,height,VK_FORMAT_R16G16B16A16_SFLOAT,true);
    const std::array<size_t,4> offsets {0,pixels*8,pixels*8+low_pixels*8,pixels*8+low_pixels*16};
    gpu.MakeBuffer(gpu.buffers[0],offsets[3]+low_pixels,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    gpu.MakeBuffer(gpu.buffers[1],pixels*8,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    gpu.MakePreparePipeline(spirv,true);
    std::vector<uint16_t> base(pixels*4),effect(low_pixels*4),native(low_pixels*4);
    std::vector<uint8_t> mask(low_pixels);
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<w;++x) {
        const size_t p=(size_t(y)*w+x)*4;
        base[p]=ReferenceHalf(1+double(x%11)/16);base[p+1]=ReferenceHalf(2+double(y%7)/8);
        base[p+2]=ReferenceHalf(.25+double((x+y)%13)/32);base[p+3]=ReferenceHalf(1);
    }
    for(uint32_t y=0;y<lh;++y)for(uint32_t x=0;x<lw;++x) {
        const size_t p=(size_t(y)*lw+x)*4;
        const double a=x<4?0:x<8?.25:x<12?.5:1;
        effect[p]=ReferenceHalf(a*(3+double(x%5)/4));effect[p+1]=ReferenceHalf(a*(1+double(y)/8));
        effect[p+2]=ReferenceHalf(a*4);effect[p+3]=ReferenceHalf(a);
        native[p]=ReferenceHalf(9+double(x)/16);native[p+1]=ReferenceHalf(.125+double(y)/32);
        native[p+2]=ReferenceHalf(7+double((x+y)%2)/2);native[p+3]=ReferenceHalf(1);
    }
    const auto sample=[&](const std::vector<uint16_t>& image,double x,double y,unsigned c) {
        const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));
        const double fx=x-ix,fy=y-iy;
        const auto at=[&](int xx,int yy){return double(ReadHalf(image[(size_t(std::clamp(yy,0,int(lh)-1))*lw+std::clamp(xx,0,int(lw)-1))*4+c]));};
        return (1-fy)*((1-fx)*at(ix,iy)+fx*at(ix+1,iy))+fy*((1-fx)*at(ix,iy+1)+fx*at(ix+1,iy+1));
    };
    report["kind"]="synthetic HDR premultiplied effect GPU composite versus independent CPU sampling";
    report["gt7_data_used"]=false;report["ngx_evaluated"]=false;report["requested_preset"]=nullptr;
    report["low_extent"]={lw,lh};report["output_extent"]={w,height};report["cases"]=Json::array();
    report["reference"]="double-precision normalized-center linear clamp sampling; max 2x2 clamped invalid mask; RGB=(1-A)*base+P without alpha/RGB saturation, invalid selects linear-clamp native RGB; output alpha=1";
    report["acceptance"]="All pixels finite, alpha exactly one, RGB <= 1 FP16 ULP from nearest-even reference";
    report["limitations"]="Synthetic base/effect/native/mask only; does not execute guest effect shaders or establish real-scene blend equivalence/visual quality";
    for(unsigned test=0;test<4;++test) {
        if(test)gpu.RecycleCompleted();
        if(test==2) {
            // Half-quantized fractions force nontrivial interpolation/composite
            // rounding instead of checking only exactly representable colors.
            for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<w;++x) {
                const size_t p=(size_t(y)*w+x)*4;
                base[p]=ReferenceHalf(1+double(x%11)/37);base[p+1]=ReferenceHalf(2+double(y%7)/43);
                base[p+2]=ReferenceHalf(.25+double((x+y)%13)/71);
            }
            for(uint32_t y=0;y<lh;++y)for(uint32_t x=0;x<lw;++x) {
                const size_t p=(size_t(y)*lw+x)*4;const double a=ReadHalf(effect[p+3]);
                effect[p]=ReferenceHalf(a*(3+double(x%5)/13));effect[p+1]=ReferenceHalf(a*(1+double(y)/19));
                effect[p+2]=ReferenceHalf(a*(4+double(x+y)/59));
                native[p]=ReferenceHalf(9+double(x)/29);native[p+1]=ReferenceHalf(.125+double(y)/31);
                native[p+2]=ReferenceHalf(7+double((x+y)%7)/23);
            }
        }
        if(test==3) {
            // Isolate the no-saturation algebra with exact binary sources;
            // the preceding case separately exercises fractional HDR rounding.
            for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<w;++x) {
                const size_t p=(size_t(y)*w+x)*4;
                base[p]=ReferenceHalf(1+double(x%11)/16);base[p+1]=ReferenceHalf(2+double(y%7)/8);
                base[p+2]=ReferenceHalf(.25+double((x+y)%13)/32);
            }
            for(uint32_t y=0;y<lh;++y)for(uint32_t x=0;x<lw;++x) {
                const size_t p=(size_t(y)*lw+x)*4;
                const double a=x<4?-.25:x<8?0:x<12?1:1.25;
                effect[p]=ReferenceHalf(a*(3+double(x%5)/4));effect[p+1]=ReferenceHalf(a*(1+double(y)/8));
                effect[p+2]=ReferenceHalf(a*4);effect[p+3]=ReferenceHalf(a);
            }
        }
        for(uint32_t y=0;y<lh;++y)for(uint32_t x=0;x<lw;++x)
            mask[size_t(y)*lw+x]=test&&(x==0||y==0||x==lw-1||y==lh-1||(x==lw/2&&y>=3&&y<=7)||(x==5&&y==5))?255:0;
        void* mapped {};CheckVk(gpu.MapMemory(h.device,gpu.buffers[0].memory,0,VK_WHOLE_SIZE,0,&mapped),"Map composite upload");
        auto* bytes=static_cast<std::byte*>(mapped);
        std::memcpy(bytes+offsets[0],base.data(),base.size()*2);std::memcpy(bytes+offsets[1],effect.data(),effect.size()*2);
        std::memcpy(bytes+offsets[2],native.data(),native.size()*2);std::memcpy(bytes+offsets[3],mask.data(),mask.size());
        gpu.UnmapMemory(h.device,gpu.buffers[0].memory);gpu.Begin();
        for(unsigned n=0;n<4;++n) {
            gpu.Transition(gpu.images[n],test?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                test?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
            const auto region=PrepareGpu::CopyRegion(gpu.images[n],offsets[n]);
            gpu.CmdCopyBufferToImage(gpu.command,gpu.buffers[0].buffer,gpu.images[n].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
            gpu.Transition(gpu.images[n],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
        }
        gpu.Transition(gpu.images[4],test?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            test?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkClearColorValue nan {};for(auto& v:nan.float32)v=std::numeric_limits<float>::quiet_NaN();
        const VkImageSubresourceRange range {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        gpu.CmdClearColorImage(gpu.command,gpu.images[4].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&nan,1,&range);
        gpu.Transition(gpu.images[4],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT);
        const std::array<uint32_t,4> push {w,height,lw,lh};
        gpu.CmdBindPipeline(gpu.command,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipeline);
        gpu.CmdBindDescriptorSets(gpu.command,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipeline_layout,0,1,&gpu.set,0,nullptr);
        gpu.CmdPushConstants(gpu.command,gpu.pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,push.data());
        gpu.CmdDispatch(gpu.command,(w+gpu.local_x-1)/gpu.local_x,(height+gpu.local_y-1)/gpu.local_y,1);
        gpu.Transition(gpu.images[4],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        const auto region=PrepareGpu::CopyRegion(gpu.images[4]);
        gpu.CmdCopyImageToBuffer(gpu.command,gpu.images[4].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,gpu.buffers[1].buffer,1,&region);
        gpu.HostReadBarrier();gpu.SubmitAndReadbackWait();const auto rendered=gpu.Readback(1);
        std::vector<uint16_t> reference(pixels*4);
        size_t invalid_pixels=0,valid_pixels=0,alpha_zero=0,alpha_one=0,alpha_between=0,alpha_below=0,alpha_above=0,finite=0,alpha_errors=0,over_one_ulp=0;
        uint32_t max_ulp=0;double max_abs=0;std::array<size_t,3> histogram {};
        for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<w;++x) {
            const size_t p=size_t(y)*w+x;
            const double lx=(double(x)+.5)*lw/w-.5,ly=(double(y)+.5)*lh/height-.5;
            const int ix=int(std::floor(lx)),iy=int(std::floor(ly));bool invalid=false;
            for(int yy=0;yy<2;++yy)for(int xx=0;xx<2;++xx)
                invalid |= mask[size_t(std::clamp(iy+yy,0,int(lh)-1))*lw+std::clamp(ix+xx,0,int(lw)-1)]>127;
            const double alpha=sample(effect,lx,ly,3);
            if(invalid)++invalid_pixels;else {++valid_pixels;if(alpha<0)++alpha_below;else if(alpha>1)++alpha_above;else if(alpha==0)++alpha_zero;else if(alpha==1)++alpha_one;else ++alpha_between;}
            for(unsigned c=0;c<4;++c) {
                const double expected=c==3?1:invalid?sample(native,lx,ly,c):(1-alpha)*ReadHalf(base[p*4+c])+sample(effect,lx,ly,c);
                reference[p*4+c]=ReferenceHalf(expected);
                uint16_t actual;std::memcpy(&actual,rendered.data()+(p*4+c)*2,2);const float value=ReadHalf(actual);
                if(std::isfinite(value))++finite;
                if(c==3) {if(actual!=0x3c00)++alpha_errors;continue;}
                const auto ordered=[](uint16_t half){return half&0x8000?0x8000-int(half&0x7fff):0x8000+int(half);};
                const auto ulp=uint32_t(std::abs(ordered(actual)-ordered(reference[p*4+c])));
                max_ulp=std::max(max_ulp,ulp);++histogram[std::min(ulp,2u)];if(ulp>1)++over_one_ulp;
                max_abs=std::max(max_abs,std::isfinite(value)?std::abs(double(value)-expected):std::numeric_limits<double>::infinity());
            }
        }
        report["cases"].push_back({{"name",test==3?"unclamped_alpha_signed_hdr":test==2?"fractional_hdr_half_rounding":test?"four_edges_corners_interior_mask":"unmasked_alpha_bands"},{"pixels",pixels},
            {"invalid_pixels",invalid_pixels},{"valid_pixels",valid_pixels},{"valid_alpha_zero",alpha_zero},{"valid_alpha_one",alpha_one},
            {"valid_alpha_between",alpha_between},{"valid_alpha_below_zero",alpha_below},{"valid_alpha_above_one",alpha_above},
            {"finite_rgba",finite},{"alpha_errors",alpha_errors},{"rgb_max_ulp",max_ulp},
            {"rgb_ulp_0_1_over1",histogram},{"rgb_over_one_ulp",over_one_ulp},{"max_absolute_error_to_unrounded_reference",max_abs}});
        if(finite!=pixels*4||alpha_errors||over_one_ulp||!alpha_zero||!alpha_one||!alpha_between||(test&&!invalid_pixels)||
           (test==3&&(!alpha_below||!alpha_above)))
            throw std::runtime_error("GPU composite does not match independent CPU reference/coverage requirements");
        const auto save=[&](const std::filesystem::path& path,const void* data,size_t bytes_count){
            std::ofstream f(path,std::ios::binary);f.exceptions(std::ios::failbit|std::ios::badbit);f.write(static_cast<const char*>(data),static_cast<std::streamsize>(bytes_count));};
        save(output/("composite-"+std::to_string(test)+".gpu.rgba16f"),rendered.data(),rendered.size());
        save(output/("composite-"+std::to_string(test)+".cpu.rgba16f"),reference.data(),reference.size()*2);
    }
    report["passed"]=true;
}
