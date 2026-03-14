# Q-CAST Routing Protocol Implementation

## Overview

Q-CAST (Contention-Free pAth Selection at runTime) is a quantum routing protocol designed for quantum repeater networks with concurrent requests. It combines:

1. **G-EDA (Greedy Extended Dijkstra Algorithm)**: For selecting primary paths without resource contention
2. **Recovery Path Discovery**: For local fault recovery
3. **XOR-based Recovery**: Efficient recovery from link failures
4. **Logarithmic-time Swap Scheduling**: Minimizes entanglement swapping steps

## Architecture Integration

```
Q-CAST Application
        ↓
QuantumNetworkLayer
        ↓ (delegates routing)
QCastRoutingProtocol
        ↓ (inherits from)
QuantumRoutingProtocol (interface)
```

## Key Components

### 1. QCastLabel

Represents a path from source to current node in EDA:

```cpp
struct QCastLabel {
    std::string node;                    // Current node
    double expectedThroughput;           // E_t value
    double pathWidth;                    // Bottleneck capacity
    uint32_t hopCount;                   // Number of hops
    std::vector<std::string> path;       // Node sequence
    std::vector<Ptr<QuantumChannel>> channels;
};
```

### 2. QCastPath

Complete path information including recovery paths:

```cpp
struct QCastPath {
    std::vector<std::string> primaryPath;
    std::vector<Ptr<QuantumChannel>> primaryChannels;
    double primaryEt;
    
    // Recovery paths indexed by (start_idx, end_idx)
    std::map<std::pair<uint32_t, uint32_t>, std::vector<std::string>> recoveryPaths;
    std::map<std::pair<uint32_t, uint32_t>, std::vector<Ptr<QuantumChannel>>> recoveryChannels;
    
    std::vector<double> linkProbabilities;
};
```

### 3. QCastRoutingProtocol

Main protocol class implementing Q-CAST algorithm.

## Algorithm Details

### Extended Dijkstra Algorithm (EDA)

Finds the E_t-optimal path using label setting with Pareto dominance:

```cpp
std::unique_ptr<QCastLabel> ExtendedDijkstra(
    const std::string &src, 
    const std::string &dst,
    const std::set<std::string> &availableNodes,
    const std::set<std::pair<std::string, std::string>> &availableEdges
);
```

**Key Features**:
- Priority queue ordered by E_t (descending)
- Each node maintains Pareto-optimal labels
- Dominance check: Label1 dominates Label2 if
  - E_t1 >= E_t2 AND
  - Width1 >= Width2 AND
  - Hops1 <= Hops2

### Expected Throughput Calculation

```cpp
E_t(new) = E_t(current) × p_next_hop × S(h+1)/S(h)

where S(h) = exp(-α × log₂(h)) for h > 1
      S(1) = 1.0
```

### G-EDA (Greedy EDA)

Handles concurrent requests:

```cpp
std::map<uint32_t, QCastPath> CalculateRoutesGEDA(
    const std::vector<QCastRequest> &requests
);
```

**Algorithm**:
1. Initialize available resources (nodes and edges)
2. While requests remain:
   - For each request, run EDA to find best path
   - Select path with maximum E_t
   - Reserve resources for selected path
   - Discover recovery paths
   - Remove satisfied request
3. Return selected paths

### Recovery Path Discovery

Finds backup paths connecting nodes within k hops on primary path:

```cpp
std::map<std::pair<uint32_t, uint32_t>, std::vector<std::string>>
DiscoverRecoveryPaths(
    const std::vector<std::string> &primaryPath,
    const std::set<std::string> &availableNodes,
    const std::set<std::pair<std::string, std::string>> &availableEdges
);
```

**Strategy**:
- For each pair (i, j) where j-i <= k and j-i >= 2
- Find shortest path using BFS
- Reserve resources for recovery paths

### XOR-based Recovery

Activates recovery rings based on failed links:

```cpp
std::vector<QCastRecoveryRing> ExecuteXORRecovery(
    uint32_t pathId,
    const std::set<uint32_t> &failedLinks
);
```

**Logic**:
- Check if recovery path covers any failed links
- Activate recovery rings that can bypass failures
- Multiple rings can be activated simultaneously

### Logarithmic-time Swap Scheduling

Generates O(log h) schedule for entanglement swapping:

```cpp
std::vector<std::pair<uint32_t, uint32_t>> GenerateSwapSchedule(
    const std::vector<std::string> &pathNodes
);
```

**Schedule**:
- Round 1: Nodes at indices 1, 3, 5, ... swap
- Round 2: Nodes at indices 2, 6, 10, ... swap
- Round i: Nodes at step 2^i swap
- Total rounds: ⌈log₂(n)⌉

## Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| MaxHops | uint32_t | 10 | Maximum allowed hops |
| KHop | uint32_t | 3 | k-hop neighborhood for recovery |
| Alpha | double | 0.1 | Swap failure rate constant |
| NodeCapacity | uint32_t | 5 | Concurrent paths per node |

## Usage Example

```cpp
// Create Q-CAST routing protocol
Ptr<QCastRoutingProtocol> qcast = CreateObject<QCastRoutingProtocol>();
qcast->SetKHop(3);
qcast->SetAlpha(0.1);
qcast->SetNodeCapacity(5);

// Set on network layer
netLayer->SetRoutingProtocol(qcast);
netLayer->Initialize();

// Create requests
std::vector<QCastRequest> requests;
QCastRequest req;
req.srcNode = "Node0";
req.dstNode = "Node5";
req.minFidelity = 0.85;
req.requestId = 1;
requests.push_back(req);

// Run G-EDA
auto paths = qcast->CalculateRoutesGEDA(requests);

// Access results
for (const auto& entry : paths) {
    const QCastPath& path = entry.second;
    std::cout << "Route: " << qcast->RouteToString(path.primaryPath) << std::endl;
    std::cout << "E_t: " << path.primaryEt << std::endl;
    std::cout << "Recovery paths: " << path.recoveryPaths.size() << std::endl;
    
    // Get swap schedule
    auto schedule = qcast->GenerateSwapSchedule(path.primaryPath);
}

// Execute recovery for failed links
std::set<uint32_t> failedLinks = {1};  // Link 1 failed
auto rings = qcast->ExecuteXORRecovery(pathId, failedLinks);
```

## Running the Example

```bash
# Basic run
./ns3 run q-cast-example

# With parameters
./ns3 run "q-cast-example --kHop=4 --alpha=0.05 --capacity=10"

# With logging
NS_LOG="QCastRoutingProtocol=info:QuantumNetworkLayer=info" ./ns3 run q-cast-example
```

## Complexity Analysis

- **EDA**: O(|V| × |E| × L) where L is labels per node
- **G-EDA**: O(|R| × EDA) for R requests
- **Recovery Discovery**: O(k × |P| × BFS) for path P
- **Swap Schedule**: O(log h) for h hops

## Advantages

1. **Contention-Free**: G-EDA ensures no resource conflicts
2. **Local Recovery**: k-hop recovery avoids global coordination
3. **Efficient Swapping**: Logarithmic schedule minimizes decoherence
4. **Concurrent Handling**: Processes multiple requests simultaneously
5. **Adaptive**: Reacts to topology changes and link failures

## Future Enhancements

1. **Dynamic Link Probabilities**: Adapt to real-time link quality
2. **Multi-path Routing**: Use multiple paths per request
3. **Priority Classes**: Support different service levels
4. **Distributed Implementation**: Remove need for global coordination
5. **Machine Learning**: Learn optimal parameters from network data

## References

Based on quantum network routing research on contention-free path selection and XOR-based recovery strategies for quantum repeater networks.
