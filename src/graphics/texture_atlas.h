#pragma once

#include <functional>
#include <variant>

#include "graphics/image.h"
#include "macros/enum_flag_operators.h"
#include "stream/readonly_data.h"
#include "utils/mat.h"

namespace Graphics
{
    // Call this to add an image to the atlas.
    // `data` is either the image data or the size of an empty image.
    // `texcoords` receives the texture coords.
    using AtlasInputFunc = std::function<void(std::variant<Stream::ReadOnlyData, ivec2> data, irect2 &texcoords)>;

    enum class AtlasFlags
    {
        none = 0,
        add_gaps = 1 << 0,
    };
    IMP_ENUM_FLAG_OPERATORS(AtlasFlags)

    // Packs images into an atlas. Throws on failure.
    // Call the `func` you receive once for each image that needs to be loaded.
    [[nodiscard]] Image MakeAtlas(ivec2 target_size, std::function<void(AtlasInputFunc func)> func, AtlasFlags flags = AtlasFlags::none);
}
