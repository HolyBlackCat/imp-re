#pragma once

#include "macros/enum_flag_operators.h"
#include "program/errors.h"
#include "tile_grids/core.h"
#include "utils/ring_multiarray.h"

#include <bit>
#include <cstdint>
#include <cstdint>
#include <memory>

namespace TileGrids
{
    // The dirty flags stored in each chunk.
    enum class ChunkDirtyFlags : std::uint8_t
    {
        // The chunk was modified, re-split it to connectivitiy components. Users might also want to update the colliders.
        geometry_changed                  = 1 << 0,
        // The chunk was re-split to components, update the edges.
        update_edge_0                     = 1 << 1, // Those 4 values must have this relative order.
        update_edge_1                     = 1 << 2,
        update_edge_2                     = 1 << 3, // For simplicity the edges 2 and 3 are also stored. Both chunks store
        update_edge_3                     = 1 << 4, //   the flag for the edge between them, in case one of them gets deleted.
        update_edge_all                   = update_edge_0 | update_edge_1 | update_edge_2 | update_edge_3,

        _all_bits                        = (1 << 5) - 1,

        // Isn't an actual dirty flag. This is temporarily set by `HandleEdgesUpdateAndSplit()` to mark chunks to split.
        try_splitting_to_components_from_here = 1 << 5,
    };
    IMP_ENUM_FLAG_OPERATORS(ChunkDirtyFlags)

    // `System` should indirectly inherit from `TileGrids::System`.
    //
    // `HighLevelTraits` that some functions accept must contain following:
    //
    //     // Each tile of a chunk stores this.
    //     using CellType = ...;
    //     // Returns true if the cell isn't empty, for the purposes of splitting unconnected grids. Default-constructed cells must count as empty.
    //     [[nodiscard]] static bool CellIsNonEmpty(const CellType &cell);
    //     // Returns the connectivity mask of a cell in the specified direction, for the purposes of splitting unconnected grids.
    //     // The bit order should NOT be reversed when flipping direction, it's always the same.
    //     [[nodiscard]] static TileEdgeConnectivity CellConnectivity(const CellType &cell, int dir);
    //
    //     // Arbitrary user data that can be used to access different grids in the world.
    //     using WorldRef = ...;
    //     // Arbitrary user data to identify a grid in the world. Must be hashable in a phmap.
    //     using GridHandle = ...;
    //
    //     A reference to a grid in the world. (Not necessarily a true reference.)
    //     using GridRef = ...;
    //
    //     // Returns a grid from a handle.
    //     [[nodiscard]] static GridRef HandleToGrid(WorldRef world, GridHandle grid);
    //     // Returns our data from a grid.
    //     [[nodiscard]] static ChunkGrid<__> &GridToData(GridRef grid)
    //
    //     // Removes a grid from the world, when it was shrinked into nothing. Having both `grid_handle` and `grid_ref` is redundant.
    //     static void DestroyGrid(WorldRef world, GridHandle grid_handle, GridRef grid_ref);
    //
    //     // Creates a new grid, splitting it from `grid`.
    //     // Must call `init(...)` once with a `GridRef` parameter of a new grid that will be filled with the chunks.
    //     static void SplitGrid(WorldRef world, GridRef grid, auto init);

    template <typename System, typename HighLevelTraits>
    class DirtyChunkLists;

    template <typename System, int N, typename CellType>
    class ChunkGrid
    {
        template <typename System_, typename HighLevelTraits_>
        friend class DirtyChunkLists;

      public:
        class Chunk
        {
            friend ChunkGrid;

            template <typename System_, typename HighLevelTraits_>
            friend class DirtyChunkLists;

          public:
            using ChunkType = System::template Chunk<N, CellType>;
            using ChunkComponentsType = System::template ChunkComponents<N>;

          private:
            ChunkType chunk;
            ChunkComponentsType components;

            ChunkDirtyFlags dirty_flags{};

          public:
            Chunk() {}

            // The tiles themselves.
            [[nodiscard]] const ChunkType &GetChunk() const {return chunk;}
            // The connectivity components in this chunk.
            [[nodiscard]] const ChunkComponentsType &GetComponents() const {return components;}

