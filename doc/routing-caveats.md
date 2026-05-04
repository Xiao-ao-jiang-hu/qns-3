# Routing Caveats

This note records current limitations and semantics that matter when
interpreting the routing examples and the sliced-Dijkstra prototype.

## 1. Topology model is a simple graph, not a multigraph

The routing topology is currently represented as:

```cpp
std::map<std::string, std::map<std::string, LinkMetrics>>
```

That means each ordered node pair `(u, v)` stores exactly one
`LinkMetrics` record.

Practical consequence:

- Re-inserting the same neighbor pair overwrites the previous link metrics.
- The current routing layer does **not** support parallel edges between the
  same two nodes.

Example:

```cpp
AddBidirectionalLink (topology, "A", "B", MakeLinkMetrics (0.98, 60.0));
AddBidirectionalLink (topology, "A", "B", MakeLinkMetrics (0.99, 70.0));
```

After the second call, only the `70 ms` edge remains visible to routing.
The `60 ms` edge is not preserved as a second candidate.

Supporting true parallel edges would require structural changes:

- topology storage would need an edge list per neighbor, not a single
  `LinkMetrics`
- route expansion would need to enumerate multiple edges to the same
  neighbor
- final route representation would need edge identity, not only node names

## 2. K-sliced Dijkstra uses fixed-width time buckets

`SlicedDijkstraRoutingProtocol` currently slices the state space using a
fixed `BucketWidthMs` parameter together with a per-node capacity limit `K`.

Current semantics:

- `BucketWidthMs` is an absolute time resolution chosen by the user
- `bucket_id = floor(t_max_ms / BucketWidthMs)`
- labels in different buckets may coexist at the same node
- labels in the same bucket compete with each other
- `K` is the maximum number of retained labels per node after admission

This is **not** the same as computing `K` buckets automatically from the
observed `[min_t, max_t]` range of the current search.

Why this matters:

- `BucketWidthMs` expresses a modeling choice about which timing differences
  should be considered meaningfully distinct
- bucket semantics stay stable across runs and topologies
- the routing search does not need to recompute bucket boundaries as new
  labels reveal larger or smaller `t_max`

If automatic `[min_t, max_t] -> K slices` behavior is desired, that would be
a different slicing policy and would need a separate implementation.

## 3. Example-level physical validation can add timing not present in the metric

Some examples compare:

- the routing metric used by the algorithm
- a more detailed end-to-end physical simulation used only for validation

When that happens, the validation path may include extra physical timing
effects, such as classical signaling delays between sequential entanglement
swaps, without changing the routing metric itself.

This is intentional when the example is meant to answer:

"How does the route chosen by the current metric behave under a richer
physical execution model?"
