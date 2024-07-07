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
        [[nodiscard]] std::unique_ptr<Chunk> *GetMutChunk(vec2<typename System::WholeChunkCoord> pos)
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
            auto a = GetMutChunk(pos).get();
            auto b = GetMutChunk(pos + vec2<typename System::WholeChunkCoord>::dir4(vertical)).get();

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
            auto *ret = const_cast<ChunkGrid &>(*this).GetMutChunk(pos);
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
            std::unique_ptr<Chunk> *chunk = GetMutChunk(chunk_pos);
            if (!chunk)
            {
                // Out of bounds, must extend the chunk grid.
                chunks.resize(chunks.bounds().combine(chunk_pos));
                chunk = GetMutChunk(chunk_pos);
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
                auto *const common_chunk = data.GetMutChunk(chunk_coord); // This can be null e.g. for edge updates of adjacent chunks.

                for (ChunkDirtyFlags bit{1}; bool(flags); bit <<= 1)
                {
                    if (!bool(flags & bit))
                        continue;
                    flags &= ~bit;

                    auto chunk = common_chunk;
                    auto chunk_coord_copy = chunk_coord;

                    // If this is an edge update that affects the other chunk, find that other chunk and bit.
                    decltype(chunk) dual_chunk = nullptr;
                    ChunkDirtyFlags dual_bit{};
                    if (bit == ChunkDirtyFlags::update_edge_0)
                    {
                        dual_chunk = data.GetMutChunk(chunk_coord + vec2<typename System::WholeChunkCoord>(1,0));
                        dual_bit = ChunkDirtyFlags::update_edge_2;
                    }
                    else if (bit == ChunkDirtyFlags::update_edge_1)
                    {
                        dual_chunk = data.GetMutChunk(chunk_coord + vec2<typename System::WholeChunkCoord>(0,1));
                        dual_bit = ChunkDirtyFlags::update_edge_3;
                    }
                    else if (bit == ChunkDirtyFlags::update_edge_2)
                    {
                        // Edges 2 and 3 are handled differently.
                        dual_chunk = chunk; chunk = data.GetMutChunk(chunk_coord_copy -= vec2<typename System::WholeChunkCoord>(1,0));
                        dual_bit = bit; bit = ChunkDirtyFlags::update_edge_0;
                    }
                    else if (bit == ChunkDirtyFlags::update_edge_3)
                    {
                        dual_chunk = chunk; chunk = data.GetMutChunk(chunk_coord_copy -= vec2<typename System::WholeChunkCoord>(0,1));
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
                        lists[DirtyBitToListIndex(bit)].push_back(chunk_coord_copy);
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
        // This automatically sets `update_edge_??` dirty flags. Even though we could manually set those flags only if
        // you modified a tile next to an edge, this is not sufficient because a modification elsewhere in the chunk can mess up the component indices.
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

                typename System::ChunkGridShrinker shrinker(grid_data.chunks.bounds());

                auto &chunk_coord_list = grid_entry.lists[BitToIndex(ChunkDirtyFlags::geometry_changed)];
                for (auto chunk_coord : chunk_coord_list)
                {
                    auto *chunk = grid_data.GetMutChunk(chunk_coord);

                    if (!chunk || !*chunk)
                        continue; // No such chunk, ignore it.

                    (*chunk)->template OnlyUpdateComponents<HighLevelTraits>(reused);

                    // Destroy the chunk if empty.
                    if ((*chunk)->components.components.empty())
                    {
                        *chunk = nullptr;
                        shrinker.AddEmptyChunk(chunk_coord);
                    }
                }

                // Set dirty flags for the edges. See the comment on this whole function.
                // We need a separate loop to properly ignore chunks destroyed during the previous loop.
                for (auto chunk_coord : chunk_coord_list)
                    grid_entry.SetDirtyFlagsLow(ChunkDirtyFlags::update_edge_all, grid_ref, chunk_coord);

                // Shrink the chunk grid if any of the border chunks were destroyed.
                // If the grid was shrinked into nothing, destroy it.
                if (shrinker.Finish([&](vec2<typename System::WholeChunkCoord> pos){return bool(grid_data.GetChunk(pos));}))
                {
                    if (shrinker.bounds.has_area())
                        HighLevelTraits::DestroyGrid(world, grid_handle, grid_ref);
                    else
                        grid_data.chunks.resize(shrinker.bounds);
                }

                chunk_coord_list.clear();
            }
        }

        struct ReusedEdgeUpdateData
        {

            // For `System::ComputeConnectivityBetweenChunks()`.
            typename System::ComputeConnectivityBetweenChunksReusedData conn;
            // For splitting grids.
            typename System::ChunkGridSplitter splitter;

            using ReusedComponentMap = phmap::flat_hash_map<vec2<typename System::WholeChunkCoord>, std::vector<typename System::ComponentIndex>>;
            // When splitting a grid, this accumulates all chunk components that are removed from it.
            ReusedComponentMap comp_map;

            // When splitting a grid, this accumulates all new chunk edges in each component, so we can update them.
            // `[0]` is vertical edges to the right of chunks, `[1]` is horizontal edges below a chunk.
            std::vector<vec2<typename System::WholeChunkCoord>> new_chunk_edges[2];
        };

        // Handle `update_edge_??` flags, and split the grid if needed.
        void HandleEdgesUpdateAndSplit(
            typename HighLevelTraits::WorldRef world,
            ReusedEdgeUpdateData &reused,
            // Setting this to false disables the splitter.
            bool enable_split = true,
            // We expect at most this many per-chunk components inside per a global component when splitting.
            std::size_t splitter_per_component_capacity = System::ChunkGridSplitter::default_per_component_capacity,
            // We expect at most this many components per chunk when splitting.
            std::size_t expected_num_comps_per_chunk = 8
        )
        {
            for (auto &[grid_handle, grid_entry] : grid_map)
            {
                if (enable_split)
                    reused.splitter.Reset();

                typename HighLevelTraits::GridRef grid_ref = HighLevelTraits::HandleToGrid(world, grid_handle);
                auto &grid_data = HighLevelTraits::GridToData(grid_ref);

                auto &chunk_coord_list_0 = grid_entry.lists[BitToIndex(ChunkDirtyFlags::update_edge_0)];
                auto &chunk_coord_list_1 = grid_entry.lists[BitToIndex(ChunkDirtyFlags::update_edge_1)];

                for (bool vertical : {false, true})
                {
                    auto &list = vertical ? chunk_coord_list_1 : chunk_coord_list_0;

                    for (const auto chunk_coord_a : list)
                    {
                        auto chunk_coord_b = chunk_coord_a + vec2<typename System::WholeChunkCoord>::dir4(vertical);
                        auto *chunk_ptr_a = grid_data.GetMutChunk(chunk_coord_a);
                        auto *chunk_ptr_b = grid_data.GetMutChunk(chunk_coord_b);
                        auto *chunk_a = chunk_ptr_a ? chunk_ptr_a->get() : nullptr;
                        auto *chunk_b = chunk_ptr_b ? chunk_ptr_b->get() : nullptr;

                        System::ComputeConnectivityBetweenChunks(reused.conn, chunk_a ? &chunk_a->components : nullptr, chunk_b ? &chunk_b->components : nullptr, vertical);

                        // Add all components to the splitter.
                        // Maybe we could skip some components here, but it's easier to add everything.
                        // If you decide to skip some, `try_splitting_to_components_from_here` flag needs to be destroyed,
                        //   and we'd need a separate bool in each component.
                        if (enable_split)
                        {
                            if (chunk_a && !bool(chunk_a->dirty_flags & ChunkDirtyFlags::try_splitting_to_components_from_here))
                            {
                                chunk_a->dirty_flags |= ChunkDirtyFlags::try_splitting_to_components_from_here;
                                for (std::size_t i = 0; i < chunk_a->components.components.size(); i++)
                                    reused.splitter.AddInitialComponent(chunk_coord_a, typename System::ComponentIndex(i), splitter_per_component_capacity);
                            }
                            if (chunk_b && !bool(chunk_b->dirty_flags & ChunkDirtyFlags::try_splitting_to_components_from_here))
                            {
                                chunk_b->dirty_flags |= ChunkDirtyFlags::try_splitting_to_components_from_here;
                                for (std::size_t i = 0; i < chunk_b->components.components.size(); i++)
                                    reused.splitter.AddInitialComponent(chunk_coord_b, typename System::ComponentIndex(i), splitter_per_component_capacity);
                            }
                        }
                    }
                }

                chunk_coord_list_0.clear();
                chunk_coord_list_1.clear();

                if (enable_split)
                {
                    // Reset the `try_splitting_to_components_from_here` for the affected chunks.
                    reused.splitter.ForEachCoordToVisit([&](vec2<typename System::WholeChunkCoord> coord, System::ComponentIndex index)
                    {
                        (void)index;
                        if (auto chunk = grid_data.GetMutChunk(coord); chunk && *chunk)
                            (*chunk)->dirty_flags &= ~ChunkDirtyFlags::try_splitting_to_components_from_here;
                        return false;
                    });

                    // Run the splitter...
                    while (!reused.splitter.Step(
                        [&](vec2<typename System::WholeChunkCoord> chunk_coord) -> const auto &
                        {
                            return (*grid_data.GetMutChunk(chunk_coord))->components;
                        }
                    ))
                    {}

                    reused.comp_map.clear();

                    std::size_t num_comps = reused.splitter.NumComponentsToEmit();

                    // For each splitted entity...
                    for (std::size_t i = 0; i < num_comps; i++)
                    {
                        auto comp_to_emit = reused.splitter.GetComponentToEmit(i);

                        // Fill the mapping from chunk coordinates to a list of removed component indices, common for all detached components.
                        reused.comp_map.reserve(comp_to_emit.contents.size());
                        for (auto coords : comp_to_emit.contents)
                        {
                            auto [iter, is_new] = reused.comp_map.try_emplace(coords.chunk_coord);
                            if (is_new)
                                iter->second.reserve(expected_num_comps_per_chunk);
                            iter->second.push_back(coords.in_chunk_component);
                        }

                        HighLevelTraits::SplitGrid(world, grid_ref, [&](typename HighLevelTraits::GridRef new_grid_ref)
                        {
                            auto &new_grid_data = HighLevelTraits::GridToData(new_grid_ref);

                            new_grid_data.chunks.resize(comp_to_emit.bounds);

                            reused.new_chunk_edges[0].clear();
                            reused.new_chunk_edges[1].clear();

                            for (auto coords : comp_to_emit.contents)
                            {
                                const auto &old_chunk = grid_data.chunks.at(coords.chunk_coord);
                                auto &new_chunk = new_grid_data.chunks.at(coords.chunk_coord);
                                if (!new_chunk)
                                    new_chunk = std::make_unique<std::remove_cvref_t<decltype(*new_chunk)>>();

                                new_chunk->chunk.MoveComponentFrom(
                                    coords.in_chunk_component,
                                    new_chunk->components,
                                    old_chunk->chunk,
                                    old_chunk->components
                                );

                                // For each of the 4 chunk edges, if there's no chunk there yet, add this edge to our own little dirty list.
                                for (int j = 0; j < 4; j++)
                                {
                                    auto other_chunk_coord = coords.chunk_coord + vec2<typename System::WholeChunkCoord>::dir4(j);
                                    const auto &other_chunk = new_grid_data.GetChunk(other_chunk_coord);
                                    if (!other_chunk)
                                        reused.new_chunk_edges[j % 2].push_back(j >= 2 ? other_chunk_coord : coords.chunk_coord);
                                }
                            }

                            // Update chunk boundaries.
                            for (bool vertical : {false, true})
                            {
                                for (const auto chunk_coord_a : reused.new_chunk_edges[vertical])
                                {
                                    auto chunk_coord_b = chunk_coord_a + vec2<typename System::WholeChunkCoord>::dir4(vertical);
                                    auto *chunk_ptr_a = new_grid_data.GetMutChunk(chunk_coord_a);
                                    auto *chunk_ptr_b = new_grid_data.GetMutChunk(chunk_coord_b);
                                    auto *chunk_a = chunk_ptr_a ? chunk_ptr_a->get() : nullptr;
                                    auto *chunk_b = chunk_ptr_b ? chunk_ptr_b->get() : nullptr;

                                    System::ComputeConnectivityBetweenChunks(reused.conn, chunk_a ? &chunk_a->components : nullptr, chunk_b ? &chunk_b->components : nullptr, vertical);
                                }
                            }
                        });
                    }

                    // We'll use to shrink the original grid if we removed some chunks on the sides.
                    typename System::ChunkGridShrinker shrinker(grid_data.chunks.bounds());

                    // Erase the original components that were split.
                    // We do it after everything else, because that messes up the indices.
                    for (auto [chunk_coord, chunk_comp_indices] : reused.comp_map)
                    {
                        auto &old_chunk = grid_data.chunks.at(chunk_coord);
                        auto &old_comps = old_chunk->components;

                        // Sort to be able to iterate backwards, to make sure we don't mess up the indices while deleting.
                        std::sort(chunk_comp_indices.begin(), chunk_comp_indices.end());

                        for (std::size_t i = chunk_comp_indices.size(); i-- > 0;)
                        {
                            old_comps.SwapWithLastAndRemoveComponent(chunk_comp_indices[i], true);
                            if (old_comps.components.empty())
                            {
                                old_chunk = nullptr;
                                shrinker.AddEmptyChunk(chunk_coord);
                            }
                        }
                    }

                    // Finish shrinking the grid.
                    if (shrinker.Finish([&](vec2<typename System::WholeChunkCoord> pos){return bool(grid_data.GetChunk(pos));}))
                    {
                        ASSERT(shrinker.bounds.has_area(), "After splitting a chunk grid, the original grid is somehow empty, but it's supposed to have something remaining.");

                        grid_data.chunks.resize(shrinker.bounds);
                    }

                    // Update chunk edges in the original grid.
                    for (auto [chunk_coord, chunk_comp_indices] : reused.comp_map)
                    {
                        for (int i = 0; i < 4; i++)
                        {
                            auto chunk_coord_a = chunk_coord;
                            auto chunk_coord_b = chunk_coord + vec2<typename System::WholeChunkCoord>::dir4(i);
                            if (i >= 2)
                                std::swap(chunk_coord_a, chunk_coord_b);
                            auto *chunk_ptr_a = grid_data.GetMutChunk(chunk_coord_a);
                            auto *chunk_ptr_b = grid_data.GetMutChunk(chunk_coord_b);
                            auto *chunk_a = chunk_ptr_a ? chunk_ptr_a->get() : nullptr;
                            auto *chunk_b = chunk_ptr_b ? chunk_ptr_b->get() : nullptr;
                            System::ComputeConnectivityBetweenChunks(reused.conn, chunk_a ? &chunk_a->components : nullptr, chunk_b ? &chunk_b->components : nullptr, i % 2);
                        }
                    }
                }
            }

            reused.comp_map.clear(); // Clean this up just in case, to not waste memory on nested vectors.
        }

        template <int N, typename CellType>
        struct ReusedUpdateData
        {
            typename System::template Chunk<N, CellType>::ComputeConnectedComponentsReusedData comps;
            ReusedEdgeUpdateData edge;
        };
    };
}
