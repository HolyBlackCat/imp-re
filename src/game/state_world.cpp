#include "gameutils/tiled_map.h"
#include "main.h"

#include "entities.h"
#include "tile_grids/core.h"
#include "tile_grids/debug_rendering.h"
#include "tile_grids/entities.h"
#include "tile_grids/high_level.h"
#include "utils/json.h"
#include "utils/ring_multiarray.h"
#include "box2d_physics/math_adapters.h"

#include <box2cpp/box2c.hpp>
#include <box2cpp/debug_imgui_renderer.hpp>

struct PhysicsWorld : Tickable
{
    IMP_STANDALONE_COMPONENT(Game)

    b2::World w;

    b2::DebugImguiRenderer renderer;

    PhysicsWorld()
    {
        w = adjust(b2::World::Params{}, .gravity.y *= -1);
        renderer.camera_pos.x = 12;
        renderer.camera_pos.y = 8;
        renderer.camera_scale = 28;
    }

    void Tick() override
    {
        w.Step(1/60.f, 4);

        renderer.DrawShapes(w);
        renderer.DrawModeToggles();
        renderer.MouseDrag(w);
    }
};

namespace Tiles
{
    using System = TileGrids::System<TileGrids::DefaultSystemTraits>;

    static constexpr int chunk_size = 8;

    enum class Tile
    {
        empty,
        wall,
    };

    struct Cell
    {
        Tile tile{};
    };

    struct GridEntity : Meta::with_virtual_destructor<GridEntity>
    {
        IMP_STANDALONE_COMPONENT(Game)

        TileGrids::ChunkGrid<System, chunk_size, Cell> grid;
        b2::Body body;

        void LoadTiles(Stream::ReadOnlyData data);
    };

    struct BasicHighLevelTraits
    {
        // For `tile_grids/entities.h`:

        // The tag for the entity system.
        using EntityTag = Game;
        // The entity that will store the grid.
        using GridEntity = Tiles::GridEntity;

        // This is called when splitting a grid to copy the basic parameters.
        static void FinishGridInitAfterSplit(typename EntityTag::Controller& world, const GridEntity &from, GridEntity &to)
        {
            (void)world;
            (void)from;

            to.body = world.get<PhysicsWorld>()->w.CreateBody(b2::OwningHandle, b2::Body::Params{});
        }

        // For `tile_grids/high_level.h`:

        // Each tile of a chunk stores this.
        using CellType = Cell;
        // Returns true if the cell isn't empty, for the purposes of splitting unconnected grids. Default-constructed cells must count as empty.
        [[nodiscard]] static bool CellIsNonEmpty(const CellType &cell)
        {
            return cell.tile != Tile::empty;
        }
        // Returns the connectivity mask of a cell in the specified direction, for the purposes of splitting unconnected grids.
        // The bit order should NOT be reversed when flipping direction, it's always the same.
        [[nodiscard]] static System::TileEdgeConnectivity CellConnectivity(const CellType &cell, int dir)
        {
            (void)dir;
            switch (cell.tile)
            {
              case Tile::empty:
                return 0;
              case Tile::wall:
                return 1;
            }
            ASSERT(false, "Invalid tile enum.");
            return 0;
        }

        // Returns our data from a grid.
        [[nodiscard]] static TileGrids::ChunkGrid<System, chunk_size, CellType> &GridToData(GridEntity &grid)
        {
            return grid.grid;
        }
    };
    using HighLevelTraits = TileGrids::EntityHighLevelTraits<BasicHighLevelTraits>;

    using DirtyLists = TileGrids::DirtyChunkLists<System, HighLevelTraits>;

    struct DirtyListsEntity
    {
        IMP_STANDALONE_COMPONENT(Game)

        DirtyLists dirty;

        DirtyLists::ReusedUpdateData<chunk_size, Cell> reused_update_data;
    };

    void GridEntity::LoadTiles(Stream::ReadOnlyData data)
    {
        Json json(data.string(), 32);
        auto tiles = Tiled::LoadTileLayer(Tiled::FindLayer(json, "mid"));

        grid.LoadFromArray(
            game,
            &game.get<DirtyListsEntity>()->dirty,
            dynamic_cast<Game::Entity &>(*this).id(),
            tiles.size().to<System::GlobalTileCoord>(),
            [&](vec2<System::GlobalTileCoord> pos, Cell &cell)
            {
                cell.tile = tiles.at(pos) ? Tile::wall : Tile::empty;
            }
        );
    }
}

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

    int active_entity_index = -1;

    TestEntity()
    {
        auto &w = game.create<PhysicsWorld>();
        auto &d = game.create<Tiles::DirtyListsEntity>();

        auto &e = game.create<Tiles::GridEntity>();
        e.LoadTiles(Program::ExeDir() + "assets/map.json");
        e.body = w.w.CreateBody(b2::OwningHandle, b2::Body::Params{});

        d.dirty.HandleGeometryUpdate(game, d.reused_update_data.comps);
        d.dirty.HandleEdgesUpdateAndSplit(game, d.reused_update_data.edge);
    }

    void Tick() override
    {
        auto &renderer = game.get<PhysicsWorld>()->renderer;

        auto &list = game.get<Game::Category<Ent::OrderedList, Tiles::GridEntity>>();
        if (Input::Button(Input::space).pressed())
        {
            ++active_entity_index;
            if (active_entity_index >= list.size())
                active_entity_index = -1;
        }

        for (int index = 0; const auto &e : list)
        {
            if (index++ != active_entity_index && active_entity_index != -1)
                continue;

            const auto &grid = e.get<Tiles::GridEntity>();
            TileGrids::ImguiDebugDraw(grid.grid, [&](fvec2 pos){return fvec2(renderer.Box2dToImguiPoint(grid.body.GetWorldPoint(pos)));}, *ImGui::GetBackgroundDrawList(), TileGrids::DebugDrawFlags::all);
        }
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
