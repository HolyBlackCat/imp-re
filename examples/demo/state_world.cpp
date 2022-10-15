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
            {
                static Audio::Buffer buf = []{
                    int16_t array[10000];
                    for (size_t i = 0; i < std::size(array); i++)
                        array[i] = std::sin(i / 30.f) * 0x7fff;
                    return Audio::Sound(48000, Audio::mono, std::size(array), array);
                }();
                src = audio.manager.Add(buf);
                src->play();
            }

            /* This requires `assets/assets/sounds/foo.wav` (sic) to exist. The directory can be changed in `main.cpp`.
            if (mouse.left.pressed())
                audio.play<"foo">(mouse.pos(), 0.3);
            */
        }

        void Render() const override
        {
            Graphics::SetClearColor(fvec3(0));
            Graphics::Clear();

            r.BindShader();

            r.iquad(mouse.pos(), ivec2(32)).center().rotate(angle).color(mouse.left.down() ? fvec3(1,0.5,0) : fvec3(0,0.5,1));
            r.itext(mouse.pos(), Graphics::Text(Fonts::main, STR((audio.manager.ActiveSources())))).color(fvec3(1));

            r.Finish();
        }
    };
}
