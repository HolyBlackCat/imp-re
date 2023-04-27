#pragma once

#include "audio/context.h"
#include "audio/openal.h"

namespace Audio
{
    inline void CheckErrors()
    {
        if (!Context::Exists())
            return;

        switch (alcGetError(Context::Get().DeviceHandle()))
        {
            case 0:                   return;
            case ALC_INVALID_DEVICE:  throw std::runtime_error("OpenAL error: Invalid device.");
            case ALC_INVALID_CONTEXT: throw std::runtime_error("OpenAL error: Invalid context.");
            case ALC_INVALID_ENUM:    throw std::runtime_error("OpenAL error: Invalid enum.");
            case ALC_INVALID_VALUE:   throw std::runtime_error("OpenAL error: Invalid value.");
            case ALC_OUT_OF_MEMORY:   throw std::runtime_error("OpenAL error: Out of memory.");
            default:                  throw std::runtime_error("OpenAL error: Unknown error.");
        }
    }
}
