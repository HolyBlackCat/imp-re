#include "gameutils/tiled_map.h"
#include "main.h"

#include "entities.h"
#include "tile_grids/chunk.h"
#include "utils/json.h"
#include "utils/ring_multiarray.h"
#include "box2d_physics/math_adapters.h"

#include <box2cpp/box2c.hpp>
#include <box2cpp/debug_imgui_renderer.hpp>

struct MouseCamera : Camera, Tickable
{
    void Tick() override
    {
        pos = mouse.pos();
    }
};

namespace Meta
{
    template <typename...>
    struct UniqueEmptyType {};

    template <typename T, typename Tag = value_list<>>
    struct
}

struct TestEntity : Tickable, Renderable
{
    IMP_STANDALONE_COMPONENT(Game)

    b2::World w;

    b2::DebugImguiRenderer renderer;

    struct Cell
    {
        int value = 0;
    };

    TestEntity()
    {
        w = adjust(b2::World::Params{}, .gravity.y *= -1);
        renderer.camera_pos.x = 2;
        renderer.camera_pos.y = 1;

        Json json(Stream::ReadOnlyData(Program::ExeDir() + "assets/map.json").string(), 64);
        auto layer = Tiled::LoadTileLayer(Tiled::FindLayer(json.GetView(), "mid"));

        using Chunk = TileGrids::Chunk<10, Cell, int>;
        Chunk chunk;
        for (ivec2 pos : vector_range(ivec2(10)))
        {
            chunk.at(pos).value = layer.at(pos);
        }

        Chunk::ConnectedComponentsReusedData reused;
        Chunk::ComponentsType comps;
        Chunk::ComponentsType::ComponentType comp;
        auto func_tile_exists = [](const Cell &c){return c.value != 0;};
        auto func_tile_conn = [](const Cell &c, int i) {return (Chunk::ComponentsType::TileEdgeConnectivity)bool((std::array{0b0000,0b1111,0b1100,0b0110,0b0011,0b1001}[c.value] << i) & 0b1000);};
        chunk.ComputeConnectedComponents(reused, comp, []{}, func_tile_exists, func_tile_conn);
        chunk.ComputeConnectedComponents(reused, comps, []{}, func_tile_exists, func_tile_conn);

        comps.RemoveComponent(TileGrids::ComponentIndex(0));

        TileGrids::ChunkGridSplitter<int> f;
        f.visited_components.insert({});

        std::cout << &comps << '\n';
    }

    void Tick() override
    {
        ImGui::ShowDemoWindow();

        w.Step(1/60.f, 4);

        renderer.DrawShapes(w);
        renderer.DrawModeToggles();
        renderer.MouseDrag(w);
    }

    void Render() const override
    {

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
