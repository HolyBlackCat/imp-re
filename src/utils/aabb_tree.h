#pragma once

#include <concepts>
#include <string>

#include "macros/finally.h"
#include "program/platform.h"
#include "utils/mat.h"
#include "utils/sparse_set.h"

// This is a rewrite of box2d's b2DynamicTree by Erin Catto,
// which was in turn inspired by BulletPhysics's btDbvt by Nathanael Presson.
// (This probably refers to `struct b3DynamicBvh`?)
// See the original box2d license at the bottom.

#ifndef IMP_AUTO_VALIDATE_AABB_TREES
#  if IMP_PLATFORM_IS(prod)
#    define IMP_AUTO_VALIDATE_AABB_TREES 0
#  else
#    define IMP_AUTO_VALIDATE_AABB_TREES 1
#  endif
#endif

// `T` is a vector type, either integral or floating-point.
// Only 2D vectors have been tested properly, the cost heuristics may not work in higher dimensions.
// `UserData` is an arbitrary type, an instance of which will be stored in each node.
// Keep it small, since it'll also be stored in internal non-leaf nodes, and copied around on a whim.
template <Math::vector T, typename UserData = char>
class AabbTree
{
  public:
    using vector = T;
    using scalar = typename T::type;
    using user_data = UserData;

    struct Params
    {
        Params() {}
        Params(T extra_margin) : extra_margin(extra_margin), shrink_margin(extra_margin * 4) {}

        // AABBs are initially extended by this margin, which allows not updating the tree when an object moves slightly.
        T extra_margin;

        // This is applied on top of `extra_margin`. When an AABB shrinks more than this amount, the tree is updated.
        // Box2d forces this to `4 * extra_margin`.
        T shrink_margin;

        // You can pass velocity to `MoveNode` to predictively extend the AABB in the specified direction. The extension is multiplied by this amount.
        T velocity_margin_factor = T(1);

        // This one is moot. It specifies the max allowed height difference between tree branches, before the tree rebalances itself.
        // Box2d has this fixed as 1, but this causes the tree to oscillate on change in some configurations.
        // It's unclear if it's harmful or not though. Setting this to 2 fixes oscillations, but again, it's unclear if it's helpful or not.
        // Must be >= 1. Values larger than 2 seem to be useless.
        int balance_threshold = 1;
    };

    // Constructs an null/invalid tree. Use the other constructor to make a proper one.
    constexpr AabbTree() {}

    // Makes an empty tree.
    AabbTree(Params params) : params(std::move(params)) {}

    struct Aabb
    {
        T a; // Inclusive.
        T b; // Exclusive. Greater or equal to `a`.

        [[nodiscard]] friend constexpr bool operator==(const Aabb &, const Aabb &) = default;

        // Returns the smallest AABB containing both this one and `other`.
        [[nodiscard]] Aabb Combine(const Aabb &other) const
        {
            Aabb ret;
            ret.a = min(a, other.a);
            ret.b = max(b, other.b);
            return ret;
        }

        // This is used to judge the size of the AABB.
        // Unclear if a different function would work.
        [[nodiscard]] scalar GetPerimeter() const
        {
            return (b - a).sum() * 2;
        }

        // Expands the AABB by `value`, uniformly in all directions.
        // The `value` can be negative. Decreasing the size can't make it smaller than 0.
        [[nodiscard]] Aabb Expand(T value) const
        {
            Aabb ret;
            ret.a = a - value;
            ret.b = b + value;
            if (ret.a(any) > ret.b)
                ret.a = ret.b = (ret.a + ret.b) / 2;
            return ret;
        }

        // Expands the AABB by `value`, in one direction (depending on the sign on `value`).
        [[nodiscard]] Aabb ExpandInDir(T value) const
        {
            Aabb ret = *this;
            for (int i = 0; i < 2; i++)
                (value[i] < 0 ? ret.a : ret.b) += value;
            return ret;
        }

        // Returns true if `other` is fully contained in this AABB, inclusive.
        [[nodiscard]] bool Contains(const Aabb &other) const
        {
            return other.a(all) >= a && other.b(all) <= b;
        }

        // Returns true if `point` is contained in this AAB, inclusive.
        [[nodiscard]] bool ContainsPoint(T point) const
        {
            return a(all) <= point && b(all) > point; // Sic, note `>`. `b` is exclusive, but `a` isn't.
        }

