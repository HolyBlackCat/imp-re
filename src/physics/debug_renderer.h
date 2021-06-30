#pragma once

#include <cstddef>
#include <memory>

#include "graphics/shader.h"
#include "physics/common.h"
#include "utils/mat.h"

IMP_PHYSICS_BULLET_HEADERS_BEGIN
#include <bullet/LinearMath/btIDebugDraw.h>
IMP_PHYSICS_BULLET_HEADERS_END

namespace Physics
{
    class DebugRenderer : public btIDebugDraw
    {
        struct Data;
        std::unique_ptr<Data> data;

      public:
        DebugRenderer();
        DebugRenderer(const Graphics::ShaderConfig &cfg, std::size_t queue_size = 0x2000);
        ~DebugRenderer();

        [[nodiscard]] explicit operator bool() const {return bool(data);}

        void SetViewMatrix(fmat4 matrix);
        void SetProjMatrix(fmat4 matrix);

        int getDebugMode() const override;
        void setDebugMode(int new_mode) override;

        void clearLines() override;
        void flushLines() override;

        void reportErrorWarning(const char *message) override;

        void drawLine(const btVector3 &from, const btVector3 &to, const btVector3 &color) override;
        void drawLine(const btVector3 &from, const btVector3 &to, const btVector3 &from_color, const btVector3 &to_color) override;

        void drawContactPoint(const btVector3 &point, const btVector3 &normal, btScalar distance, int lifetime, const btVector3 &color) override;
        void draw3dText(const btVector3 &location, const char *textString) override;
    };
}
