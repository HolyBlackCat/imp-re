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
        // The chunk was modified, try re-splitting the whole chunk grid starting from here.
        try_splitting_grid_from_here      = 1 << 5,

        _all_bits                        = (1 << 6) - 1,
    };
    IMP_ENUM_FLAG_OPERATORS(ChunkDirtyFlags)

    // `HighLevelSystemTraits` should inherit (perhaps indirectly) from `TileGrids::DefaultSystemTraits`,
    // and add following:
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
    //     [[nodiscard]] static GridRef HandleToGrid(WorldRef world, GridHandle &grid);
    //     // Returns our data from a grid.
    //     [[nodiscard]] static ChunkGrid<__> &GridToData(GridRef grid)
    //
    //     // Removes a grid from the world, when it was shrinked into nothing. Having both `grid_handle` and `grid_ref` is redundant.
    //     static void DestroyGrid(WorldRef world, GridHandle &grid_handle, GrifRef grid_ref);

    template <typename HighLevelSystemTraits>
    class DirtyChunkLists;

    template <typename HighLevelSystemTraits, int N>
    class ChunkGrid
    {
        friend DirtyChunkLists<HighLevelSystemTraits>;

      public:
        using Traits = HighLevelSystemTraits;
        using System = TileGrids::System<Traits>;

        class Chunk
        {
            friend ChunkGrid;

          public:
            using ChunkType = System::template Chunk<N, typename Traits::CellType>;
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
            void OnlyUpdateComponents(typename ChunkType::ComputeConnectedComponentsReusedData &reused)
            {
                components = {};
                chunk.ComputeConnectedComponents(reused, components, []{}, &Traits::CellIsNonEmpty, &Traits::CellConnectivity);
            }
        };

        // The chunks are stored in this.
        // Using `std::unique_ptr` here for faster resizes, and because some chunks can be null.
        using ChunkRingArray = RingMultiarray<2, std::unique_ptr<Chunk>, typename Traits::WholeChunkCoord>;

      private:
        ChunkRingArray chunks;

        // Returns the chunk, or null or empty if out of range.
        [[nodiscard]] std::unique_ptr<Chunk> *GetChunk(vec2<typename Traits::WholeChunkCoord> pos)
        {
            if (!chunks.bounds().contains(pos))
                return nullptr;
            return chunks.at(pos);
        }

        // Updates the edge to the right or down from `pos`, depending on `vertical`.
        // `pos` can be out of bounds (when updating top and left edges).
        // Must do this after `Chunk::UpdateComponents()` on all affected edges.
        void OnlyUpdateChunkEdge(
            typename System::ComputeConnectivityBetweenChunksReusedData &reused,
            vec2<typename Traits::WholeChunkCoord> pos,
            bool vertical
        )
        {
            auto a = GetChunk(pos).get();
            auto b = GetChunk(pos + vec2<typename Traits::WholeChunkCoord>::dir4(vertical)).get();

            System::ComputeConnectivityBetweenChunks(reused, a ? &a->components : nullptr, b ? &b->components : nullptr, vertical);
        }

      public:
        // Updates the specified chunk with the specified action. `bit` must be a single bit.
        // Returns the new flags that need to be OR-ed with the existing flags (don't forget to zero this bit).
        // Does nothing and returns zero if there is no chunk at those coords.
        [[nodiscard]] ChunkDirtyFlags UpdateNow(
            vec2<typename Traits::WholeChunkCoord> chunk_coord,
            ChunkDirtyFlags bit,
            // Needed for `ChunkDirtyFlags::geometry_changed`.
            Chunk::ChunkType::ComputeConnectedComponentsReusedData *reused_comps,
            // Needed for `ChunkDirtyFlags::update_edge_??`.
            typename System::ComputeConnectivityBetweenChunksReusedData *reused_conn
        )
        {
            switch (bit)
            {
              case ChunkDirtyFlags::geometry_changed:
                {
                    auto &c = GetChunk(chunk_coord);
                    if (!c)
                        return {};
                    c->OnlyUpdateComponents(*reused_comps);
                    if (c->components.empty())
                    {
                        c = nullptr; // Destroy the chunk if no components.
                        return {};
                    }
                    return ChunkDirtyFlags::update_edge_all; // Can we be more specific? Unsure how.
                }

              case ChunkDirtyFlags::update_edge_0:
              case ChunkDirtyFlags::update_edge_1:
                OnlyUpdateChunkEdge(*reused_conn, chunk_coord, bit == ChunkDirtyFlags::update_edge_1);
                return ChunkDirtyFlags::try_splitting_grid_from_here;
              case ChunkDirtyFlags::update_edge_2:
                return UpdateNow(chunk_coord - vec2<typename Traits::WholeChunkCoord>(1,0), ChunkDirtyFlags::update_edge_0, reused_comps, reused_conn);
              case ChunkDirtyFlags::update_edge_3:
                return UpdateNow(chunk_coord - vec2<typename Traits::WholeChunkCoord>(0,1), ChunkDirtyFlags::update_edge_1, reused_comps, reused_conn);

              case ChunkDirtyFlags::update_edge_all:
              case ChunkDirtyFlags::try_splitting_grid_from_here:
              case ChunkDirtyFlags::_all_bits:
                // Those bits can't be handled here.
            }

            Program::HardError("Invalid enum passed to `TileGrids::ChunkGrid::UpdateNow()`.");
        }
    };

    template <typename HighLevelSystemTraits>
    class DirtyChunkLists
    {
      public:
        using Traits = HighLevelSystemTraits;
        using System = TileGrids::System<Traits>;

      private:
        static constexpr int num_lists = 4;

        [[nodiscard]] static constexpr int DirtyBitToListIndex(ChunkDirtyFlags bit)
        {
            switch (bit)
            {
                case ChunkDirtyFlags::geometry_changed:             return 0;
                case ChunkDirtyFlags::update_edge_0:                return 1; // Edges 2 and 3 don't have their own indices, they are stored as 0 and 1
                case ChunkDirtyFlags::update_edge_1:                return 2; // in the adjacent chunks. This works even if there's no actual chunk there.
                case ChunkDirtyFlags::try_splitting_grid_from_here: return 3;

                // Those have no associated lists.
                case ChunkDirtyFlags::update_edge_2:
                case ChunkDirtyFlags::update_edge_3:
                case ChunkDirtyFlags::update_edge_all:
                case ChunkDirtyFlags::_all_bits:
            }
            Program::HardError("This dirty flag doesn't have an associated list index.");
        }

        using ChunkCoordList = std::vector<vec2<typename Traits::WholeChunkCoord>>;

        struct GridEntry
        {
            ChunkCoordList lists[num_lists];

            // Does nothing if the flag is already set for that chunk, or if the chunk is null.
            // `grid` must match the current instance.
            void SetDirtyFlagsLow(ChunkDirtyFlags flags, typename Traits::GridRef grid, vec2<typename Traits::WholeChunkCoord> chunk_coord)
            {
                if (!bool(flags))
                    return; // No flags to set.

                auto &data = Traits::GridToData(grid);
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
                        dual_chunk = data.GetChunk(chunk_coord + vec2<typename Traits::WholeChunkCoord>(1,0));
                        dual_bit = ChunkDirtyFlags::update_edge_2;
                    }
                    else if (bit == ChunkDirtyFlags::update_edge_1)
                    {
                        dual_chunk = data.GetChunk(chunk_coord + vec2<typename Traits::WholeChunkCoord>(0,1));
                        dual_bit = ChunkDirtyFlags::update_edge_3;
                    }
                    else if (bit == ChunkDirtyFlags::update_edge_2)
                    {
                        // Edges 2 and 3 are handled differently.
                        dual_chunk = chunk; chunk = data.GetChunk(chunk_coord + vec2<typename Traits::WholeChunkCoord>(-1,0));
                        dual_bit = bit; bit = ChunkDirtyFlags::update_edge_0;
                    }
                    else if (bit == ChunkDirtyFlags::update_edge_3)
                    {
                        dual_chunk = chunk; chunk = data.GetChunk(chunk_coord + vec2<typename Traits::WholeChunkCoord>(0,-1));
                        dual_bit = bit; bit = ChunkDirtyFlags::update_edge_1;
                    }

                    // At least one of the two chunks must exist.
                    if (!chunk && !dual_chunk)
                        continue;

                    // Only write to the list if all existing chunks don't have the bit set.
                    if ((!chunk || !bool(chunk->dirty_flags & bit)) && (!dual_chunk || !bool(dual_chunk->dirty_flags & dual_bit)))
                    {
                        lists[DirtyBitToListIndex(bit)].push_back(chunk_coord);
                    }

                    // Regardless, set the bits.
                    if (chunk)
                        chunk->dirty_flags |= bit;
                    if (dual_chunk)
                        dual_chunk->dirty_flags |= dual_bit;
                }
            }
        };

        using GridMap = phmap::flat_hash_map<typename Traits::GridHandle, GridEntry>;

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
            typename Traits::WorldRef world,
            typename Traits::GridHandle grid_handle,
            vec2<typename Traits::WholeChunkCoord> chunk_coord
        )
        {
            grid_map[grid_handle].SetDirtyFlagsLow(flags, Traits::HandleToGrid(world, grid_handle), chunk_coord);
        }

        void HandleGeometryUpdate(
            typename Traits::WorldRef world,
            // This is `DirtyChunkLists<__>::System::Chunk<__>::ComputeConnectedComponentsReusedData`.
            auto &reused
        )
        {
            for (auto &[grid_handle, grid_entry] : grid_map)
            {
                typename Traits::GridRef grid_ref = Traits::HandleToGrid(world, grid_handle);
                auto &grid_data = Traits::GridToData(grid_ref);

                // If true, a border chunk (at `i`th side) was destroyed and maybe we should shrink the chunk grid in this direction.
                bool shrink_side[4]{};

                auto &chunk_coord_list = grid_entry.chunk_coords[BitToIndex(ChunkDirtyFlags::geometry_changed)];
                for (auto chunk_coord : chunk_coord_list)
                {
                    auto &chunk = grid_data.GetChunk(chunk_coord);

                    if (!chunk)
                        continue; // No such chunk, ignore it.

                    chunk->OnlyUpdateComponents(reused);

                    // Destroy the chunk if empty.
                    if (chunk->components.components.empty())
                    {
                        chunk = nullptr;

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
                        typename Traits::CoordInsideChunk len = new_bounds.size()[is_vertical_edge];

                        vec2<typename Traits::CoordInsideChunk> pos(
                            i == 0 ? new_bounds.b.x - 1 : new_bounds.a.x,
                            i == 1 ? new_bounds.b.y - 1 : new_bounds.a.y
                        );

                        bool ok = true;

                        for (typename Traits::CoordInsideChunk i = 0; i < len; i++)
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
                            new_bounds = new_bounds.shrink_dir(vec2<typename Traits::CoordInsideChunk>::dir4(i));
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
                        Traits::DestroyGrid(world, grid_handle, grid_ref);
                    else
                        grid_data.chunks.resize(new_bounds);
                }

                chunk_coord_list.clear();
            }
        }

        void HandleEdgesUpdate(
            typename Traits::WorldRef world,
            System::ComputeConnectivityBetweenChunksReusedData &reused
        )
        {
            for (auto &[grid_handle, grid_entry] : grid_map)
            {
                typename Traits::GridRef grid_ref = Traits::HandleToGrid(world, grid_handle);
                auto &grid_data = Traits::GridToData(grid_ref);

                auto &chunk_coord_list_0 = grid_entry.chunk_coords[BitToIndex(ChunkDirtyFlags::update_edge_0)];
                auto &chunk_coord_list_1 = grid_entry.chunk_coords[BitToIndex(ChunkDirtyFlags::update_edge_1)];

                for (bool vertical : {false, true})
                {
                    auto &list = vertical ? chunk_coord_list_1 : chunk_coord_list_0;

                    for (auto chunk_coord : list)
                    {
                        auto *chunk_a = grid_data.GetChunk(chunk_coord);
                        auto *chunk_b = grid_data.GetChunk(chunk_coord + vec2<typename Traits::WholeChunkCoord>::dir4(vertical));

                        System::ComputeConnectivityBetweenChunks(reused, chunk_a ? &chunk_a->components : nullptr, chunk_b ? &chunk_b->components : nullptr, vertical);
                    }
                }

                chunk_coord_list_0.clear();
                chunk_coord_list_1.clear();
            }
        }

        #error need the grid-splitting func
    };
}
