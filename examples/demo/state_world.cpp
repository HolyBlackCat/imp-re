#include "main.h"

namespace States
{
    STRUCT( World EXTENDS StateBase )
    {
        MEMBERS()

        float angle = 0;
        std::shared_ptr<Audio::Source> src;

        void Init() override
        {
            // Configure the audio.
            float audio_distance = screen_size.x * 3;
            Audio::ListenerPosition(fvec3(0, 0, -audio_distance));
            Audio::ListenerOrientation(fvec3(0,0,1), fvec3(0,-1,0));
            Audio::Source::DefaultRefDistance(audio_distance);
        }

        void Tick(std::string &next_state) override
        {
            (void)next_state;

            angle += 0.01;
            ImGui::ShowDemoWindow();

            if (mouse.right.pressed())
                audio.Play("test_sound"_sound, mouse.pos()); // Use `Audio::GlobalData::File()` instead of `_sound` for more customization.
        }

        void Render() const override
        {
            Graphics::SetClearColor(fvec3(0));
            Graphics::Clear();

            r.BindShader();

            r.iquad(mouse.pos(), ivec2(32)).center().rotate(angle).color(mouse.left.down() ? fvec3(1,0.5,0) : fvec3(0,0.5,1));
            r.itext(mouse.pos(), Graphics::Text(Fonts::main, STR((audio.ActiveSources())))).color(fvec3(1));

            r.Finish();
        }
    };
}
