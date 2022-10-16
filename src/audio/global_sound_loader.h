#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "audio/buffer.h"
#include "audio/sound.h"
#include "meta/common.h"
#include "meta/const_string.h"
#include "program/errors.h"

// Provides singletones to conveniently load sounds.

namespace Audio::GlobalData
{
    template <typename T> concept ChannelsOrNullptr = Meta::same_as_any_of<T, Channels, std::nullptr_t>;
    template <typename T> concept FormatOrNullptr = Meta::same_as_any_of<T, Format, std::nullptr_t>;

    namespace impl
    {
        struct AutoLoadedBuffer
        {
            Buffer buffer;
            // Those may override the parameters specified when calling `LoadFiles()`.
            std::optional<Channels> channels_override;
            std::optional<Format> format_override;
        };

        // We rely on `std::map` never invalidating the references.
        using AutoLoadedBuffersMap = std::map<std::string, AutoLoadedBuffer, std::less<>>;

        [[nodiscard]] inline AutoLoadedBuffersMap &GetAutoLoadedBuffers()
        {
            static AutoLoadedBuffersMap ret;
            return ret;
        }

        template <Meta::ConstString Name, ChannelsOrNullptr auto ChannelCount, FormatOrNullptr auto FileFormat>
        struct RegisterAutoLoadedBuffer
        {
            [[maybe_unused]] inline static const Buffer &ref = []() -> Buffer &
            {
                auto it = GetAutoLoadedBuffers().find(Name.str);
                auto [data, ok] = GetAutoLoadedBuffers().try_emplace(Name.str);
                ASSERT(ok, "Attempt to register a duplicate auto-loaded sound file. This shouldn't be possible.");
                if constexpr (!std::is_null_pointer_v<decltype(ChannelCount)>)
                    data->second.channels_override = ChannelCount;
                if constexpr (!std::is_null_pointer_v<decltype(FileFormat)>)
                    data->second.format_override = FileFormat;
                return data->second.buffer; // We rely on `std::map` never invalidating the references.
            }();
        };
    }

    // Returns a reference to a buffer, loaded from the filename passed as the parameter.
    // The load doesn't happen at the call point, and is done by `LoadFiles()`, which magically knows all files that it needs to load in this manner.
    template <Meta::ConstString Name, ChannelsOrNullptr auto ChannelCount = nullptr, FormatOrNullptr auto FileFormat = nullptr>
    [[nodiscard]] const Buffer &File()
    {
        return impl::RegisterAutoLoadedBuffer<Name, ChannelCount, FileFormat>::ref;
    }

    // Loads (or reloads) all files requested with `Audio::GlobalData::File()`. Consider using the simplified overload, defined below.
    // The number of channels and the file format can be overridden by the `File()` calls.
    // `get_stream` is called repeatedly for all needed files.
    inline void Load(std::optional<Channels> channels, Format format, std::function<Stream::Input(const std::string &name, std::optional<Channels> channels, Format format)> get_stream)
    {
        for (auto &[name, data] : impl::GetAutoLoadedBuffers())
        {
            std::optional<Channels> file_channels = data.channels_override ? data.channels_override : channels;
            Format file_format = data.format_override.value_or(format);
            data.buffer = Audio::Sound(file_format, file_channels, get_stream(name, file_channels, file_format));
        }
    }

    // Same, but the sounds are loaded from files named `prefix + name + ext`,
    // where `name` comes from the `File()` call, and `ext` is determined from the format (`.wav` or `.ogg`).
    inline void Load(std::optional<Channels> channels, Format format, const std::string &prefix)
    {
        Load(channels, format, [&prefix](const std::string &name, std::optional<Channels> channels, Format format) -> Stream::Input
        {
            (void)channels;
            const char *ext = "";
            switch (format)
            {
                case wav: ext = ".wav"; break;
                case ogg: ext = ".ogg"; break;
            }
            return prefix + name + ext;
        });
    }
}
