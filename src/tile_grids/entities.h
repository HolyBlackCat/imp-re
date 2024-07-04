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
    //     // This is called when splitting a grid, after copying the tiles so you can finish the initialization of `to` by e.g. copying the grid
    //     // coordinates or whatever.
    //     static void FinishGridInitAfterSplit(typename EntityTag::Controller& world, const GridEntity &from, GridEntity &to);
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
    //     [[nodiscard]] static ChunkGrid<__> &GridToData(GridEntity &grid)
    template <typename BaseTraits>
    struct EntityHighLevelTraits : BaseTraits
    {
        using WorldRef = BaseTraits::EntityTag::Controller &;
        using GridHandle = BaseTraits::EntityTag::Id;
        using GridRef = BaseTraits::GridEntity &;

        [[nodiscard]] static GridRef HandleToGrid(WorldRef world, GridHandle grid) {return world.get(grid).template get<typename BaseTraits::GridEntity>();}


        // Removes a grid from the world, when it was shrinked into nothing. Having both `grid_handle` and `grid_ref` is redundant.
        static void DestroyGrid(WorldRef world, GridHandle grid_handle, GridRef grid_ref)
        {
            (void)grid_handle;
            world.destroy(grid_ref);
        }

        // Creates a new grid, splitting it from `grid`.
        // Must call `init(...)` once with a `GridRef` parameter of a new grid that will be filled with the chunks.
        static void SplitGrid(WorldRef world, GridRef grid, auto init)
        {
            typename BaseTraits::GridEntity &new_grid = world.template create<typename BaseTraits::GridEntity>();
            init(new_grid);
            BaseTraits::FinishGridInitAfterSplit(world, grid, new_grid);
        }
    };
}
