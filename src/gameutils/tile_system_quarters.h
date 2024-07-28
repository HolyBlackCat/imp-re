#pragma once

#include "program/errors.h"

// This is for managing tiles where each tile is divided diagonally into 4 triangles, each of which can be added/removed individually.

namespace GameUtils::TileQuarters
{
    template <typename T, T Invalid>
    struct TileConnectivity
    {
        // Bits:
        // 0 = connected to the tile on the right (+X)
        // 1 = connected to the tile below (+Y)
        // 2 = right  and bottom parts are connected (+X and +Y).
        // 3 = bottom and left   parts are connected (+Y and -X).
        // 4 = left   and top    parts are connected (-X and -Y).
        // 5 = top    and right  parts are connected (-Y and +X).
        std::uint8_t bits{};

        [[nodiscard]] bool GetConnectedToTile(int i) const
        {
            ASSERT(i >= 0 && i < 2);
            return bits & (1 << i);
        }
        void SetConnectedToTile(int i, bool value)
        {
            ASSERT(i >= 0 && i < 2);
            return bits & (1 << i);
        }
    };
}
