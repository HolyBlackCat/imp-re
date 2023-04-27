#include "texture_atlas.h"

#include <vector>

#include "meta/common.h"
#include "strings/format.h"
#include "utils/packing.h"

namespace Graphics
{
    Image MakeAtlas(ivec2 target_size, std::function<void(AtlasInputFunc func)> func, AtlasFlags flags)
    {
        // Load images.
        struct Elem
        {
            Image image; // Can be empty for empty images. But the size in `texcoords` is always correct.
            irect2 *texcoords = nullptr;
        };
        std::vector<Elem> elem_list;
        func([&](std::variant<Stream::ReadOnlyData, ivec2> data, irect2 &texcoords)
        {
            Elem &new_elem = elem_list.emplace_back();
            new_elem.texcoords = &texcoords;
            std::visit(Meta::overload{
                [&](const Stream::ReadOnlyData &data)
                {
                    new_elem.image = data;
                    texcoords = ivec2().rect_size(new_elem.image.Size());
                },
                [&](ivec2 size)
                {
                    texcoords = ivec2().rect_size(size);
                },
            }, data);
        });

        // Construct the rectangle list for packing.
        std::vector<Packing::Rect> rect_list;
        rect_list.reserve(elem_list.size());
        for (const Elem &elem : elem_list)
            rect_list.push_back(elem.texcoords->size());

        // Try packing the rectangles.
        if (Packing::PackRects(target_size, rect_list.data(), rect_list.size(), bool(flags & AtlasFlags::add_gaps)))
            throw std::runtime_error(FMT("Unable to fit texture atlas into a {}x{} texture.", target_size.x, target_size.y));

        // Construct the final image, and output the texture coords.
        Image ret = Image(target_size, u8vec4(0));
        for (size_t i = 0; i < elem_list.size(); i++)
        {
            // Copy the image, if any.
            if (elem_list[i].image)
                ret.UnsafeDrawImage(elem_list[i].image, rect_list[i].pos);

            // Output the coordinates.
            *elem_list[i].texcoords = rect_list[i].pos.rect_size(elem_list[i].texcoords->size());
        }

        return ret;
    }
}
