#pragma once

#include <memory>
#include <utility>

#include "audio/global_sound_loader.h"
#include "audio/source_manager.h"
#include "audio/source.h"
#include "meta/const_string.h"
#include "utils/mat.h"

// Provides a convenient audio player that wraps `Audio::Context` and `Audio::SourceManager`.
// Don't forget to call `Audio::GlobalData::Load()` to load the used sounds.

namespace Audio
{
    class SimplePlayer
    {
      public:
        // You can use this to create unusual sources directly.
        Audio::SourceManager manager;

        // Call this at the end of every tick.
        void Tick()
        {
            manager.Tick();
        }

        // With fvec3 position.
        template <Meta::ConstString Name, GlobalData::ChannelsOrNullptr auto ChannelCount = nullptr, GlobalData::FormatOrNullptr auto FileFormat = nullptr>
        std::shared_ptr<Audio::Source> play(fvec3 pos, float volume = 1, float pitch = 0)
        {
            auto ret = manager.Add(GlobalData::File<Name, ChannelCount, FileFormat>());
            ret->pos(pos);
            ret->volume(volume);
            ret->pitch(pitch);
            ret->play();
            return ret;
        }

        // With fvec2 position.
        template <Meta::ConstString Name, GlobalData::ChannelsOrNullptr auto ChannelCount = nullptr, GlobalData::FormatOrNullptr auto FileFormat = nullptr>
        std::shared_ptr<Audio::Source> play(fvec2 pos, float volume = 1, float pitch = 0)
        {
            auto ret = manager.Add(GlobalData::File<Name, ChannelCount, FileFormat>());
            ret->pos(pos);
            ret->volume(volume);
            ret->pitch(pitch);
            ret->play();
            return ret;
        }

        // Without position.
        template <Meta::ConstString Name, GlobalData::ChannelsOrNullptr auto ChannelCount = nullptr, GlobalData::FormatOrNullptr auto FileFormat = nullptr>
        std::shared_ptr<Audio::Source> play(float volume = 1, float pitch = 0)
        {
            auto ret = manager.Add(GlobalData::File<Name, ChannelCount, FileFormat>());
            ret->relative();
            ret->volume(volume);
            ret->pitch(pitch);
            ret->play();
            return ret;
        }
    };
}
