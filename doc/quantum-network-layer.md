# Quantum Network Layer Design

## Overview

The Quantum Network Layer provides end-to-end quantum connectivity in a quantum repeater network. It is responsible for:

- **Quantum Routing**: Finding paths between source and destination nodes based on fidelity and success rate metrics
- **Entanglement Distribution**: Coordinating with the Link Layer to establish entanglement across multiple hops
- **Entanglement Swapping**: Managing the execution of entanglement swapping at intermediate nodes
- **Path Management**: Handling path setup, retry logic, and resource cleanup

## Architecture

The network layer follows the layered architecture pattern:

```
Quantum Applications
        ↓
Quantum Network Layer (quantum-network-layer.h/cc)
        ↓ (via ILinkLayerService interface)
Quantum Link Layer (quantum-link-layer-service.h)
        ↓
Quantum Physical Layer (quantum-phy-entity.h)
```

## Key Components

### QuantumNetworkLayer Class

The main class implementing the network layer functionality. It inherits from `ILinkLayerService` to provide a unified interface.

**Key Methods:**

- `SetupPath(src, dst, minFidelity, callback)`: Initiates path establishment between source and destination
- `CalculateRoute(src, dst)`: Implements Dijkstra's algorithm for path finding
- `PerformEntanglementSwapping(pathId)`: Coordinates swapping operations across the path
- `RequestEntanglement(...)`: Passes through to the actual link layer service

### Data Structures

#### PathInfo
```cpp
struct PathInfo {
    PathId id;                          // Unique path identifier
    std::string srcNode;                // Source node name
    std::string dstNode;                // Destination node name
    std::vector<std::string> route;     // List of nodes in the path
    double minFidelity;                 // Minimum required fidelity
    PathState state;                    // PENDING, READY, or FAILED
    uint32_t retryCount;                // Number of retry attempts
    std::vector<EntanglementRequestId> entanglementRequestIds;
};
```

#### NeighborInfo
```cpp
struct NeighborInfo {
    std::string neighborName;           // Neighbor node name
    Ptr<QuantumChannel> channel;        // Quantum channel to neighbor
    double linkFidelity;                // Link fidelity metric
    double linkSuccessRate;             // Link success probability
    bool isAvailable;                   // Whether link is currently available
};
```

## Workflow

### Path Setup Process

1. **Path Request**: Application calls `SetupPath()` with source, destination, and fidelity requirements
2. **Route Calculation**: Network layer runs Dijkstra's algorithm to find the best path
3. **Entanglement Requests**: For each hop in the path, request entanglement from the link layer
4. **Wait for Ready**: Wait for all link-layer entanglements to be established
5. **Entanglement Swapping**: Coordinate swapping at intermediate nodes
6. **Path Ready**: Notify application that end-to-end entanglement is ready

### Entanglement Swapping Coordination

For a path A → B → C → D:

1. Establish A-B, B-C, and C-D entanglements via link layer
2. Node B performs Bell State Measurement on its two qubits
3. Node B sends classical results to node A
4. Node C performs Bell State Measurement on its two qubits
5. Node C sends classical results to node D
6. Nodes A and D apply correction gates based on received results
7. End-to-end entanglement between A and D is established

## Shared Discrete Timeline

The network layer leverages ns-3's discrete event scheduler (`Simulator::Schedule`) to coordinate quantum and classical operations:

- **Quantum Operations**: Entanglement generation, swapping, and measurements are scheduled events
- **Classical Communication**: Routing messages and measurement results use ns-3 sockets
- **Timeouts**: Path establishment timeouts are scheduled events
- **Retries**: Failed paths are retried after a scheduled delay

All events (quantum and classical) are processed in the same `Simulator::Run()` loop, ensuring consistent timing.

## Integration with Lower Layers

### Link Layer Interface

The network layer uses the `ILinkLayerService` interface to interact with the link layer:

```cpp
class ILinkLayerService {
    virtual void RequestEntanglement(src, dst, minFidelity, callback) = 0;
    virtual EntanglementInfo GetEntanglementInfo(id) = 0;
    virtual bool ConsumeEntanglement(id) = 0;
    virtual bool IsEntanglementReady(id) = 0;
};
```

This abstraction allows different link layer implementations (e.g., with or without purification) to be used interchangeably.

### Physical Layer Access

The network layer accesses the physical layer through `QuantumPhyEntity` for:
- Quantum memory management
- Gate operations (for swapping corrections)
- Measurement operations

## Configuration Parameters

- **MaxRetries**: Maximum number of path setup retry attempts (default: 3)
- **RetryInterval**: Delay between retry attempts (default: 100ms)
- **EntanglementTimeout**: Timeout for waiting link layer entanglement (default: 1s)

## Usage Example

```cpp
// Create and configure network layer
Ptr<QuantumNetworkLayer> netLayer = CreateObject<QuantumNetworkLayer>();
netLayer->SetOwner("Node0");
netLayer->SetPhyEntity(qphyent);

// Add neighbors
netLayer->AddNeighbor("Node1", channel, 0.95, 0.9);

// Set link layer service
netLayer->SetLinkLayerService(linkLayer);

// Request path establishment
PathReadyCallback callback = MakeCallback(&OnPathReady);
PathId pathId = netLayer->SetupPath("Node0", "Node3", 0.85, callback);

// Callback is invoked when path is ready or fails
void OnPathReady(PathId id, bool success) {
    if (success) {
        // Use the quantum path
    }
}
```

## Future Enhancements

1. **Dynamic Routing**: Support for routing table updates and dynamic path changes
2. **Congestion Control**: Link availability monitoring and congestion-aware routing
3. **Multi-path**: Support for establishing multiple parallel paths
4. **Quality of Service**: Different fidelity requirements for different traffic classes
5. **Distributed Swapping**: More efficient distributed swapping protocols
