#include "main.h"

#include "entities.h"

struct MouseCamera : Camera, Tickable
{
    void Tick() override
    {
        pos = mouse.pos();
    }
};

struct TestEntity : Tickable, Renderable
{
    IMP_STANDALONE_COMPONENT(Game)

    float angle = 0;

    void Tick() override
    {
        ImGui::ShowDemoWindow();

        angle += 0.01;

        if (mouse.right.pressed())
            audio.Play("test_sound"_sound, mouse.pos()); // Use `Audio::GlobalData::Sound()` instead of `_sound` for more customization.
    }

    void Render() const override
    {
        r.iquad(game.get<Camera>()->pos, "dummy"_image).center().rotate(angle);
        r.iquad(game.get<Camera>()->pos.rect_size(32)).center().rotate(angle).color(mouse.left.down() ? fvec3(1,0.5,0) : fvec3(0,0.5,1));
        r.itext(game.get<Camera>()->pos, Graphics::Text(Fonts::main, STR((audio.ActiveSources())))).color(fvec3(1));
    }
};

namespace States
{
    STRUCT( World EXTENDS StateBase )
    {
        MEMBERS()

        void Init() override
        {
            // Configure the audio.
            float audio_distance = screen_size.x * 3;
            Audio::ListenerPosition(fvec3(0, 0, -audio_distance));
            Audio::ListenerOrientation(fvec3(0,0,1), fvec3(0,-1,0));
            Audio::Source::DefaultRefDistance(audio_distance);

            // Entities.
            game = nullptr;

            game.create<MouseCamera>();
            game.create<TestEntity>();
        }

        void Tick(std::string &next_state) override
        {
            (void)next_state;

            for (auto &e : game.get<AllTickable>())
                e.get<Tickable>().Tick();
        }

        void Render() const override
        {
            Graphics::SetClearColor(fvec3(0));
            Graphics::Clear();

            r.BindShader();

            for (auto &e : game.get<AllRenderable>())
                e.get<Renderable>().Render();

            r.Finish();
        }
    };
}
