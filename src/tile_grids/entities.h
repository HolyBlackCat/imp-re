#pragma once

namespace TileGrids
{
    // Those traits extend `core.h` traits with some members needed by `high_level.h`, but not all of them.
    // `BaseSystem` must inherit from `TileGrids::System` (which can use the default template argument).
    // You then inherit from this, add the members listed below, and pass the resulting traits to `TileGrids::HighLevelSystem`.
    //
    //     // Our own stuff:
    //
    //     // The tag for the entity system.
    //     using EntityTag = ...;
    //     // The entity that will store the grid.
    //     using GridEntity = ...;
    //
    //     // This is called when splitting a grid, to create a new grid.
    //     static GridEntity &CreateSplitGrid(typename EntityTag::Controller& controller, const GridEntity &source_grid);
    //     // Then we write the tiles into it, and lastly call this to finish the initialization.
    //     static void FinishSplitGridInit(typename EntityTag::Controller& controller, const GridEntity &from, GridEntity &to);
    //
    //     // Missing members from `high_level.h` traits:
    //
    //     // Each tile of a chunk stores this.
    //     using CellType = ...;
    //     // Returns true if the cell isn't empty, for the purposes of splitting unconnected grids. Default-constructed cells must count as empty.
    //     [[nodiscard]] static bool CellIsNonEmpty(const CellType &cell);
    //     // Returns the connectivity mask of a cell in the specified direction, for the purposes of splitting unconnected grids.
    //     // The bit order should NOT be reversed when flipping direction, it's always the same.
    //     [[nodiscard]] static TileEdgeConnectivity CellConnectivity(const CellType &cell, int dir);
    //
    //     // Returns our data from a grid.
    //     [[nodiscard]] static ChunkGrid<__> &GridToData(GridEntity *grid);
    //
    //     // This is called after updating chunk contents (when handling the `geometry_changed` flag).
    //     // `comps_per_tile` is the mapping between tile coordinates to component indices.
    //     static void OnUpdateGridChunkContents(typename EntityTag::Controller& controller, GridEntity *grid, vec2<typename System::WholeChunkCoord> chunk_coord, const typename System::TileComponentIndices<N> &comps_per_tile);
    //
    //     // When splitting a grid, this is called before moving a component from chunk to a chunk of a newly created entity.
    //     static void OnPreMoveComponentBetweenChunks(typename EntityTag::Controller& controller, GridEntity *source_grid, System::ComponentCoords coords, GridEntity *target_grid);
    template <typename BaseTraits>
    struct EntityHighLevelTraits : BaseTraits
    {
        using WorldRef = BaseTraits::EntityTag::Controller &;
        using GridHandle = BaseTraits::EntityTag::Id;
        using GridRef = BaseTraits::GridEntity *; // This must be nullable.

        [[nodiscard]] static GridRef HandleToGrid(WorldRef world, GridHandle grid)
        {
            auto ptr = world.get_opt(grid);
            return ptr ? &ptr->template get<typename BaseTraits::GridEntity>() : nullptr;
        }


        // Removes a grid from the world, when it was shrinked into nothing. Having both `grid_handle` and `grid_ref` is redundant.
        static void DestroyGrid(WorldRef world, GridHandle grid_handle, GridRef grid_ref)
        {
            (void)grid_handle;
            world.destroy(*grid_ref);
        }

        // Creates a new grid, splitting it from `grid`.
        // Must call `init(...)` once with a `GridRef` parameter of a new grid that will be filled with the chunks.
        static void SplitGrid(WorldRef world, GridRef grid, auto init)
        {
            typename BaseTraits::GridEntity &new_grid = BaseTraits::CreateSplitGrid(world, *grid);
            init(&new_grid);
            BaseTraits::FinishSplitGridInit(world, *grid, new_grid);
        }
    };
}
