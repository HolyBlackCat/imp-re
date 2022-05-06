#include "utils/aabb_tree.h"

// A demo of our dynamic AABB tree, copied from box2d.

// Uncomment this to use the box2d implementation instead of ours, as a reference.
// #define DEMO_USE_BOX2D_TREE

// If you see jittering when creating 4 objects and moving one, that seems to be normal, box2d does this too.
// In our tree it can be fixed by setting the parameter `balance_threshold` to `2`, but it's unclear if it's actually good for performance.

IMP_DIAGNOSTICS_PUSH
IMP_DIAGNOSTICS_IGNORE("-Wkeyword-macro")
#define private public
#define protected public
#include <box2d/b2_dynamic_tree.h>
#undef public
#undef public
IMP_DIAGNOSTICS_POP

#include "physics_2d/math_adapters.h"

const ivec2 screen_size = ivec2(480, 270);
const std::string_view window_name = "Iota";

Interface::Window window(std::string(window_name), screen_size * 2, Interface::windowed, adjust_(Interface::WindowSettings{}, min_size = screen_size));
static Graphics::DummyVertexArray dummy_vao = nullptr;

Audio::Context audio_context = nullptr;
Audio::SourceManager audio_controller;

const Graphics::ShaderConfig shader_config = Graphics::ShaderConfig::Core();
Interface::ImGuiController gui_controller(Poly::derived<Interface::ImGuiController::GraphicsBackend_Modern>, adjust_(Interface::ImGuiController::Config{}, shader_header = shader_config.common_header, store_state_in_file = {}));

namespace Fonts
{
    namespace Files
    {
        Graphics::FontFile main(Program::ExeDir() + "assets/Monocat_6x12.ttf", 12);
    }
    Graphics::Font main;
}

Graphics::TextureAtlas texture_atlas = []{
    // Don't generate a new atlas in prod.
    std::string source_dir = IMP_PLATFORM_IF_NOT(prod)("assets/_images") "";
    // Look for the atlas relative to the exe in prod, and relative to the project root otherwise.
    std::string target_prefix = IMP_PLATFORM_IF(prod)(Program::ExeDir() + "assets/") IMP_PLATFORM_IF_NOT(prod)("assets/assets/");
    Graphics::TextureAtlas ret(ivec2(2048), source_dir, target_prefix + "atlas.png", target_prefix + "atlas.refl", {{"/font_storage", ivec2(256)}});
    auto font_region = ret.Get("/font_storage");

    Unicode::CharSet glyph_ranges;
    glyph_ranges.Add(Unicode::Ranges::Basic_Latin);

    Graphics::MakeFontAtlas(ret.GetImage(), font_region.pos, font_region.size, {
        {Fonts::main, Fonts::Files::main, glyph_ranges, Graphics::FontFile::monochrome_with_hinting},
    });
    return ret;
}();
Graphics::Texture texture_main = Graphics::Texture(nullptr).Wrap(Graphics::clamp).Interpolation(Graphics::nearest).SetData(texture_atlas.GetImage());

GameUtils::AdaptiveViewport adaptive_viewport(shader_config, screen_size);
Render r = adjust_(Render(0x2000, shader_config), SetTexture(texture_main), SetMatrix(adaptive_viewport.GetDetails().MatrixCentered()));

Input::Mouse mouse;

Random::DefaultGenerator random_generator = Random::MakeGeneratorFromRandomDevice();
Random::DefaultInterfaces<Random::DefaultGenerator> ra(random_generator);

struct AabbTreeDemo
{
    #ifndef DEMO_USE_BOX2D_TREE
    AabbTree<ivec2, std::string> tree = AabbTree<ivec2, std::string>::Params(ivec2(4));
    int object_counter = 0;
    #else
    b2DynamicTree tree;
    #endif

    struct Object
    {
        ivec2 a, b;
        fvec3 color;
        int tree_node = 0;
    };
    std::vector<Object> objects;

    bool editing_object = false;
    std::size_t moved_obj_index = -1zu;

