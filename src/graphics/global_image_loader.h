#pragma once

#include <functional>
#include <map>
#include <string_view>
#include <string>
#include <vector>

#include "graphics/image.h"
#include "graphics/texture_atlas.h"
#include "graphics/texture.h"
#include "macros/enum_flag_operators.h"
#include "meta/const_string.h"
#include "stream/readonly_data.h"
#include "utils/mat.h"

namespace Graphics::GlobalData
{
    // A rect in a texture atlas, plus the name of that atlas.
    struct Region : irect2
    {
        using irect2::operator=; // This lets us do `Region(...) with(().shrink(n))`, and so on.
        std::string_view atlas;
    };

    // Describes a single global texture atlas.
    struct Atlas
    {
        TexObject texture;
        ivec2 size; // Since `image` can be null, and the texture doesn't remember its size, we also store it here.
        Image image; // Normally null, unless `Flags::keep_images` is used.
    };

    using AtlasMap = std::map<std::string, Atlas, std::less<>>;

    namespace impl
    {
        struct ImageData
        {
            Region region;
            bool empty = false; // If true, don't load a file and create an empty image of the specified size.
        };

        struct State
        {
            // We rely on `std::map` never invalidating the references.
            std::map<std::string, ImageData, std::less<>> regions;
            using RegionPair = decltype(regions)::value_type;

            AtlasMap atlases;
        };

        [[nodiscard]] inline State &GetState()
        {
            static State ret;
            return ret;
        }

        template <Meta::ConstString Name, ivec2 Size>
        struct RegisterImage
        {
            inline static const Region &ref = []() -> Region &
            {
                auto [iter, ok] = GetState().regions.try_emplace(Name.str);
                ASSERT(ok, "Attempt to register a duplicate auto-loaded image. This shouldn't be possible.");
                if (Size)
                {
                    iter->second.empty = true;
                    static_cast<irect2 &>(iter->second.region) = ivec2().rect_size(Size);
                }
                return iter->second.region;
            }();
        };
    }

    // Loads an image.
    // If `Size` is specified, no file is loaded and an empty image of the specified size is created.
    // The returned reference is stable across reloads.
    template <Meta::ConstString Name, ivec2 Size = ivec2()>
    [[nodiscard]] const Region &Image()
    {
        return impl::RegisterImage<Name, Size>::ref;
    }

    // Same as `EmptyImage()`, but doesn't let you specify `Size`.
    template <Meta::ConstString Name>
    [[nodiscard]] const Region &operator""_image()
    {
        return Image<Name>();
    }

    enum class Flags
    {
        none = 0,
        keep_image = 1 << 0, // Preserve the generate image (in RAM, in addition to the texture).
        no_texture = 1 << 1, // Only load the image, don't generate a texture. Implies `keep_image`.
    };
    IMP_ENUM_FLAG_OPERATORS(Flags)
    using enum Flags;

    struct AtlasParams
    {
        ivec2 size = ivec2(2048);
        Flags flags{};

        static constexpr AtlasFlags default_atlas_flags = AtlasFlags::add_gaps;
        AtlasFlags atlas_flags = default_atlas_flags;

        WrapMode texture_wrap = WrapMode::fill;
        InterpolationMode texture_interpolation = InterpolationMode::nearest;

        // Optional, modifies an image after it's loaded, before generating a texture.
        std::function<void(Graphics::Image &image)> modify_image;
    };

    struct LoadParams
    {
        std::function<Stream::ReadOnlyData(const std::string &name)> get_data; // Mandatory, returns the memory to load the image from.
        std::function<std::string(const std::string &name)> name_to_atlas; // Optional, maps images to atlases. Assumed to return an empty string by default, putting all images into a single atlas.
        std::function<AtlasParams(const std::string &atlas)> atlas_params; // Optional, returns per-atlas parameters. Returns default-constructed parameters by default.

        LoadParams() {}

        // Constructs the minimal viable parameters.
        LoadParams(std::string prefix) : get_data(LoadFileFromPrefix(std::move(prefix))) {}

        // A default value for `get_data`, that loads files from a specific prefix.
        static decltype(get_data) LoadFileFromPrefix(std::string prefix, std::string suffix = ".png")
        {
            return [prefix = std::move(prefix), suffix = std::move(suffix)](const std::string &name) -> Stream::ReadOnlyData
            {
                return Stream::ReadOnlyData(FMT("{}{}{}", prefix, name, suffix));
            };
        }
    };

    // Loads all images mentioned in `Image()` calls into atlases.
    inline void Load(const LoadParams &params)
    {
        // In case the list of atlases changes.
        impl::GetState().atlases.clear();

        // Group the regions by atlases.
        std::map<std::string, std::vector<impl::State::RegionPair *>, std::less<>> regions_per_atlas;
        for (auto &elem : impl::GetState().regions)
            regions_per_atlas[params.name_to_atlas ? params.name_to_atlas(elem.first) : std::string{}].push_back(&elem);

        TexUnit tex_unit = nullptr; // We need this to upload images to textures.

        for (auto &[atlas_name, regions] : regions_per_atlas)
        {
            Atlas &atlas = impl::GetState().atlases.try_emplace(atlas_name).first->second;

            AtlasParams atlas_params;
            if (params.atlas_params)
                atlas_params = params.atlas_params(atlas_name);

            atlas.image = MakeAtlas(atlas_params.size, [&, &regions = regions](AtlasInputFunc func)
            {
                for (impl::State::RegionPair *pair : regions)
                {
                    if (pair->second.empty)
                        func(pair->second.region.size(), pair->second.region);
                    else
                        func(params.get_data(pair->first), pair->second.region);
                }
            }, atlas_params.atlas_flags);

            if (atlas_params.modify_image)
                atlas_params.modify_image(atlas.image);

            atlas.size = atlas.image.Size();

            if (!bool(atlas_params.flags & Flags::no_texture))
            {
                atlas.texture = nullptr;
                tex_unit.Attach(atlas.texture).SetData(atlas.image).Wrap(atlas_params.texture_wrap).Interpolation(atlas_params.texture_interpolation);
                if (!bool(atlas_params.flags & Flags::keep_image))
                    atlas.image = {};
            }
        }
    }

    // Returns a map of all loaded atlases.
    // The atlas addresses are NOT stable across reloads.
    [[nodiscard]] inline const AtlasMap &GetAtlases()
    {
        return impl::GetState().atlases;
    }
}

namespace Graphics
{
    using GlobalData::Region;
}

using Graphics::GlobalData::operator""_image;
