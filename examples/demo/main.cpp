#include "main.h"

const ivec2 screen_size = ivec2(480, 270);
const std::string_view window_name = "Iota";

Interface::Window window(std::string(window_name), screen_size * 2, Interface::windowed, adjust_(Interface::WindowSettings{}, min_size = screen_size));
static Graphics::DummyVertexArray dummy_vao = nullptr;

Audio::Context audio_context = nullptr;
Audio::SourceManager audio;

const Graphics::ShaderConfig shader_config = Graphics::ShaderConfig::Core();
Interface::ImGuiController gui_controller(Poly::derived<Interface::ImGuiController::GraphicsBackend_Modern>, adjust_(Interface::ImGuiController::Config{}, shader_header = shader_config.common_header, store_state_in_file = {}));

Graphics::FontFile Fonts::Files::main(Program::ExeDir() + "assets/Monocat_6x12.ttf", 12);
Graphics::Font Fonts::main;

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

struct Application : Program::DefaultBasicState
{
    GameUtils::State::Manager<StateBase> state_manager;
    GameUtils::FpsCounter fps_counter;

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
        window.SetTitle(STR((window_name), " TPS:", (fps_counter.Tps()), " FPS:", (fps_counter.Fps()), " SOUNDS:", (audio.ActiveSources())));
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
        state_manager.Tick();
        audio.Tick();

        Audio::CheckErrors();
    }

    void Render() override
    {
        gui_controller.PreRender();
        adaptive_viewport.BeginFrame();
        state_manager.Call(&StateBase::Render);
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

        Audio::GlobalData::Load(Audio::mono, Audio::wav, Program::ExeDir() + "assets/sounds/");

        state_manager.SetState("World{}");
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