        // Returns true if this AABB intersects with `other` in any way.
        [[nodiscard]] bool Intersects(const Aabb &other) const
        {
            return a(all) < other.b && b(all) > other.a;
        }
    };

    // Creates a new node. Returns the node index.
    // `suggested_index` allows you to force a specific index. Mostly for internal use.
    [[nodiscard]] int AddNode(Aabb new_aabb, UserData new_data = {}, int new_index = null_index) noexcept
    {
        #if IMP_AUTO_VALIDATE_AABB_TREES
        FINALLY{Validate();};
        #endif

        sort_two_var(new_aabb.a, new_aabb.b);
        new_aabb = new_aabb.Expand(params.extra_margin);

        ASSERT(new_index == null_index || !node_set.Contains(new_index));
        if (new_index == null_index)
        {
            ReserveMoreIfFull();
            new_index = node_set.InsertAny();
        }
        else
        {
            ASSERT(node_set.Insert(new_index));
        }

        // Don't want to create a reference to `nodes[new_index]` yet, since it can become dangling later.
        nodes[new_index] = {}; // Reset the node.
        nodes[new_index].aabb = new_aabb;
        // nodes[new_index].moved = true; // See `struct Node` below.
        nodes[new_index].userdata = std::move(new_data);

        if (node_set.ElemCount() == 1)
        {
            root_index = new_index;
            return new_index;
        }

        // We'll need a parent node, reserve space for it now.
        ReserveMoreIfFull();
        // Now we can safely create references.
        Node &new_node = nodes[new_index];


        // Find the insertion place.

        int sibling_index = root_index;
        while (!nodes[sibling_index].IsLeaf())
        {
            const Node &sibling_node = nodes[sibling_index];

            scalar combined_area = new_aabb.Combine(sibling_node.aabb).GetPerimeter();

            // Original comment said:
            // "Cost of creating a new parent for this node and the new leaf"
            scalar sibling_cost = 2 * combined_area;

            // Original comment said:
            // "Minimum cost of pushing the leaf further down the tree"
            scalar inheritance_cost = 2 * (combined_area - sibling_node.aabb.GetPerimeter());

            // Some unknown heuristic...
            scalar child_costs[2];
            for (int i = 0; i < 2; i++)
            {
                scalar &child_cost = child_costs[i];
                const Node &child_node = nodes[sibling_node.children[i]];
                child_cost = inheritance_cost + new_aabb.Combine(child_node.aabb).GetPerimeter();
                if (!child_node.IsLeaf())
                    child_cost -= child_node.aabb.GetPerimeter();
            }

            if (sibling_cost < child_costs[0] && sibling_cost < child_costs[1])
                break; // Will insert here.

            if (child_costs[0] <= child_costs[1])
                sibling_index = sibling_node.children[0];
            else
                sibling_index = sibling_node.children[1];
        }


        // Insert the new node.

        Node &sibling_node = nodes[sibling_index];

        int old_parent_index = sibling_node.parent;

        int new_parent_index = node_set.InsertAny();
        Node &new_parent_node = nodes[new_parent_index];

        new_parent_node = {};
        new_parent_node.parent = old_parent_index;
        new_parent_node.aabb = new_aabb.Combine(sibling_node.aabb);
        new_parent_node.height = sibling_node.height + 1;

        if (old_parent_index == null_index)
        {
            // The sibling was the root node.
            root_index = new_parent_index;
        }
        else
        {
            Node &old_parent_node = nodes[old_parent_index];
            if (old_parent_node.children[0] == sibling_index)
                old_parent_node.children[0] = new_parent_index;
            else
                old_parent_node.children[1] = new_parent_index;
        }

        new_parent_node.children[0] = sibling_index;
        new_parent_node.children[1] = new_index;
        sibling_node.parent = new_parent_index;
        new_node.parent = new_parent_index;

        // Insertion finished.
        // Now we need to fix AABBs and heights of all parents.
        FixNodeAndParents(new_parent_index);

        return new_index;
    }

