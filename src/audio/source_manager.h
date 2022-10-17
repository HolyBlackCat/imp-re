#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "audio/buffer.h"
#include "audio/source.h"
#include "program/errors.h"

namespace Audio
{
    // Keeps a list of `std::shared_ptr`s to sources.
    // Automatically releases them when they stop playing.
    class SourceManager
    {
        std::vector<std::shared_ptr<Source>> sources;

      public:
        SourceManager() {}

        // Add a new source to the manager.
        // It should be `play()`ed immediately, otherwise it will be removed at the next `Tick()`.
        void Add(std::shared_ptr<Source> source)
        {
            ASSERT(source, "Passing a null source.");
            ASSERT(std::find(sources.begin(), sources.end(), source) == sources.end(), "Adding a duplicate source to `Audio::SourceManager`.");
            sources.push_back(std::move(source));
        }
        // Add a new source to the manager.
        // It should be `play()`ed immediately, otherwise it will be removed at the next `Tick()`.
        [[nodiscard]] std::shared_ptr<Source> Add(const Buffer &buffer)
        {
            return sources.emplace_back(std::make_shared<Source>(buffer));
        }

        // Add a new source and play it immediately.
        std::shared_ptr<Source> Play(const Buffer &buffer, fvec3 pos, float volume = 1, float pitch = 0)
        {
            std::shared_ptr<Source> ret = Add(buffer);
            ret->pos(pos).volume(volume).pitch(pitch).play();
            return ret;
        }
        std::shared_ptr<Source> Play(const Buffer &buffer, fvec2 pos, float volume = 1, float pitch = 0)
        {
            std::shared_ptr<Source> ret = Add(buffer);
            ret->pos(pos).volume(volume).pitch(pitch).play();
            return ret;
        }
        std::shared_ptr<Source> Play(const Buffer &buffer, float volume = 1, float pitch = 0)
        {
            std::shared_ptr<Source> ret = Add(buffer);
            ret->relative().volume(volume).pitch(pitch).play();
            return ret;
        }

        // Releases sources that aren't playing (i.e. are stopped, paused, or not played yet).
        // Call this at the end of every tick.
        void Tick()
        {
            std::erase_if(sources, [](const std::shared_ptr<Source> &ptr){return !ptr->IsPlaying();});
        }

        [[nodiscard]] std::size_t ActiveSources() const
        {
            return sources.size();
        }
    };
}