          private:
            template <typename HighLevelTraits>
            void OnlyUpdateComponents(typename ChunkType::ComputeConnectedComponentsReusedData &reused)
            {
                components = {};
                chunk.ComputeConnectedComponents(reused, components, []{}, &HighLevelTraits::CellIsNonEmpty, &HighLevelTraits::CellConnectivity);
            }
        };

        // The chunks are stored in this.
        // Using `std::unique_ptr` here for faster resizes, and because some chunks can be null.
        using ChunkRingArray = RingMultiarray<2, std::unique_ptr<Chunk>, typename System::WholeChunkCoord>;

      private:
        ChunkRingArray chunks;

        // Returns the chunk, or null or empty if out of range.
        [[nodiscard]] std::unique_ptr<Chunk> *GetChunk(vec2<typename System::WholeChunkCoord> pos)
        {
            if (!chunks.bounds().contains(pos))
                return nullptr;
            return &chunks.at(pos);
        }

        // Updates the edge to the right or down from `pos`, depending on `vertical`.
        // `pos` can be out of bounds (when updating top and left edges).
        // Must do this after `Chunk::UpdateComponents()` on all affected edges.
        void OnlyUpdateChunkEdge(
            typename System::ComputeConnectivityBetweenChunksReusedData &reused,
            vec2<typename System::WholeChunkCoord> pos,
            bool vertical
        )
        {
            auto a = GetChunk(pos).get();
            auto b = GetChunk(pos + vec2<typename System::WholeChunkCoord>::dir4(vertical)).get();

            System::ComputeConnectivityBetweenChunks(reused, a ? &a->components : nullptr, b ? &b->components : nullptr, vertical);
        }

      public:
        // Returns the coordinate bounds of the chunk grid, measured in whole chunks.
        [[nodiscard]] rect2<typename System::WholeChunkCoord> ChunkGridBounds() const
        {
            return chunks.bounds();
        }

        // Returns the chunk, or null if out of bounds or just null.
        [[nodiscard]] const Chunk *GetChunk(vec2<typename System::WholeChunkCoord> pos) const
        {
            auto *ret = const_cast<ChunkGrid &>(*this).GetChunk(pos);
            return ret ? ret->get() : nullptr;
        }

        // Mostly for debug use. Returns the connectivity components of this chunk or null if no chunk.
        [[nodiscard]] const Chunk::ChunkComponentsType *GetChunkComponents(vec2<typename System::WholeChunkCoord> pos) const
        {
            if (!chunks.bounds().contains(pos))
                return nullptr;
            const auto &chunk = chunks.at(pos);
            return chunk ? &chunk->components : nullptr;
        }

        // Reads a cell from the grid. Returns null if the chunk doesn't exist (either outside of the boundaries,
        // or just doesn't exist because all cells in it were empty).
        [[nodiscard]] CellType *GetCell(vec2<typename System::GlobalTileCoord> coord) const
        {
            const Chunk *chunk = GetChunk(vec2<typename System::WholeChunkCoord>(Math::div_ex(coord, vec2<typename System::GlobalTileCoord>(N))));
            if (!chunk)
                return nullptr; // No such chunk.

            return &chunk->chunk.at(vec2<typename System::CoordInsideChunk>(Math::mod_ex(coord, vec2<typename System::GlobalTileCoord>(N))));
        }

        // Returns a mutable reference to array of cells for a chunk. Creates the chunk if it doesn't exist yet.
        // Sets the `geometry_changed` dirty flag for the chunk.
        template <typename HighLevelTraits>
        [[nodiscard]] typename Chunk::ChunkType::UnderlyingArray &ModifyChunk(
            DirtyChunkLists<System, HighLevelTraits> &dirty,
            typename HighLevelTraits::WorldRef world,
            typename HighLevelTraits::GridHandle grid_handle, // The handle of this grid.
            vec2<typename System::WholeChunkCoord> chunk_pos
        )
        {
            std::unique_ptr<Chunk> *chunk = GetChunk(chunk_pos);
            if (!chunk)
            {
                // Out of bounds, must extend the chunk grid.
                chunks.resize(chunks.bounds().combine(chunk_pos));
                chunk = GetChunk(chunk_pos);
                ASSERT(chunk, "Why is the chunk still null?");
            }

            if (!*chunk)
            {
                // The chunk is at a valid position, but not allocated. Need to allocate it.
                chunk = std::make_unique<Chunk>();
            }

            dirty.SetDirtyFlags(ChunkDirtyFlags::geometry_changed, world, grid_handle, chunk_pos);
            return chunk->chunk.cells;
        }

