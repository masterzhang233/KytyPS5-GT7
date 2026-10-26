#include "graphics/vr/vrEyeImage.h"
#include "graphics/guest_gpu/tile.h"
#include <cstdio>
#include <stdexcept>
#include <string>

// Only fatal/log sinks are replaced. Image decoding, format mapping and tiled layout are
// real production code. No emulated process, Vulkan device or SteamVR session is created.
namespace Common {
int DbgExitHandler(const char*,int,std::string_view text){throw std::runtime_error(std::string(text));}
int DbgExitHandler(const char*,int,fmt::text_style,std::string_view text){throw std::runtime_error(std::string(text));}
int DbgExitIfHandler(const char* text,const char*,int){throw std::runtime_error(text);}
int DbgNotImplementedHandler(const char* text,const char*,int){throw std::runtime_error(text);}
void DbgExit(int){throw std::runtime_error("unexpected fatal");}
}
using namespace Libs::Graphics;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(false)
static int groups=0;
static void Pass(const char* text){++groups;std::printf("PASS %s\n",text);}
static std::array<ShaderTextureResource,2> Make(uint32_t w=64,uint32_t h=64,
    Prospero::TileMode tile=Prospero::TileMode::kLinear,Prospero::BufferFormat fmt=Prospero::BufferFormat::k8_8_8_8UNorm) {
    ShaderTextureResource t;
    constexpr uint64_t address=0x1200000000ull;
    t.fields[0]=static_cast<uint32_t>(address>>8);
    t.fields[1]=static_cast<uint32_t>((address>>40)&0xff) | (static_cast<uint32_t>(fmt)<<20) | (((w-1)&3)<<30);
    t.fields[2]=((w-1)>>2)|((h-1)<<14);
    t.fields[3]=(13u<<28)|(static_cast<uint32_t>(tile)<<20)|0xfac;
    std::array<ShaderTextureResource,2> pair{t,t};pair[1].fields[4]=0x10001;
    return pair;
}
static void Dcc(std::array<ShaderTextureResource,2>& pair) {
    constexpr uint64_t address=0x1300000000ull;
    for(auto& t:pair){t.fields[6]=(1u<<21)|(1u<<22)|static_cast<uint32_t>(((address>>8)&0xff)<<24);t.fields[7]=static_cast<uint32_t>(address>>16);}
}
int main(){try{
    std::array<ImageInfo,2> image;std::array<uint32_t,2> layer;
    {auto t=Make();CHECK(Vr::DescribeEyeImages(t,image,layer));CHECK(layer[0]==0&&layer[1]==1);for(auto& i:image){CHECK(i.resources.layers==2&&i.resources.levels==1);CHECK(i.data.size==32768);CHECK(i.mip_layout[0].size==32768);CHECK(i.pitch==64);CHECK(i.data.address==0x1200000000ull);CHECK(i.type==Prospero::ImageType::kColor2D);}CHECK(t[0].Depth()==0&&t[1].Depth()==1);Pass("left_depth_zero_keeps_complete_two_layer_allocation");}
    {auto t=Make(2528,2704);CHECK(Vr::DescribeEyeImages(t,image,layer));CHECK(image[0].pitch==2560);CHECK(image[0].data.size==uint64_t{2560}*2704*4*2);CHECK(image[0].data==image[1].data);Pass("actual_recommended_extent_linear_row_padding");}
    {for(auto tile:{Prospero::TileMode::kStandard4KB,Prospero::TileMode::kStandard64KB,Prospero::TileMode::kRenderTarget}){auto t=Make(128,64,tile);CHECK(Vr::DescribeEyeImages(t,image,layer));TileSizeAlign one,two;TileGetTextureTotalSize(t[0].Format(),128,64,1,1,tile,false,one);TileGetTextureTotalSize(t[0].Format(),128,64,2,1,tile,false,two);CHECK(image[0].data.size==two.size&&two.size==2*one.size);CHECK(image[0].mip_layout[0].size==two.size);CHECK(image[0].tile_mode==tile);}Pass("real_tiler_full_array_for_all_supported_tiled_modes");}
    {auto t=Make(128,64,Prospero::TileMode::kRenderTarget);Dcc(t);CHECK(Vr::DescribeEyeImages(t,image,layer));TileSizeAlign dcc;CHECK(TileGetDccSize(128,64,2,4,1,Prospero::TileMode::kRenderTarget,dcc));for(auto& i:image){CHECK(i.metadata.kind==ImageMetadataKind::Dcc);CHECK(i.metadata.range.address==0x1300000000ull);CHECK(i.metadata.range.size==dcc.size);CHECK(i.metadata.dcc_alpha_msb);}Pass("dcc_address_size_alpha_and_two_layers_preserved");}
    {for(auto format:{Prospero::BufferFormat::k8_8_8_8Srgb,Prospero::BufferFormat::k10_10_10_2UNorm,Prospero::BufferFormat::k16_16_16_16Float}){auto t=Make(64,64,Prospero::TileMode::kLinear,format);CHECK(Vr::DescribeEyeImages(t,image,layer));CHECK(image[0].guest_format==format);CHECK(image[0].pixel_format!=vk::Format::eUndefined);}Pass("real_format_mapping_four_supported_color_formats");}
    {for(uint32_t word=0;word<8;++word){auto t=Make();t[1].fields[word]^=word==4?1u<<30:1u;CHECK(!Vr::DescribeEyeImages(t,image,layer));CHECK(image[0].data.size==0&&image[1].data.size==0);}Pass("all_word_mismatches_rejected_and_output_cleared");}
    {for(int kind=0;kind<3;++kind){auto t=Make();if(kind==0)t[1].fields[4]=0;if(kind==1)t[0].fields[4]=0x10001;if(kind==2)t[1].fields[4]=0x20002;CHECK(!Vr::DescribeEyeImages(t,image,layer));}Pass("exact_guest_slice_pair_only");}
    {for(int kind=0;kind<8;++kind){auto t=Make();for(auto& d:t){if(kind==0)d.fields[3]|=1u<<12;if(kind==1)d.fields[3]|=1u<<16;if(kind==2)d.fields[5]|=1u<<4;if(kind==3)d.fields[1]|=1u<<8;if(kind==4)d.fields[6]|=1u<<10;if(kind==5)d.fields[3]=(d.fields[3]&0x0fffffff)|(14u<<28);if(kind==6)d.fields[3]^=1;if(kind==7)d.fields[3]=(d.fields[3]&~(31u<<20))|(31u<<20);}CHECK(!Vr::DescribeEyeImages(t,image,layer));}Pass("mips_msaa_swizzle_type_unknown_tile_reject");}
    {auto t=Make(16384,16384);CHECK(!Vr::DescribeEyeImages(t,image,layer));t=Make();for(auto& d:t)d.fields[1]=(d.fields[1]&~(511u<<20))|(511u<<20);CHECK(!Vr::DescribeEyeImages(t,image,layer));t=Make(64,64,Prospero::TileMode::kRenderTarget);for(auto& d:t)++d.fields[0];CHECK(!Vr::DescribeEyeImages(t,image,layer));Pass("oversized_unknown_format_misaligned_address_reject");}
    {auto t=Make(64,64,Prospero::TileMode::kRenderTarget);for(auto& d:t)d.fields[6]|=1u<<21;CHECK(!Vr::DescribeEyeImages(t,image,layer));t=Make();Dcc(t);CHECK(!Vr::DescribeEyeImages(t,image,layer));Pass("dcc_requires_valid_nonzero_supported_metadata_layout");}
    std::printf("PASSED %d groups; real CPU format/tiler code, no GPU\n",groups);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAILED %s\n",e.what());return 1;}}
