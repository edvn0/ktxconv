#include "ktx2_write.hxx"

#include <ktx.h>

#include <cstring>
#include <memory>
#include <string>

namespace {
[[nodiscard]] auto error_string(KTX_error_code code) -> std::string {
  return std::string(ktxErrorString(code));
}

[[nodiscard]] auto compress_texture(ktxTexture2 *texture,
                                    CompressionConfig const &compression)
    -> bool {
  if (compression.mode == CompressionMode::None) {
    return true;
  }

  ktxBasisParams params{};
  params.structSize = sizeof(ktxBasisParams);
  params.threadCount = compression.thread_count;
  params.normalMap = compression.normal_map ? KTX_TRUE : KTX_FALSE;

  if (compression.mode == CompressionMode::BasisLz) {
    params.uastc = KTX_FALSE;
    params.qualityLevel = compression.quality_level;
    params.compressionLevel = compression.compression_level == 0
                                  ? KTX_ETC1S_DEFAULT_COMPRESSION_LEVEL
                                  : compression.compression_level;
  } else {
    params.uastc = KTX_TRUE;
    params.uastcRDO = compression.uastc_rdo ? KTX_TRUE : KTX_FALSE;
    params.uastcRDOQualityScalar = compression.uastc_rdo_quality_scalar;
    params.uastcRDODictSize = compression.uastc_rdo_dict_size;
    params.uastcRDOMaxSmoothBlockErrorScale =
        compression.uastc_rdo_max_smooth_block_error_scale;
    params.uastcRDOMaxSmoothBlockStdDev =
        compression.uastc_rdo_max_smooth_block_std_dev;
  }

  auto const rc = ktxTexture2_CompressBasisEx(texture, &params);
  if (rc != KTX_SUCCESS) {
    return false;
  }

  return true;
}
} // namespace

auto write_ktx2(std::filesystem::path const &path, PixelFormat format,
                std::vector<Image> const &mip_chain,
                CompressionConfig const &compression) -> bool {
  if (mip_chain.empty()) {
    return false;
  }

  auto vk_format_result = to_vk_format(format);
  if (!vk_format_result) {
    return false;
  }

  auto const &base = mip_chain.front();

  ktxTextureCreateInfo create_info{};
  create_info.vkFormat = static_cast<ktx_uint32_t>(vk_format_result);
  create_info.baseWidth = base.width;
  create_info.baseHeight = base.height;
  create_info.baseDepth = 1;
  create_info.numDimensions = 2;
  create_info.numLevels = static_cast<ktx_uint32_t>(mip_chain.size());
  create_info.numLayers = 1;
  create_info.numFaces = 1;
  create_info.isArray = KTX_FALSE;
  create_info.generateMipmaps = KTX_FALSE;

  ktxTexture2 *raw_texture = nullptr;
  auto rc = ktxTexture2_Create(&create_info, KTX_TEXTURE_CREATE_ALLOC_STORAGE,
                               &raw_texture);

  if (rc != KTX_SUCCESS) {
    return false;
  }

  std::unique_ptr<ktxTexture2, Deleter> texture{raw_texture};

  for (u32 level = 0; level < mip_chain.size(); ++level) {
    auto const &image = mip_chain[level];
    auto const size = static_cast<ktx_size_t>(image.pixels.size());

    rc = ktxTexture_SetImageFromMemory(
        std::bit_cast<ktxTexture *>(texture.get()), level, 0, 0,
        image.pixels.data(), size);

    if (rc != KTX_SUCCESS) {
      return false;
    }
  }

  auto compress_result = compress_texture(raw_texture, compression);
  if (!compress_result) {
    return false;
  }

  rc = ktxTexture2_WriteToNamedFile(texture.get(), path.string().c_str());
  if (rc != KTX_SUCCESS) {
    return false;
  }

  return true;
}

auto read_ktx2_info(std::filesystem::path const &path) -> Ktx2Info {
  ktxTexture2 *raw_texture = nullptr;
  auto result = ktxTexture2_CreateFromNamedFile(
      path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
      &raw_texture);

  if (result != KTX_SUCCESS) {
    throw std::runtime_error("ktxTexture2_CreateFromNamedFile failed: " +
                             error_string(result));
  }

  std::unique_ptr<ktxTexture2, Deleter> texture{raw_texture};

  Ktx2Info info{};
  info.width = texture->baseWidth;
  info.height = texture->baseHeight;
  info.levels = texture->numLevels;
  info.vk_format = texture->vkFormat;
  return info;
}