        // Returns a mutable reference to a single cell of a chunk. Creates the chunk if it doesn't exist yet.
        // Sets the `geometry_changed` dirty flag for the chunk.
        template <typename HighLevelTraits>
        [[nodiscard]] CellType &ModifyCell(
            DirtyChunkLists<System, HighLevelTraits> &dirty,
            typename HighLevelTraits::WorldRef world,
            typename HighLevelTraits::GridHandle grid_handle, // The handle of this grid.
            vec2<typename System::GlobalTileCoord> tile_coord
        )
        {
            auto chunk_pos = vec2<typename System::WholeChunkCoord>(Math::div_ex(tile_coord, vec2<typename System::GlobalTileCoord>(N)));
            auto &cells = ModifyChunk(dirty, world, grid_handle, chunk_pos);
            auto cell_pos = vec2<std::size_t>(Math::mod_ex(tile_coord, vec2<typename System::GlobalTileCoord>(N)));
            return cells[cell_pos.y][cell_pos.x];
        }

        // A simple function to load a 2D array of tiles.
        template <typename HighLevelTraits>
        void LoadFromArray(
            typename HighLevelTraits::WorldRef world,
            // If this is not null, sets the dirty flags for this entity. Resets existing flags first.
            DirtyChunkLists<System, HighLevelTraits> *dirty,
            // The handle for this grid. Only makes sense if `dirty` is specified.
            typename HighLevelTraits::GridHandle grid_handle,
            // The input array size.
            vec2<typename System::GlobalTileCoord> tile_size,
            // Loads a single cell.
            // `(vec2<typename System::CoordInsideChunk> pos, CellType &cell) -> void`.
            auto &&load_tile
        )
        {
            *this = {};

            if (dirty)
                dirty->CancelGridUpdate(grid_handle);

            auto num_chunks = vec2<typename System::WholeChunkCoord>(div_ex(tile_size, vec2<typename System::GlobalTileCoord>(N)));
            vec2<typename System::WholeChunkCoord> first_chunk_pos{}; // Zero for now.

            chunks.resize(first_chunk_pos.rect_size(num_chunks));
            for (auto chunk_pos : vector_range(chunks.bounds()))
            {
                const auto base_tile_pos = vec2<typename System::GlobalTileCoord>(chunk_pos) * vec2<typename System::GlobalTileCoord>(N);

                auto &chunk = chunks.at(chunk_pos);
                chunk = std::make_unique<Chunk>();

                for (auto pos_in_chunk : vector_range(clamp_max(tile_size - base_tile_pos, N)))
                    load_tile(base_tile_pos + vec2<typename System::GlobalTileCoord>(pos_in_chunk), chunk->chunk.at(pos_in_chunk));

                // Setting dirty flags individually is not too efficient. Ideally we'd query the entity dirty lists once, and then populate them.
                if (dirty)
                    dirty->SetDirtyFlags(ChunkDirtyFlags::geometry_changed, world, grid_handle, chunk_pos);
            }
        }
    };

    template <typename System, typename HighLevelTraits>
    class DirtyChunkLists
    {
        static constexpr int num_lists = 3;

        [[nodiscard]] static constexpr int DirtyBitToListIndex(ChunkDirtyFlags bit)
        {
            switch (bit)
            {
                case ChunkDirtyFlags::geometry_changed:             return 0;
                case ChunkDirtyFlags::update_edge_0:                return 1; // Edges 2 and 3 don't have their own indices, they are stored as 0 and 1
                case ChunkDirtyFlags::update_edge_1:                return 2; // in the adjacent chunks. This works even if there's no actual chunk there.
                // Sync `num_lists` with the number of constants here.

                // Those have no associated lists.
                case ChunkDirtyFlags::update_edge_2:
                case ChunkDirtyFlags::update_edge_3:
                case ChunkDirtyFlags::update_edge_all:
                case ChunkDirtyFlags::_all_bits:
                case ChunkDirtyFlags::try_splitting_to_components_from_here:
            }
            Program::HardError("This dirty flag doesn't have an associated list index.");
        }

