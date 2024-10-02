#pragma once

#include <algorithm>
#include <vector>
#include <utility>

#include <stb_image.h>
#include <stb_image_write.h>

#include "macros/finally.h"
#include "stream/readonly_data.h"
#include "strings/format.h"
#include "utils/mat.h"

namespace Graphics
{
    class Image
    {
        // Note that moved-from instance is left in an invalid (yet destructable) state.

        ivec2 size = ivec2(0);
        std::vector<u8vec4> data;

      public:
        enum Format {png, tga};
        enum FlipMode {no_flip, flip_y};

        Image() {}
        Image(ivec2 size, const uint8_t *bytes = 0) : size(size) // If `bytes == 0`, then the image will be filled with transparent black.
        {
            if (bytes)
                data = std::vector<u8vec4>((u8vec4 *)bytes, (u8vec4 *)bytes + size.prod());
            else
                data = std::vector<u8vec4>(size.prod());
        }
        Image(ivec2 size, u8vec4 color) : size(size)
        {
            data = std::vector<u8vec4>(size.prod(), color);
        }
        Image(Stream::ReadOnlyData file, FlipMode flip_mode = no_flip) // Throws on failure.
        {
            stbi_set_flip_vertically_on_load(flip_mode == flip_y);
            ivec2 img_size;
            uint8_t *bytes = stbi_load_from_memory(file.data(), file.size(), &img_size.x, &img_size.y, 0, 4);
            if (!bytes)
                throw std::runtime_error(FMT("Unable to parse image: {}", file.name()));
            FINALLY{stbi_image_free(bytes);};
            *this = Image(img_size, bytes);
        }

        explicit operator bool() const {return data.size() > 0;}

        const u8vec4 *Pixels() const {return data.data();}
        const uint8_t *Data() const {return reinterpret_cast<const uint8_t *>(Pixels());}
        ivec2 Size() const {return size;}
        irect2 Bounds() const {return ivec2().rect_size(Size());}

        // Throws on failure.
        void Save(std::string file_name, Format format = png) const
        {
            if (!*this)
                throw std::runtime_error("Attempt to save an empty image to a file.");

            int ok = 0;
            switch (format)
            {
              case png:
                ok = stbi_write_png(file_name.c_str(), size.x, size.y, 4, data.data(), 0);
                break;
              case tga:
                ok = stbi_write_tga(file_name.c_str(), size.x, size.y, 4, data.data());
                break;
            }

            if (!ok)
                throw std::runtime_error(FMT("Unable to write image to file: {}", file_name));
        }

        u8vec4 &UnsafeAt(ivec2 pos)
        {
            return const_cast<u8vec4 &>(std::as_const(*this).UnsafeAt(pos));
        }
        const u8vec4 &UnsafeAt(ivec2 pos) const
        {
            return data[pos.x + pos.y * size.x];
        }

        u8vec4 TryGet(ivec2 pos) const // Returns transparent black if out of range.
        {
            if (Bounds().contains(pos))
                return UnsafeAt(pos);
            else
                return u8vec4(0);
        }
        void TrySet(ivec2 pos, u8vec4 color)
        {
            if (Bounds().contains(pos))
                UnsafeAt(pos) = color;
        }

        void UnsafeFill(irect2 rect, u8vec4 color)
        {
            for (ivec2 pos : vector_range(rect))
                UnsafeAt(pos) = color;
        }

        // Copies other image into this image, at specified location.
        void UnsafeDrawImage(const Image &other, ivec2 pos)
        {
            for (int y = 0; y < other.Size().y; y++)
            {
                auto source_address = &other.UnsafeAt(ivec2(0,y));
                std::copy(source_address, source_address + other.Size().x, &UnsafeAt(ivec2(pos.x, y + pos.y)));
            }
        }

        // Copies other image into this image, at specified location.
        // Prefers the pixels with higher alpha, with the other image getting preference.
        void UnsafeDrawImagePickMaxAlpha(const Image &other, ivec2 pos, irect2 source_rect)
        {
            for (ivec2 source_pos : vector_range(source_rect))
            {
                u8vec4 source_pixel = other.UnsafeAt(source_pos);
                u8vec4 &target_pixel = UnsafeAt(pos + source_pos);
                if (source_pixel.a() >= target_pixel.a())
                    target_pixel = source_pixel;
            }
        }
        void UnsafeDrawImagePickMaxAlpha(const Image &other, ivec2 pos)
        {
            UnsafeDrawImagePickMaxAlpha(other, pos, other.Bounds());
        }
    };
}