    void Tick()
    {
        // Create new objects.
        if (mouse.left.pressed() && !editing_object && moved_obj_index == -1zu)
        {
            objects.emplace_back();
            objects.back().color = ra.fvec3 <= 1;
            objects.back().a = mouse.pos();
            editing_object = true;
        }
        if (editing_object)
        {
            objects.back().b = mouse.pos();

            if (mouse.left.released())
            {
                editing_object = false;
                sort_two_var(objects.back().a, objects.back().b);
                if (objects.back().b(any) <= objects.back().a)
                {
                    objects.pop_back();
                }
                else
                {
                    #ifndef DEMO_USE_BOX2D_TREE
                    objects.back().tree_node = tree.AddNode({objects.back().a, objects.back().b}, std::string(1, 'A' + object_counter++));
                    #else
                    objects.back().tree_node = tree.CreateProxy({b2Vec2(fvec2(objects.back().a)), b2Vec2(fvec2(objects.back().b))}, nullptr);
                    #endif
                }

                // std::cout << tree.DebugToString() << '\n';
            }
        }

        // Destroy objects.
        if (!editing_object && moved_obj_index == -1zu && mouse.middle.released())
        {
            auto it = std::find_if(objects.rbegin(), objects.rend(), [&](const Object &obj){return obj.a(all) < mouse.pos() && obj.b(all) >= mouse.pos();});
            if (it != objects.rend())
            {
                auto regular_it = it.base() - 1;
                #ifndef DEMO_USE_BOX2D_TREE
                tree.RemoveNode(regular_it->tree_node);
                #else
                tree.RemoveLeaf(regular_it->tree_node);
                #endif
                objects.erase(regular_it);

                // std::cout << tree.DebugToString() << '\n';
            }
        }

        // Move objects.
        if (!editing_object && moved_obj_index == -1zu && mouse.right.pressed())
        {
            auto it = std::find_if(objects.rbegin(), objects.rend(), [&](const Object &obj){return obj.a(all) < mouse.pos() && obj.b(all) >= mouse.pos();});
            if (it != objects.rend())
            {
                moved_obj_index = it.base() - 1 - objects.begin();
            }
        }
        else if (moved_obj_index != -1zu)
        {
            Object &obj = objects[moved_obj_index];
            obj.a += mouse.pos_delta();
            obj.b += mouse.pos_delta();
            #ifndef DEMO_USE_BOX2D_TREE
            tree.ModifyNode(obj.tree_node, {obj.a, obj.b}, mouse.pos_delta());
            #else
            tree.MoveProxy(obj.tree_node, {b2Vec2(fvec2(obj.a)), b2Vec2(fvec2(obj.b))}, b2Vec2(fvec2(mouse.pos_delta()) * 0.4f));
            #endif
        }
        if (moved_obj_index != -1zu && mouse.right.released())
            moved_obj_index = -1;
    }

    void Render() const
    {
        // Objects.
        for (const Object &obj : objects)
            r.iquad(obj.a, obj.b).absolute().color(obj.color).beta(0.5);

        { // AABB tree.
            fvec3 color(1,1,1);
            float alpha = 0.7;
            float text_alpha = 0.7;
            #ifndef DEMO_USE_BOX2D_TREE
            for (int i = 0; i < tree.Nodes().ElemCount(); i++)
            {
                auto aabb = tree.GetNodeAabb(tree.Nodes().GetElem(i));

                // Top.
                r.iquad(aabb.a-1, ivec2(aabb.b.x - aabb.a.x + 2, 1)).color(color).alpha(alpha);
                // Bottom
                r.iquad(ivec2(aabb.a.x - 1, aabb.b.y), ivec2(aabb.b.x - aabb.a.x + 2, 1)).color(color).alpha(alpha);
                // Left.
                r.iquad(aabb.a with(x -= 1), ivec2(1, aabb.b.y - aabb.a.y)).color(color).alpha(alpha);
                // Right.
                r.iquad(ivec2(aabb.b.x, aabb.a.y), ivec2(1, aabb.b.y - aabb.a.y)).color(color).alpha(alpha);

                r.itext((aabb.a + aabb.b) / 2, Graphics::Text(Fonts::main, FMT("{}", tree.Nodes().GetElem(i)))).color(color).alpha(text_alpha);

                r.itext(aabb.a + ivec2(1, -1), Graphics::Text(Fonts::main, tree.GetNodeUserData(tree.Nodes().GetElem(i)))).align(ivec2(-1)).color(color).alpha(text_alpha);
            }
            #else
            auto lambda = [&](auto &lambda, int node) -> void
            {
                ivec2 a = fvec2(tree.m_nodes[node].aabb.lowerBound);
                ivec2 b = fvec2(tree.m_nodes[node].aabb.upperBound);

                // Top.
                r.iquad(a-1, ivec2(b.x - a.x + 2, 1)).color(color).alpha(alpha);
                // Bottom
                r.iquad(ivec2(a.x - 1, b.y), ivec2(b.x - a.x + 2, 1)).color(color).alpha(alpha);
                // Left.
                r.iquad(a with(x -= 1), ivec2(1, b.y - a.y)).color(color).alpha(alpha);
                // Right.
                r.iquad(ivec2(b.x, a.y), ivec2(1, b.y - a.y)).color(color).alpha(alpha);

                r.itext((a + b) / 2, Graphics::Text(Fonts::main, FMT("{}", node))).color(color).alpha(text_alpha);

                if (!tree.m_nodes[node].IsLeaf())
                {
                    lambda(lambda, tree.m_nodes[node].child1);
                    lambda(lambda, tree.m_nodes[node].child2);
                }
            };
            if (tree.m_root != b2_nullNode)
                lambda(lambda, tree.m_root);
            #endif
        }

        if (!editing_object && moved_obj_index == -1zu)
        {
            { // Rect collision.
                ivec2 a = mouse.pos() - ivec2(10,5);
                ivec2 b = mouse.pos() + ivec2(15,20);
                #ifndef DEMO_USE_BOX2D_TREE
                bool hit = tree.CollideAabb({a, b}, [](int node){(void)node; return true;});
                #else
                bool hit = false;
                struct Callback
                {
                    bool &hit;
                    bool QueryCallback(std::uint32_t)
                    {
                        hit = true;
                        return false;
                    }
                };
                Callback callback{hit};
                tree.Query(&callback, {b2Vec2(fvec2(a)), b2Vec2(fvec2(b))});
                #endif
                fvec3 color = hit ? fvec3(1,0,0) : fvec3(0,1,0);
                float alpha = 0.4;
                r.iquad(a, b).absolute().color(color).alpha(alpha);
            }

            { // Point collision.
                ivec2 point = mouse.pos();
                #ifndef DEMO_USE_BOX2D_TREE
                bool hit = tree.CollidePoint(point, [](int node){(void)node; return true;});
                #else
                bool hit = false;
                struct Callback
                {
                    bool &hit;
                    bool QueryCallback(std::uint32_t)
                    {
                        hit = true;
                        return false;
                    }
                };
                Callback callback{hit};
                tree.Query(&callback, {b2Vec2(fvec2(point)), b2Vec2(fvec2(point))});
                #endif
                fvec3 color = hit ? fvec3(1,0,0) : fvec3(0,1,0);
                float alpha = 0.8;
                int len = 10;
                r.iquad(point with(y -= len), ivec2(1, len*2+1)).color(color).alpha(alpha);
                r.iquad(point with(x -= len), ivec2(len*2+1, 1)).color(color).alpha(alpha);
            }
        }
    }
};

