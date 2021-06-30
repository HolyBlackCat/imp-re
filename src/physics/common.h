#pragma once

#include "program/compiler.h"
#include "program/platform.h"

// Temporarily disables some warnings that Bullet Physics headers tends to trigger.
#define IMP_PHYSICS_BULLET_HEADERS_BEGIN \
    IMP_DIAGNOSTICS_PUSH \
    IMP_PLATFORM_IF(gcc_clang) \
    ( \
        IMP_DIAGNOSTICS_IGNORE("-Wextra-semi") \
    )

// Re-enables the warnings.
#define IMP_PHYSICS_BULLET_HEADERS_END \
    IMP_DIAGNOSTICS_POP

IMP_PHYSICS_BULLET_HEADERS_BEGIN
#include <bullet/LinearMath/btIDebugDraw.h>
#include <bullet/LinearMath/btScalar.h>
#include <bullet/LinearMath/btVector3.h>
IMP_PHYSICS_BULLET_HEADERS_END

#include "utils/mat.h"

namespace Physics
{
    // `float` or `double`, depending on how Bullet was built.
    using Scalar = btScalar;

    // `fvec3` or `dvec3`, depending on how Bullet was built.
    using Vector3 = vec3<btScalar>;

    // A debug renderer interface.
    using BasicDebugRenderer = btIDebugDraw;
}

// Touch customization points of `mat.h` to allow conversions between `btVector3` and our native vectors.
namespace Math::Custom
{
    template <typename T>
    struct Convert<btVector3, vec3<T>>
    {
        vec3<T> operator()(const btVector3 &v) const
        {
            return vec3<T>(v.x(), v.y(), v.z());
        }
    };

    template <typename T>
    struct Convert<vec3<T>, btVector3>
    {
        btVector3 operator()(const vec3<T> &v) const
        {
            return btVector3(v.x, v.y, v.z);
        }
    };
}