    // Removes a node. Returns false if the index is invalid.
    bool RemoveNode(int target_index) noexcept
    {
        if (!node_set.Contains(target_index))
            return false;

        #if IMP_AUTO_VALIDATE_AABB_TREES
        FINALLY{Validate();};
        #endif

        if (target_index == root_index)
        {
            node_set.EraseUnordered(target_index);
            root_index = null_index;
            return true;
        }

        int parent = nodes[target_index].parent;
        int grand_parent = nodes[parent].parent;

        int sibling;
        if (nodes[parent].children[0] == target_index)
            sibling = nodes[parent].children[1];
        else
            sibling = nodes[parent].children[0];

        if (grand_parent == null_index)
        {
            root_index = sibling;
            nodes[sibling].parent = null_index;
            node_set.EraseUnordered(parent);
        }
        else
        {
            // Destroy parent and connect `sibling` to `grand_parent`.
            if (nodes[grand_parent].children[0] == parent)
                nodes[grand_parent].children[0] = sibling;
            else
                nodes[grand_parent].children[1] = sibling;
            nodes[sibling].parent = grand_parent;
            node_set.EraseUnordered(parent);

            // Adjust ancestor bounds.
            FixNodeAndParents(grand_parent);
        }

        node_set.EraseUnordered(target_index);

        return true;
    }

    // Modifies a node.
    // `new_velocity` is used to predictively expand AABB in the specified direction,
    // it's multiplied by `params.velocity_margin_factor`.
    void ModifyNode(int target_index, Aabb new_aabb, T new_velocity)
    {
        ASSERT(node_set.Contains(target_index));

        Node &node = nodes[target_index];
        ASSERT(node.IsLeaf());

        sort_two_var(new_aabb.a, new_aabb.b);
        Aabb large_aabb = new_aabb.ExpandInDir(new_velocity * params.velocity_margin_factor);

        if (node.aabb.Contains(new_aabb))
        {
            // The new rect fits inside the existing one.
            // Check if we should shrink the rect.
            Aabb extra_large_aabb = large_aabb.Expand(params.extra_margin + params.shrink_margin);
            if (extra_large_aabb.Contains(node.aabb))
                return; // No shrink needed.

            // Shrinking is needed.
        }

        // The existing rect is either too big or too small.

        UserData userdata = std::move(node.userdata);

        RemoveNode(target_index);
        (void)AddNode(large_aabb, std::move(userdata), target_index);
    }

    // Returns arbitrary user data for the node.
    [[nodiscard]] UserData &GetNodeUserData(int node_index)
    {
        return const_cast<UserData &>(std::as_const(*this).GetNodeUserData(node_index));
    }
    [[nodiscard]] const UserData &GetNodeUserData(int node_index) const
    {
        ASSERT(node_set.Contains(node_index));
        return nodes[node_index].userdata;
    }

    // Returns the AABB of a node. It might be larger than the requested AABB.
    // For debug purposes, you can give it IDs of non-leaf nodes, which can only be obtained by calling `Nodes()`.
    [[nodiscard]] Aabb GetNodeAabb(int node_index) const
    {
        ASSERT(node_set.Contains(node_index));
        return nodes[node_index].aabb;
    }

    // A point collision test.
    // `func` is `bool func(int node)`. It's called for all colliding nodes. If it returns true, the function stops immediately and also returns true.
    // Since we expand AABBs, you might get false positive nodes. Manually check if the collision is exact.
    template <typename F>
    bool CollidePoint(T point, F &&func) const
    {
        return CollideCustom([&point](const Aabb &aabb){return aabb.ContainsPoint(point);}, std::forward<F>(func));
    }

    // An AABB collision test.
    // `func` is `bool func(int node)`. It's called for all colliding nodes. If it returns true, the function stops immediately and also returns true.
    // Since we expand AABBs, you might get false positive nodes. Manually check if the collision is exact.
    template <typename F>
    bool CollideAabb(Aabb aabb, F &&func) const
    {
        sort_two_var(aabb.a, aabb.b);
        return CollideCustom([&aabb](const Aabb &node_aabb){return aabb.Intersects(node_aabb);}, std::forward<F>(func));
    }

