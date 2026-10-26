#include "graphics/vr/vrEyeImage.h"
#include "graphics/guest_gpu/tile.h"
#include "graphics/host_gpu/renderer/image/textureCommon.h"
#include <bit>
#include <limits>

namespace Libs::Graphics::Vr {
namespace {
bool ColorFormatSupported(Prospero::BufferFormat format) {
    // Initial supported final color surfaces. No depth, integer, block-compressed or
    // shader-only format conversion is guessed when exporting an eye to the compositor.
    switch(format) {
        case Prospero::BufferFormat::k8_8_8_8UNorm:
        case Prospero::BufferFormat::k8_8_8_8Srgb:
        case Prospero::BufferFormat::k10_10_10_2UNorm:
        case Prospero::BufferFormat::k16_16_16_16Float: return true;
        default: return false;
    }
}
bool ValidAlignedRange(uint64_t address, const TileSizeAlign& size) {
    return address != 0 && size.size != 0 && size.align != 0 && std::has_single_bit(size.align) &&
        (address & (static_cast<uint64_t>(size.align)-1)) == 0 &&
        address < (uint64_t{1}<<48) && size.size <= (uint64_t{1}<<48)-address;
}
}

bool DescribeEyeImages(const std::array<ShaderTextureResource, 2>& textures,
                       std::array<ImageInfo, 2>& images, std::array<uint32_t, 2>& layers) {
    images={}; layers={};
    constexpr uint32_t SliceFields = 0x1fff1fffu;
    for(uint32_t word=0;word<8;++word) {
        const uint32_t mask = word==4 ? ~SliceFields : UINT32_MAX;
        if((textures[0].fields[word]&mask)!=(textures[1].fields[word]&mask)) return false;
    }
    if((textures[0].fields[4]&SliceFields)!=0 ||
       (textures[1].fields[4]&SliceFields)!=0x00010001u) return false;
    const auto& texture=textures[0];
    if(texture.Type()!=Prospero::ImageType::kColor2DArray || texture.BaseLevel()!=0 ||
       texture.LastLevel()!=0 || texture.MaxMip()!=0 || texture.MinLod()!=0 ||
       texture.MsaaDepth() || texture.DstSelXYZW()!=0xfacu) return false;
    const auto format=texture.Format();
    if(!ColorFormatSupported(format) || !TextureFormatKnown(format)) return false;
    const auto surface=TextureGetSurfaceFormatInfo(format);
    if(surface.conversion_format!=Prospero::BufferFormat::kInvalid ||
       !surface.host_to_storage.IsIdentity() || surface.vk_format==vk::Format::eUndefined) return false;
    const uint32_t width=static_cast<uint32_t>(texture.Width5())+1;
    const uint32_t height=static_cast<uint32_t>(texture.Height5())+1;
    // This experimental exporter supports up to 8K per eye, and keeps the existing
    // 32-bit tiler size domain safe even for hostile/malformed descriptors.
    if(width>8192 || height>8192) return false;
    const auto tile=texture.TileMode();
    if(tile!=Prospero::TileMode::kLinear && tile!=Prospero::TileMode::kStandard4KB &&
       tile!=Prospero::TileMode::kStandard64KB && tile!=Prospero::TileMode::kRenderTarget) return false;
    constexpr uint32_t PhysicalLayers=2, Levels=1;
    if(!TileTextureLayoutKnown(format,width,height,PhysicalLayers,Levels,tile,false)) return false;
    TileSizeAlign size{};
    TileGetTextureTotalSize(format,width,height,PhysicalLayers,Levels,tile,false,size);
    if(!ValidAlignedRange(texture.Base40(),size)) return false;

    // Same construction/tiler calls as descriptors.cpp::ResolveTexture and
    // PopulateTextureMipLayout, with the physical layer extent recovered from BOTH eyes.
    // Using left.Depth()+1 here would silently halve the allocation and DCC layout.
    ImageInfo info{};
    info.data={texture.Base40(),size.size};
    info.pixel_format=surface.vk_format;
    info.guest_format=format;
    info.type=Prospero::ImageType::kColor2D;
    info.extent={width,height,1};
    info.resources={Levels,PhysicalLayers};
    info.pitch=TileGetTexturePitch(format,width,tile);
    info.bytes_per_block=Prospero::NumBytesPerElement(format);
    info.samples=1; info.tile_mode=tile;
    if(info.pitch==0 || info.bytes_per_block==0) return false;
    TileSizeOffset mip[1]{};
    TilePaddedSize padded[1]{};
    TileGetTextureSize(format,width,height,Levels,tile,nullptr,mip,padded);
    const uint64_t offset=mip[0].src_size!=0 ? mip[0].src_offset : mip[0].offset;
    const uint64_t slice_size=mip[0].src_size!=0 ? mip[0].src_size : mip[0].size;
    if(slice_size==0 || offset>size.size/PhysicalLayers ||
       slice_size>size.size/PhysicalLayers-offset || padded[0].width==0 || padded[0].height==0) return false;
    info.mip_layout[0]={offset,slice_size*PhysicalLayers,padded[0].width,padded[0].height};
    if(texture.MetaCompress()) {
        TileSizeAlign metadata{};
        if(!TileGetDccSize(width,height,PhysicalLayers,info.bytes_per_block,Levels,tile,metadata,0) ||
           !ValidAlignedRange(texture.MetaAddr()<<8u,metadata)) return false;
        info.metadata.kind=ImageMetadataKind::Dcc;
        info.metadata.range={texture.MetaAddr()<<8u,metadata.size};
        info.metadata.dcc_alpha_msb=texture.DccAlphaPos();
    }
    images[0]=info; images[1]=info;
    layers={0,1};
    return true;
}
} // namespace Libs::Graphics::Vr