        using ChunkCoordList = std::vector<vec2<typename System::WholeChunkCoord>>;

        struct GridEntry
        {
            ChunkCoordList lists[num_lists];

            // Does nothing if the flag is already set for that chunk, or if the chunk is null.
            // `grid` must match the current instance.
            void SetDirtyFlagsLow(ChunkDirtyFlags flags, typename HighLevelTraits::GridRef grid, vec2<typename System::WholeChunkCoord> chunk_coord)
            {
                if (!bool(flags))
                    return; // No flags to set.

                auto &data = HighLevelTraits::GridToData(grid);
                auto *const common_chunk = data.GetChunk(chunk_coord); // This can be null e.g. for edge updates of adjacent chunks.

                for (ChunkDirtyFlags bit{1}; bool(flags); bit <<= 1)
                {
                    if (!bool(flags & bit))
                        continue;
                    flags &= ~bit;

                    auto chunk = common_chunk;

                    // If this is an edge update that affects the other chunk, find that other chunk and bit.
                    decltype(chunk) dual_chunk = nullptr;
                    ChunkDirtyFlags dual_bit{};
                    if (bit == ChunkDirtyFlags::update_edge_0)
                    {
                        dual_chunk = data.GetChunk(chunk_coord + vec2<typename System::WholeChunkCoord>(1,0));
                        dual_bit = ChunkDirtyFlags::update_edge_2;
                    }
                    else if (bit == ChunkDirtyFlags::update_edge_1)
                    {
                        dual_chunk = data.GetChunk(chunk_coord + vec2<typename System::WholeChunkCoord>(0,1));
                        dual_bit = ChunkDirtyFlags::update_edge_3;
                    }
                    else if (bit == ChunkDirtyFlags::update_edge_2)
                    {
                        // Edges 2 and 3 are handled differently.
                        dual_chunk = chunk; chunk = data.GetChunk(chunk_coord + vec2<typename System::WholeChunkCoord>(-1,0));
                        dual_bit = bit; bit = ChunkDirtyFlags::update_edge_0;
                    }
                    else if (bit == ChunkDirtyFlags::update_edge_3)
                    {
                        dual_chunk = chunk; chunk = data.GetChunk(chunk_coord + vec2<typename System::WholeChunkCoord>(0,-1));
                        dual_bit = bit; bit = ChunkDirtyFlags::update_edge_1;
                    }

                    bool chunk_valid = chunk && *chunk;
                    bool dual_chunk_valid = dual_chunk && *dual_chunk;

                    // At least one of the two chunks must exist.
                    if (!chunk_valid && !dual_chunk_valid)
                        continue;

                    // Only write to the list if all existing chunks don't have the bit set.
                    if ((!chunk_valid || !bool((*chunk)->dirty_flags & bit)) && (!dual_chunk_valid || !bool((*dual_chunk)->dirty_flags & dual_bit)))
                    {
                        lists[DirtyBitToListIndex(bit)].push_back(chunk_coord);
                    }

                    // Regardless, set the bits.
                    if (chunk_valid)
                        (*chunk)->dirty_flags |= bit;
                    if (dual_chunk_valid)
                        (*dual_chunk)->dirty_flags |= dual_bit;
                }
            }
        };

        using GridMap = phmap::flat_hash_map<typename HighLevelTraits::GridHandle, GridEntry>;

        GridMap grid_map;

        [[nodiscard]] static int BitToIndex(ChunkDirtyFlags bit)
        {
            ASSERT(bool(bit & ChunkDirtyFlags::_all_bits) && std::has_single_bit(std::to_underlying(bit)), "This isn't a single bit.");
            return std::countr_zero(std::to_underlying(bit));
        }

      public:
        DirtyChunkLists() {}

        // Sets the dirty flags in a chunk. No-op if no such chunk, or if the flag is already set.
        // This is a high-level version, use `GridEntry::SetDirtyFlagsLow()` internally for a better speed.
        void SetDirtyFlags(
            ChunkDirtyFlags flags,
            typename HighLevelTraits::WorldRef world,
            typename HighLevelTraits::GridHandle grid_handle,
            vec2<typename System::WholeChunkCoord> chunk_coord
        )
        {
            grid_map[grid_handle].SetDirtyFlagsLow(flags, HighLevelTraits::HandleToGrid(world, grid_handle), chunk_coord);
        }