struct Application : Program::DefaultBasicState
{
    GameUtils::FpsCounter fps_counter;

    AabbTreeDemo demo;

    void Resize()
    {
        adaptive_viewport.Update();
        mouse.SetMatrix(adaptive_viewport.GetDetails().MouseMatrixCentered());
    }

    Metronome metronome = Metronome(60);

    Metronome *GetTickMetronome() override
    {
        return &metronome;
    }

    int GetFpsCap() override
    {
        return 60 * NeedFpsCap();
    }

    void EndFrame() override
    {
        fps_counter.Update();
        window.SetTitle(STR((window_name), " TPS:", (fps_counter.Tps()), " FPS:", (fps_counter.Fps())));
    }

    void Tick() override
    {
        // window.ProcessEvents();
        window.ProcessEvents({gui_controller.EventHook()});

        if (window.ExitRequested())
            Program::Exit();
        if (window.Resized())
        {
            Resize();
            Graphics::Viewport(window.Size());
        }

        gui_controller.PreTick();
        demo.Tick();
        audio_controller.Tick();

        Audio::CheckErrors();
    }

    void Render() override
    {
        gui_controller.PreRender();
        adaptive_viewport.BeginFrame();
        Graphics::SetClearColor(fvec3(0));
        Graphics::Clear();
        r.BindShader();
        demo.Render();
        r.Finish();
        adaptive_viewport.FinishFrame();
        gui_controller.PostRender();
        Graphics::CheckErrors();

        window.SwapBuffers();
    }


    void Init()
    {
        ImGui::StyleColorsDark();

        // Load various small fonts
        auto monochrome_font_flags = ImGuiFreeTypeBuilderFlags_Monochrome | ImGuiFreeTypeBuilderFlags_MonoHinting;

        gui_controller.LoadFont(Program::ExeDir() + "assets/Monocat_6x12.ttf", 12.0f, adjust(ImFontConfig{}, FontBuilderFlags = monochrome_font_flags));
        gui_controller.LoadDefaultFont();
        gui_controller.RenderFontsWithFreetype();

        Graphics::Blending::Enable();
        Graphics::Blending::FuncNormalPre();
    }
};

IMP_MAIN(,)
{
    Application app;
    app.Init();
    app.Resize();
    app.RunMainLoop();
    return 0;
}