    // A custom collision test.
    // `check_collision` is `bool check_collision(const Aabb &aabb)`. If it returns true, this AABB will be examined further.
    // `func` is `bool func(int node)`. It's called for all colliding nodes. If it returns true, the function stops immediately and also returns true.
    // Since we expand AABBs, you might get false positive nodes. Manually check if the collision is exact.
    template <typename C, typename F>
    bool CollideCustom(C &&check_collision, F &&func) const
    {
        static constexpr bool (*lambda)(const AabbTree &, int, C &, F &) =
        [](const AabbTree &self, int node_index, C &check_collision, F &func) -> bool
        {
            const Node &node = self.nodes[node_index];
            if (check_collision(std::as_const(node.aabb)))
            {
                if (node.IsLeaf())
                {
                    if (func(std::as_const(node_index)))
                        return true;
                }
                else
                {
                    if (lambda(self, node.children[0], check_collision, func))
                        return true;
                    if (lambda(self, node.children[1], check_collision, func))
                        return true;
                }
            }
            return false;
        };
        return root_index == null_index ? false : lambda(*this, root_index, check_collision, func);
    }

    // Performs some internal tests. Throws on failure.
    // In the debug builds this is called automatically as needed.
    void Validate()
    {
        if (root_index != null_index)
            ValidateNode(root_index);
    }

    // Reserves memory for a specific number of nodes.
    void Reserve(int new_capacity)
    {
        if (new_capacity < node_set.Capacity())
            return; // Can't decrease capacity.

        node_set.Reserve(new_capacity);
        nodes.resize(new_capacity);
    }
    // Lets you look at the node set, mostly for debug purposes.
    [[nodiscard]] const SparseSet<int> &Nodes() const
    {
        return node_set;
    }

    // Draws the tree with text.
    [[nodiscard]] std::string DebugToString() const
    {
        if (root_index == null_index)
            return "empty";
        std::string ret;
        auto lambda = [&](auto &lambda, int node, int level) -> void
        {
            ret += std::string(level * 4, ' ') + std::to_string(node);
            if (!nodes[node].IsLeaf())
            {
                ret += '\n';
                lambda(lambda, nodes[node].children[0], level + 1);
                ret += '\n';
                lambda(lambda, nodes[node].children[1], level + 1);
            }
        };
        lambda(lambda, root_index, 0);
        return ret;
    }

  private:
    static constexpr int null_index = -1;

    Params params;

    SparseSet<int> node_set;

    int root_index = null_index;

    struct Node
    {
        Aabb aabb;
        // The height of the sub-tree.
        int height = 0;

        // Box2d sets this to true when a leaf node is created or moved.
        // I don't see any use for it yet, so it's commented out.
        // bool moved = false;

        int parent = null_index;
        int children[2] = {null_index, null_index};

        // Arbitrary data.
        [[no_unique_address]] UserData userdata;

        [[nodiscard]] bool IsLeaf() const
        {
            return children[0] == null_index;
        }
    };
    std::vector<Node> nodes;

    // Increases the capacity if we're full.
    void ReserveMoreIfFull()
    {
        if (node_set.IsFull())
            Reserve((node_set.Capacity() + 1) * 3 / 2);
    }