        // Remove the grid from the pending update list.
        void CancelGridUpdate(typename HighLevelTraits::GridHandle handle)
        {
            grid_map.erase(handle);
        }

        // Handle `geometry_changed` flag.
        // This doesn't automatically set `update_edge_??` dirty flags, because manual setting can be more precise.
        void HandleGeometryUpdate(
            typename HighLevelTraits::WorldRef world,
            // This is `DirtyChunkLists<__>::System::Chunk<__>::ComputeConnectedComponentsReusedData`.
            auto &reused
        )
        {
            for (auto &[grid_handle, grid_entry] : grid_map)
            {
                typename HighLevelTraits::GridRef grid_ref = HighLevelTraits::HandleToGrid(world, grid_handle);
                auto &grid_data = HighLevelTraits::GridToData(grid_ref);

                // If true, a border chunk (at `i`th side) was destroyed and maybe we should shrink the chunk grid in this direction.
                bool shrink_side[4]{};

                auto &chunk_coord_list = grid_entry.lists[BitToIndex(ChunkDirtyFlags::geometry_changed)];
                for (auto chunk_coord : chunk_coord_list)
                {
                    auto *chunk = grid_data.GetChunk(chunk_coord);

                    if (!chunk || !*chunk)
                        continue; // No such chunk, ignore it.

                    (*chunk)->template OnlyUpdateComponents<HighLevelTraits>(reused);

                    // Destroy the chunk if empty.
                    if ((*chunk)->components.components.empty())
                    {
                        *chunk = nullptr;

                        if (chunk_coord.x == grid_data.chunks.bounds().b.x) shrink_side[0] = true;
                        if (chunk_coord.y == grid_data.chunks.bounds().b.y) shrink_side[1] = true;
                        if (chunk_coord.x == grid_data.chunks.bounds().a.x) shrink_side[2] = true;
                        if (chunk_coord.y == grid_data.chunks.bounds().a.y) shrink_side[3] = true;
                    }
                }

                // Shrink the chunk grid if any of the border chunks were destroyed.
                bool shrinked_any = false;
                auto new_bounds = grid_data.chunks.bounds();
                for (int i = 0; i < 4; i++)
                {
                    if (!shrink_side[i])
                        continue;

                    while (new_bounds.has_area())
                    {
                        bool is_vertical_edge = i % 2 == 0;
                        typename System::CoordInsideChunk len = new_bounds.size()[is_vertical_edge];

                        vec2<typename System::CoordInsideChunk> pos(
                            i == 0 ? new_bounds.b.x - 1 : new_bounds.a.x,
                            i == 1 ? new_bounds.b.y - 1 : new_bounds.a.y
                        );

                        bool ok = true;

                        for (typename System::CoordInsideChunk i = 0; i < len; i++)
                        {
                            if (bool(grid_data.GetChunk(pos)))
                            {
                                ok = false;
                                break;
                            }

                            pos[is_vertical_edge]++;
                        }

                        if (ok)
                        {
                            new_bounds = new_bounds.shrink_dir(vec2<typename System::CoordInsideChunk>::dir4(i));
                            shrinked_any = true;
                        }
                        else
                        {
                            break;
                        }
                    }
                }

                // If the grid was shrinked into nothing, destroy it.
                if (shrinked_any)
                {
                    if (new_bounds.has_area())
                        HighLevelTraits::DestroyGrid(world, grid_handle, grid_ref);
                    else
                        grid_data.chunks.resize(new_bounds);
                }

                chunk_coord_list.clear();
            }
        }

