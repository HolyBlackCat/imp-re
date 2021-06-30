#include "debug_renderer.h"

#include <iostream>

#include "graphics/shader.h"
#include "graphics/simple_render_queue.h"
#include "reflection/full.h"
#include "strings/common.h"

namespace Physics
{
    namespace ShaderSources
    {
        static constexpr const char
            *vert = R"(
                varying vec3 v_color;
                void main()
                {
                    gl_Position = u_matrix * vec4(a_pos, 1);
                    v_color = a_color;
                }
            )",
            *frag = R"(
                varying vec3 v_color;
                void main()
                {
                    gl_FragColor = vec4(v_color, 1);
                }
            )";
    }

    struct DebugRenderer::Data
    {
        int mode = 0;

        fmat4 mat_view, mat_proj;
        bool matrices_dirty = true;

        REFL_SIMPLE_STRUCT( Attrib
            REFL_DECL(fvec3) pos
            REFL_DECL(fvec3) color
        )

        REFL_SIMPLE_STRUCT( Uniforms
            REFL_DECL(Graphics::Uniform<fmat4> REFL_ATTR Graphics::Vert) matrix
        )
        Uniforms uniforms;

        Graphics::SimpleRenderQueue<Attrib, 2> queue;
        Graphics::Shader shader;


        Data(const Graphics::ShaderConfig &cfg, std::size_t queue_size)
            : queue(queue_size), shader("Debug renderer for Bullet Physics", cfg, {}, Meta::tag<Attrib>{}, uniforms, ShaderSources::vert, ShaderSources::frag)
        {}

        Data(const Data &) = delete;
        Data &operator=(const Data &) = delete;
    };

    DebugRenderer::DebugRenderer() {}

    DebugRenderer::DebugRenderer(const Graphics::ShaderConfig &cfg, std::size_t queue_size)
        : data(std::make_unique<Data>(cfg, queue_size))
    {}

    DebugRenderer::~DebugRenderer() {}

    void DebugRenderer::SetViewMatrix(fmat4 matrix)
    {
        data->mat_view = matrix;
        data->matrices_dirty = true;
    }

    void DebugRenderer::SetProjMatrix(fmat4 matrix)
    {
        data->mat_proj = matrix;
        data->matrices_dirty = true;
    }

    int DebugRenderer::getDebugMode() const
    {
        return data->mode;
    }

    void DebugRenderer::setDebugMode(int new_mode)
    {
        data->mode = new_mode;
    }

    void DebugRenderer::clearLines()
    {
        // This function is called each frame before any rendering is done.

        data->shader.Bind();

        if (data->matrices_dirty)
        {
            data->matrices_dirty = true;
            data->uniforms.matrix = data->mat_proj * data->mat_view;
        }
    }

    void DebugRenderer::flushLines()
    {
        // This function is called each frame after all rendering is done.

        data->queue.Flush();
    }

    void DebugRenderer::reportErrorWarning(const char *message)
    {
        Strings::Split(message, '\n', [](std::string_view line, bool is_final)
        {
            if (is_final && line.empty())
                return;
            std::clog << "BULLET: " << line << '\n';
        });
    }

    void DebugRenderer::drawLine(const btVector3 &from, const btVector3 &to, const btVector3 &color)
    {
        drawLine(from, to, color, color);
    }

    void DebugRenderer::drawLine(const btVector3 &from, const btVector3 &to, const btVector3 &from_color, const btVector3 &to_color)
    {
        data->queue.Add({fvec3(from), fvec3(from_color)}, {fvec3(to), fvec3(to_color)});
    }

    void DebugRenderer::drawContactPoint(const btVector3 &point, const btVector3 &normal, btScalar distance, int lifetime, const btVector3 &color)
    {
        (void)lifetime;
        drawLine(point, point + normal.normalized() * distance, color);
    }

    void DebugRenderer::draw3dText(const btVector3 &location, const char *textString)
    {
        (void)location;
        (void)textString;
    }
}
