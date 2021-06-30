#pragma once

#include "utils/mat.h"

namespace GameUtils
{
    struct DebugCamera
    {
        fquat orientation;
        fvec3 pos;

        float rotation_speed_factor = 1;
        float movement_speed_factor = 1;

        fmat4 matrix_proj;

        // `movement` is `fvec3(forward, left, up)`.
        // `rotation` is `fvec2(right, down)`.
        void ProcessInput(fvec3 movement, fvec2 rotation)
        {
            movement = fvec3(-movement.y, movement.z, -movement.x);
            rotation.y = -rotation.y;
            orientation *= fquat(rotation.rot90().to_vec3(), rotation.len() * rotation_speed_factor);
            pos += orientation * movement * movement_speed_factor;
        }

        // Returns the final camera matrix.
        [[nodiscard]] fmat4 CalcFinalMatrix() const
        {
            return matrix_proj * orientation.matrix().to_vec4(pos).to_mat4();
        }
    };
}