        // Handle `update_edge_??` flags, and split the grid if needed.
        void HandleEdgesUpdateAndSplit(
            typename HighLevelTraits::WorldRef world,
            typename System::ComputeConnectivityBetweenChunksReusedData &reused,
            typename System::ChunkGridSplitter &splitter,
            std::size_t splitter_per_component_capacity = System::ChunkGridSplitter::default_per_component_capacity
        )
        {
            for (auto &[grid_handle, grid_entry] : grid_map)
            {
                splitter.Reset();

                typename HighLevelTraits::GridRef grid_ref = HighLevelTraits::HandleToGrid(world, grid_handle);
                auto &grid_data = HighLevelTraits::GridToData(grid_ref);

                auto &chunk_coord_list_0 = grid_entry.chunk_coords[BitToIndex(ChunkDirtyFlags::update_edge_0)];
                auto &chunk_coord_list_1 = grid_entry.chunk_coords[BitToIndex(ChunkDirtyFlags::update_edge_1)];

                for (bool vertical : {false, true})
                {
                    auto &list = vertical ? chunk_coord_list_1 : chunk_coord_list_0;

                    for (const auto chunk_coord_a : list)
                    {
                        auto chunk_coord_b = chunk_coord_a + vec2<typename System::WholeChunkCoord>::dir4(vertical);
                        auto *chunk_a = grid_data.GetChunk(chunk_coord_a).get();
                        auto *chunk_b = grid_data.GetChunk(chunk_coord_b).get();

                        System::ComputeConnectivityBetweenChunks(reused, chunk_a ? &chunk_a->components : nullptr, chunk_b ? &chunk_b->components : nullptr, vertical);

                        // Add all components to the splitter.
                        // Maybe we could skip some components here, but it's easier to add everything.
                        // If you decide to skip some, `try_splitting_to_components_from_here` flags needs to be destroyed,
                        //   and we'd need a separate bool in each component.
                        if (chunk_a && !bool(chunk_a->dirty_flags & ChunkDirtyFlags::try_splitting_to_components_from_here))
                        {
                            chunk_a->dirty_flags |= ChunkDirtyFlags::try_splitting_to_components_from_here;
                            for (std::size_t i = 0; i < chunk_a->components.size(); i++)
                                splitter.AddInitialComponent(chunk_coord_a, System::ComponentIndex(i), splitter_per_component_capacity);
                        }
                        if (chunk_b && !bool(chunk_b->dirty_flags & ChunkDirtyFlags::try_splitting_to_components_from_here))
                        {
                            chunk_b->dirty_flags |= ChunkDirtyFlags::try_splitting_to_components_from_here;
                            for (std::size_t i = 0; i < chunk_b->components.size(); i++)
                                splitter.AddInitialComponent(chunk_coord_b, System::ComponentIndex(i), splitter_per_component_capacity);
                        }
                    }
                }

                chunk_coord_list_0.clear();
                chunk_coord_list_1.clear();

                // Reset the `try_splitting_to_components_from_here` for the affected chunks.
                splitter.ForEachCoordToVisit([&](vec2<typename System::WholeChunkCoord> coord, System::ComponentIndex index)
                {
                    (void)index;
                    if (auto chunk = grid_data.GetChunk(coord).get())
                        chunk->dirty_flags &= ~ChunkDirtyFlags::try_splitting_to_components_from_here;
                });

                // while (!splitter.Step()) {}

                // std::size_t num_comps = splitter.NumComponentsToEmit();

                // for (std::size_t i = 0; i < num_comps; i++)
                // {
                //     HighLevelTraits::SplitGrid(world, grid_ref, [&](typename System::GridRef new_grid_ref)
                //     {
                //         const auto &global_comp = splitter.GetComponentToEmit(i);
                //         auto &new_grid_data = HighLevelTraits::GridToData(grid_ref);

                //         new_grid_data.chunks.resize(global_comp.bounds);

                //         for (const typename System::ComponentCoords &comp : global_comp.contents)
                //         {
                //             auto *source_chunk = grid_data.chunks.at(comp.chunk_coord).get();
                //             ASSERT(source_chunk, "Source chunk doesn't exist?");

                //             auto &new_chunk = new_grid_data.chunks.at(comp.chunk_coord);

                //             // Allocate the chunk if not allocated yet.
                //             if (!new_chunk)
                //                 new_chunk = std::make_unique<std::remove_reference_t<decltype(new_chunk)>::element_type>();

                //             // source_chunk. comp.in_chunk_component

                //             // new_chunk->chunk.at(comp.coords)
                //         }
                //     });
                // }
            }

        }
    };
}
