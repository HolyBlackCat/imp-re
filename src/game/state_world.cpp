#include "gameutils/tiled_map.h"
#include "geometry/edges_to_polygons.h"
#include "geometry/tiles_to_edges.h"
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
        wall_slope_a, // |/
        wall_slope_b, // \|
        wall_slope_c, // /|
        wall_slope_d, // |\ .
        _count,
    };

    // A chunk stores a grid of those.
    struct Cell
    {
        Tile tile{};
    };

    // Those must be synced with `baked_tileset`.
    enum class CellShape
    {
        empty,
        full,
        slope_a, // |/
        slope_b, // \|
        slope_c, // /|
        slope_d, // |\ .
    };

    [[nodiscard]] static CellShape GetCellShape(const Cell &cell)
    {
        switch (cell.tile)
        {
            case Tile::empty:        return CellShape::empty;
            case Tile::wall:         return CellShape::full;
            case Tile::wall_slope_a: return CellShape::slope_a;
            case Tile::wall_slope_b: return CellShape::slope_b;
            case Tile::wall_slope_c: return CellShape::slope_c;
            case Tile::wall_slope_d: return CellShape::slope_d;
            case Tile::_count: break; // Should be unreachable.
        }
        ASSERT(false, "Must specify shape for this cell.");
        return CellShape::empty;
    }

    static Geom::TilesToEdges::BakedTileset baked_tileset = []{
        Geom::TilesToEdges::Tileset tileset;
        tileset.tile_size = ivec2(1,1);
        tileset.vertices = {ivec2(0,0), ivec2(1,0), ivec2(1,1), ivec2(0,1)};
        tileset.tiles = {
            // Those must be synced with `CellShape`.
            {},           // Empty.
            {{0,1,2,3}},  // Box.
            {{0,1,3}},    // |/
            {{0,1,2}},    // \|
            {{1,2,3}},    // /|
            {{0,2,3}},    // |\ .
        };
        return Geom::TilesToEdges::BakedTileset(std::move(tileset));
    }();

    struct ChunkComponentCollider
    {
        struct Polygon
        {
            b2Hull points{};

            b2::Shape shape;
        };
        std::vector<Polygon> polygons;
    };

    // The entity for tile grids.
    struct GridEntity : Meta::with_virtual_destructor<GridEntity>
    {
        IMP_STANDALONE_COMPONENT(Game)

        TileGrids::ChunkGrid<System, chunk_size, Cell, ChunkComponentCollider> grid;
        b2::Body body;

        void LoadTiles(Stream::ReadOnlyData data);
    };

    // Traits class for `tile_grids/high_level.h`. We wrap it in `EntityHighLevelTraits` below instead of using directly.
    struct BasicHighLevelTraits
    {
        // This part is for `tile_grids/entities.h`, for the `EntityHighLevelTraits` wrapper.

        // The tag for the entity system.
        using EntityTag = Game;
        // The entity that will store the grid.
        using GridEntity = Tiles::GridEntity;

        // This is called when splitting a grid to copy the basic parameters.
        static void FinishGridInitAfterSplit(EntityTag::Controller& controller, const GridEntity *from, GridEntity *to)
        {
            (void)controller;
            (void)from;

            to->body = controller.get<PhysicsWorld>()->w.CreateBody(b2::OwningHandle, b2::Body::Params{});
        }

        // This part is directly for `tile_grids/high_level.h`:

        static void OnUpdateGridChunkContents(EntityTag::Controller& controller, GridEntity *grid, vec2<System::WholeChunkCoord> chunk_coord, const System::TileComponentIndices<chunk_size> &comps_per_tile)
        {
            (void)controller;

            static b2::Shape::Params shape_params = []{
                b2::Shape::Params shape_params;
                return shape_params;
            }();

            fvec2 chunk_base_offset = fvec2(chunk_coord) * chunk_size;

            auto *chunk = grid->grid.GetChunk(chunk_coord);

            struct CompEntry
            {
                Geom::EdgesToPolygons::TriangulationInput<int> tri_input;
                decltype(tri_input.InsertionCallback()) callback = tri_input.InsertionCallback();

                CompEntry() {}
                CompEntry(const CompEntry &) = delete;
                CompEntry &operator=(const CompEntry &) = delete;
            };

            // One triangulation input per component.
            std::vector<CompEntry> tri_inputs(chunk->GetComponents().components.size());

            System::ComponentIndex cur_comp = System::ComponentIndex::invalid;
            System::ComponentIndex next_comp = System::ComponentIndex::invalid;

            Geom::TilesToEdges::ConvertTilesToEdges(
                baked_tileset, Geom::TilesToEdges::Mode::closed, ivec2(chunk_size),
                // Tile input.
                [&](ivec2 pos)
                {
                    // The return values are indices into `baked_tileset`.
                    return std::to_underlying(GetCellShape(chunk->GetChunk().at(pos)));
                },
                // Point output.
                [&](ivec2 pos, Geom::PointInfo info)
                {
                    tri_inputs[std::to_underlying(cur_comp)].callback(pos, info);

                    if (info.type == Geom::PointType::last)
                    {
                        cur_comp = next_comp;
                        next_comp = System::ComponentIndex::invalid;
                    }
                },
                // Starting tile output for each edge loop.
                [&](ivec2 pos, Geom::TilesToEdges::BakedTileset::EdgeId edge)
                {
                    (void)edge;
                    System::ComponentIndex comp = comps_per_tile.at(pos);
                    ASSERT(comp != System::ComponentIndex::invalid, "Why does an edge loop start from an empty cell?");
                    ASSERT(next_comp == System::ComponentIndex::invalid, "Too many 'edge loop begins' events?");
                    if (cur_comp == System::ComponentIndex::invalid)
                        cur_comp = comp; // First edge loop.
                    else
                        next_comp = comp; // Subsequent loops.
                }
            );

            for (std::size_t i = 0; i < chunk->GetComponents().components.size(); i++)
            {
                ChunkComponentCollider &coll = chunk->GetMutComponentExtraData(System::ComponentIndex(i));
                coll.polygons.clear(); // This destroys the existing shapes too.

                b2Hull new_hull{};

                Geom::EdgesToPolygons::ConvertToPolygons<float>(
                    tri_inputs[i].tri_input, b2_maxPolygonVertices,
                    [&](ivec2 pos, Geom::PointInfo info)
                    {
                        if (info.type == Geom::PointType::last)
                        {
                            ChunkComponentCollider::Polygon &new_poly = coll.polygons.emplace_back();
                            new_poly.points = new_hull;

                            grid->body.CreateShape(b2::DestroyWithParent, shape_params, b2MakePolygon(&new_hull, 0));
                            new_hull.count = 0;
                        }
                        else
                        {
                            assert(new_hull.count < b2_maxPolygonVertices);
                            new_hull.points[new_hull.count++] = b2Vec2(fvec2(pos) + chunk_base_offset);
                        }
                    }
                );
            }
        }

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
            CellShape shape = GetCellShape(cell);
            switch (shape)
            {
                case CellShape::empty:   return 0;
                case CellShape::full:    return 1;
                case CellShape::slope_a: return std::array{0,0,1,1}[dir];
                case CellShape::slope_b: return std::array{1,0,0,1}[dir];
                case CellShape::slope_c: return std::array{1,1,0,0}[dir];
                case CellShape::slope_d: return std::array{0,1,1,0}[dir];
            }
            ASSERT(false, "Invalid tile shape enum.");
            return 0;
        }

        // Returns our data from a grid.
        [[nodiscard]] static TileGrids::ChunkGrid<System, chunk_size, CellType, ChunkComponentCollider> &GridToData(GridEntity *grid)
        {
            return grid->grid;
        }
    };
    using HighLevelTraits = TileGrids::EntityHighLevelTraits<BasicHighLevelTraits>;

    // Stores lists of dirty chunks across entities.
    // We have a separate entity that stores those, see `DirtyListsEntity` below.
    using DirtyLists = TileGrids::DirtyChunkLists<System, HighLevelTraits>;

    // This entity stores dirty chunk lists across other entities.
    struct DirtyListsEntity
    {
        IMP_STANDALONE_COMPONENT(Game)

        // The dirty chunk lists.
        DirtyLists dirty;

        // This is used when updating chunks. We're reusing this across frames to reduce heap allocations/deallocations,
        // but nothing is stopping you from creating it per frame as needed.
        DirtyLists::ReusedUpdateData<chunk_size> reused_update_data;
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
                int tile = tiles.at(pos);
                if (tile < 0 || tile >= int(Tile::_count))
                    throw std::runtime_error(FMT("Unknown tile at {}", pos));
                cell.tile = Tile(tile);
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

        d.dirty.Update(game, d.reused_update_data);
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
