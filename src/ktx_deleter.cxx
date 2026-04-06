
#include "types.hxx"

#include <ktx.h>

using KtxTextureDeleter = Deleter;

template<> auto KtxTextureDeleter::operator()<ktxTexture2>(ktxTexture2* texture) const -> void
{
    if (texture != nullptr)
    {
        ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(texture));
    }
}