    // Performs left or right rotation on the node `ia`, if it's not balanced.
    // Returns the node that replaced it, or `ia` if nothing was changed.
    [[nodiscard]] int BalanceNode(int ia)
    {
        ASSERT(ia != null_index);

        Node &a = nodes[ia];
        if (a.IsLeaf() || a.height < 2)
            return ia;

        int ib = a.children[0];
        int ic = a.children[1];
        ASSERT(node_set.Contains(ib));
        ASSERT(node_set.Contains(ic));

        Node &b = nodes[ib];
        Node &c = nodes[ic];

        int balance = c.height - b.height;

        // Rotate C up.
    	if (balance > params.balance_threshold)
    	{
    		int id = c.children[0];
    		int ie = c.children[1];
    		Node &d = nodes[id];
    		Node &e = nodes[ie];
    		ASSERT(node_set.Contains(id));
    		ASSERT(node_set.Contains(ie));

    		// Swap A and C.
    		c.children[0] = ia;
    		c.parent = a.parent;
    		a.parent = ic;

    		// A's old parent should point to C.
    		if (c.parent != null_index)
    		{
    			if (nodes[c.parent].children[0] == ia)
    			{
    				nodes[c.parent].children[0] = ic;
    			}
    			else
    			{
    				ASSERT(nodes[c.parent].children[1] == ia);
    				nodes[c.parent].children[1] = ic;
    			}
    		}
    		else
    		{
    			root_index = ic;
    		}

    		// Rotate.
    		if (d.height > e.height)
    		{
    			c.children[1] = id;
    			a.children[1] = ie;
    			e.parent = ia;
    			a.aabb = b.aabb.Combine(e.aabb);
    			c.aabb = a.aabb.Combine(d.aabb);

    			a.height = 1 + max(b.height, e.height);
    			c.height = 1 + max(a.height, d.height);
    		}
    		else
    		{
    			c.children[1] = ie;
    			a.children[1] = id;
    			d.parent = ia;
    			a.aabb = b.aabb.Combine(d.aabb);
    			c.aabb = a.aabb.Combine(e.aabb);

    			a.height = 1 + max(b.height, d.height);
    			c.height = 1 + max(a.height, e.height);
    		}

    		return ic;
    	}

    	// Rotate B up.
    	if (balance < -params.balance_threshold)
    	{
    		int id = b.children[0];
    		int ie = b.children[1];
    		Node &d = nodes[id];
    		Node &e = nodes[ie];
    		ASSERT(node_set.Contains(id));
    		ASSERT(node_set.Contains(ie));

    		// Swap A and B.
    		b.children[0] = ia;
    		b.parent = a.parent;
    		a.parent = ib;

    		// A's old parent should point to B.
    		if (b.parent != null_index)
    		{
    			if (nodes[b.parent].children[0] == ia)
    			{
    				nodes[b.parent].children[0] = ib;
    			}
    			else
    			{
    				ASSERT(nodes[b.parent].children[1] == ia);
    				nodes[b.parent].children[1] = ib;
    			}
    		}
    		else
    		{
    			root_index = ib;
    		}

    		// Rotate
    		if (d.height > e.height)
    		{
    			b.children[1] = id;
    			a.children[0] = ie;
    			e.parent = ia;
    			a.aabb = c.aabb.Combine(e.aabb);
    			b.aabb = a.aabb.Combine(d.aabb);

    			a.height = 1 + max(c.height, e.height);
    			b.height = 1 + max(a.height, d.height);
    		}
    		else
    		{
    			b.children[1] = ie;
    			a.children[0] = id;
    			d.parent = ia;
    			a.aabb = c.aabb.Combine(d.aabb);
    			b.aabb = a.aabb.Combine(e.aabb);

    			a.height = 1 + max(c.height, d.height);
    			b.height = 1 + max(a.height, e.height);
    		}

    		return ib;
    	}

    	return ia;
    }

    // For `index` and its every parent, performs `BalanceNode` and updates their AABBs and heights.
    void FixNodeAndParents(int index)
    {
        while (index != null_index)
        {
            index = BalanceNode(index);
            Node &node = nodes[index];

            const Node &child0 = nodes[node.children[0]];
            const Node &child1 = nodes[node.children[1]];

            node.height = 1 + max(child0.height, child1.height);
            node.aabb = child0.aabb.Combine(child1.aabb);

            index = node.parent;
        }
    }

    // Performs some internal tests on a node, recursively. Throws on failure.
    // Don't call direclty, use the `Validate()` function.
    void ValidateNode(int index) const
    {
        ASSERT_ALWAYS(index != null_index);

        const Node &node = nodes[index];

        ASSERT_ALWAYS((index == root_index) == (node.parent == null_index));

        if (node.IsLeaf())
        {
            ASSERT_ALWAYS(node.children[0] == null_index);
            ASSERT_ALWAYS(node.children[1] == null_index);
            ASSERT_ALWAYS(node.height == 0);
        }
        else
        {
            ASSERT_ALWAYS(node_set.Contains(node.children[0]));
            ASSERT_ALWAYS(node_set.Contains(node.children[1]));

            const Node &child0 = nodes[node.children[0]];
            const Node &child1 = nodes[node.children[1]];

            ASSERT_ALWAYS(child0.parent == index);
            ASSERT_ALWAYS(child1.parent == index);

            ASSERT_ALWAYS(node.height == 1 + max(child0.height, child1.height));
            ASSERT_ALWAYS(node.aabb == child0.aabb.Combine(child1.aabb));

            ValidateNode(node.children[0]);
            ValidateNode(node.children[1]);
        }
    }
};

// License for the original Box2D code:
// ----- [

// MIT License

// Copyright (c) 2019 Erin Catto

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// ] -----
