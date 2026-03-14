# Quantum Network Simulator Implementation

## Overview

This document describes the implementation details of the quantum network simulator architecture, including the design philosophy, class interfaces, and interactions between layers.

## Architecture Philosophy

### Layer Separation

The simulator follows a strict layered architecture with clear interfaces between layers:

```
┌─────────────────────────────────────────────┐
│           Quantum Applications               │
├─────────────────────────────────────────────┤
│  QuantumNetworkLayer (quantum-network-layer) │
│  - End-to-end path establishment             │
│  - Delegates routing to:                     │
│    QuantumRoutingProtocol (interface)        │
├─────────────────────────────────────────────┤
│  ILinkLayerService (quantum-link-layer-svc) │
│  - Neighbor-to-neighbor entanglement         │
├─────────────────────────────────────────────┤
│  QuantumPhyEntity (quantum-phy-entity)      │
│  - Physical quantum operations               │
├─────────────────────────────────────────────┤
│  ExaTN Tensor Network Backend               │
└─────────────────────────────────────────────┘
```

### Key Design Principles

1. **Interface Segregation**: Each layer exposes only necessary interfaces
2. **Dependency Inversion**: High-level modules depend on abstractions, not concrete implementations
3. **Single Responsibility**: Each class has one reason to change
4. **Separation of Concerns**: Routing algorithms are separated from network layer logic

## Class Specifications

### 1. QuantumNetworkLayer

**File**: `model/quantum-network-layer.h/cc`

**Purpose**: Main network layer class that orchestrates end-to-end quantum path establishment.

**Key Responsibilities**:
- Path setup and management
- Coordination of multi-hop entanglement
- Entanglement swapping orchestration
- Retry and timeout handling

**Public Interface**:

```cpp
class QuantumNetworkLayer : public Object {
public:
    // Lifecycle
    static TypeId GetTypeId();
    void Initialize();
    void DoDispose() override;
    
    // Dependencies
    void SetPhyEntity(Ptr<QuantumPhyEntity> qphyent);
    Ptr<QuantumPhyEntity> GetPhyEntity() const;
    
    void SetRoutingProtocol(Ptr<QuantumRoutingProtocol> routingProtocol);
    Ptr<QuantumRoutingProtocol> GetRoutingProtocol() const;
    
    void SetLinkLayer(Ptr<ILinkLayerService> linkLayer);
    Ptr<ILinkLayerService> GetLinkLayer() const;
    
    // Identity
    void SetOwner(const std::string& owner);
    std::string GetOwner() const;
    
    // Topology Management
    void AddNeighbor(const std::string& neighbor, Ptr<QuantumChannel> channel,
                     double linkFidelity, double linkSuccessRate);
    void RemoveNeighbor(const std::string& neighbor);
    void UpdateNeighborAvailability(const std::string& neighbor, bool available);
    const std::map<std::string, NeighborInfo>& GetNeighbors() const;
    
    // Path Management
    PathId SetupPath(const std::string& srcNode, const std::string& dstNode,
                     double minFidelity, PathReadyCallback callback);
    PathInfo GetPathInfo(PathId pathId) const;
    bool IsPathReady(PathId pathId) const;
    void ReleasePath(PathId pathId);
};
```

**Internal Workflow**:
1. `SetupPath()` called by application
2. Delegates route calculation to `QuantumRoutingProtocol::CalculateRoute()`
3. For each hop, requests entanglement via `ILinkLayerService`
4. Waits for all entanglements to be ready
5. Coordinates entanglement swapping across intermediate nodes
6. Notifies application via callback

**Note**: This class does NOT implement routing algorithms. All routing is delegated to the pluggable `QuantumRoutingProtocol`.

---

### 2. QuantumRoutingProtocol (Abstract Interface)

**File**: `model/quantum-routing-protocol.h/cc`

**Purpose**: Abstract base class for all routing protocols. Defines the interface that routing algorithms must implement.

**Design Rationale**: Separating routing from the network layer allows:
- Easy testing of different routing strategies
- Custom routing for specific network topologies
- Integration with external routing protocols

**Public Interface**:

```cpp
class QuantumRoutingProtocol : public Object {
public:
    static TypeId GetTypeId();
    
    // Called by network layer to set reference
    void SetNetworkLayer(Ptr<QuantumNetworkLayer> networkLayer);
    
    // Lifecycle
    virtual void Initialize() = 0;
    
    // Core routing function - MUST be implemented by subclasses
    virtual std::vector<std::string> CalculateRoute(
        const std::string& src, 
        const std::string& dst
    ) = 0;
    
    // Notification when topology changes
    virtual void NotifyTopologyChange() = 0;
    
    // Utility
    virtual std::string RouteToString(const std::vector<std::string>& route) = 0;
    
protected:
    Ptr<QuantumNetworkLayer> m_networkLayer;
};
```

**Implementation Requirements**:
- Subclasses MUST implement `CalculateRoute()`
- Subclasses SHOULD update routing tables when `NotifyTopologyChange()` is called
- Subclasses MAY cache routes for performance

---

### 3. DijkstraRoutingProtocol (Concrete Implementation)

**File**: `model/quantum-routing-protocol.h/cc`

**Purpose**: Default routing protocol implementation using Dijkstra's shortest path algorithm.

**Algorithm**:
- Uses link fidelity as edge weight: `weight = 1.0 / fidelity`
- Considers only available neighbors
- Computes shortest path in terms of cumulative weight

**Public Interface**:

```cpp
class DijkstraRoutingProtocol : public QuantumRoutingProtocol {
public:
    static TypeId GetTypeId();
    
    // Implementation of abstract interface
    void Initialize() override;
    std::vector<std::string> CalculateRoute(
        const std::string& src, 
        const std::string& dst
    ) override;
    void NotifyTopologyChange() override;
    std::string RouteToString(const std::vector<std::string>& route) override;
    
    // Dijkstra-specific configuration
    void SetMetricType(MetricType type);  // FIDELITY, HOP_COUNT, CUSTOM
    void SetCustomMetric(Callback<double, std::string, std::string> metric);
};

enum class MetricType {
    HOP_COUNT,      // Minimize number of hops
    FIDELITY,       // Maximize end-to-end fidelity
    SUCCESS_RATE,   // Maximize success probability
    CUSTOM          // User-defined metric
};
```

**Implementation Details**:
1. Queries neighbors from `QuantumNetworkLayer::GetNeighbors()`
2. Builds graph representation
3. Runs Dijkstra's algorithm
4. Reconstructs path from predecessors
5. Returns vector of node names: `[src, ..., dst]`

---

### 4. ILinkLayerService (Abstract Interface)

**File**: `model/quantum-link-layer-service.h/cc`

**Purpose**: Interface for link-layer services that provide neighbor-to-neighbor entanglement.

**Design Rationale**: Abstract interface allows different link layer implementations:
- Simple entanglement generation
- Entanglement with purification
- Quantum error correction-based links

**Public Interface**:

```cpp
class ILinkLayerService : public Object {
public:
    static TypeId GetTypeId();
    
    // Request entanglement with a neighbor
    virtual void RequestEntanglement(
        const std::string& srcNode,
        const std::string& dstNode,
        double minFidelity,
        EntanglementCallback callback
    ) = 0;
    
    // Query entanglement status
    virtual EntanglementInfo GetEntanglementInfo(EntanglementId id) const = 0;
    virtual bool IsEntanglementReady(EntanglementId id) const = 0;
    
    // Consume (use up) entanglement
    virtual bool ConsumeEntanglement(EntanglementId id) = 0;
    
    // Dependencies
    virtual void SetPhyEntity(Ptr<QuantumPhyEntity> qphyent) = 0;
    virtual Ptr<QuantumPhyEntity> GetPhyEntity() const = 0;
    virtual std::string GetOwner() const = 0;
};
```

**Callback Types**:

```cpp
typedef uint32_t EntanglementId;
typedef Callback<void, EntanglementId, EntanglementState> EntanglementCallback;

enum class EntanglementState {
    PENDING,
    READY,
    CONSUMED,
    FAILED
};

struct EntanglementInfo {
    EntanglementId id;
    std::string localNode;
    std::string remoteNode;
    std::string localQubit;
    std::string remoteQubit;
    double fidelity;
    Time createdAt;
    bool isValid;
};
```

---

### 5. QuantumPhyEntity

**File**: `model/quantum-phy-entity.h/cc`

**Purpose**: Physical layer entity that manages quantum operations via ExaTN backend.

**Key Responsibilities**:
- Qubit generation and management
- Gate application
- Measurement operations
- Error model application
- Quantum memory management

**Public Interface** (simplified):

```cpp
class QuantumPhyEntity : public Object {
public:
    // Qubit operations
    bool GenerateQubitsPure(const std::string& owner,
                           const std::vector<std::complex<double>>& data,
                           const std::vector<std::string>& qubits);
    
    // Gate operations
    bool ApplyGate(const std::string& owner, const std::string& gate,
                   const std::vector<std::complex<double>>& data,
                   const std::vector<std::string>& qubits);
    
    // Measurement
    std::pair<unsigned, std::vector<double>> Measure(
        const std::string& owner, 
        const std::vector<std::string>& qubits
    );
    
    // Partial trace
    bool PartialTrace(const std::vector<std::string>& qubits);
    
    // Node management
    Ptr<QuantumNode> GetNode(const std::string& owner);
    
    // Error models
    void SetDepolarModel(std::pair<std::string, std::string> conn, double fidel);
    void ApplyErrorModel(const std::vector<std::string>& qubits, 
                         const Time& moment = Simulator::Now());
};
```

---

### 6. Supporting Data Structures

#### PathInfo

```cpp
struct PathInfo {
    PathId id;
    std::string srcNode;
    std::string dstNode;
    std::vector<std::string> route;  // Computed by routing protocol
    double minFidelity;
    PathState state;
    uint32_t retryCount;
    std::vector<EntanglementRequestId> entanglementRequestIds;
};
```

#### NeighborInfo

```cpp
struct NeighborInfo {
    std::string neighborName;
    Ptr<QuantumChannel> channel;
    double linkFidelity;
    double linkSuccessRate;
    bool isAvailable;
};
```

---

## Workflow Details

### Path Establishment Workflow

```
Application
    ↓ calls SetupPath()
QuantumNetworkLayer
    ↓ delegates CalculateRoute()
QuantumRoutingProtocol
    ↓ returns route [A, B, C, D]
QuantumNetworkLayer
    ↓ for each hop (A-B, B-C, C-D)
    ↓ calls RequestEntanglement()
ILinkLayerService
    ↓ generates entanglement
    ↓ callback when ready
QuantumNetworkLayer
    ↓ when all hops ready
    ↓ coordinates swapping
    ↓ ExecuteSwapAtNode(B)
    ↓ ExecuteSwapAtNode(C)
    ↓ FinalizePath()
    ↓ callback to Application
```

### Entanglement Swapping Coordination

For path A → B → C → D:

1. **Link Layer Phase**: Establish A-B, B-C, C-D entanglements
2. **Swapping Phase**:
   - Node B performs Bell State Measurement (BSM) on its two qubits
   - Node B sends classical results to A
   - Node C performs BSM on its two qubits
   - Node C sends classical results to D
3. **Correction Phase**:
   - Nodes A and D apply Pauli corrections based on received results
4. **Result**: End-to-end entanglement between A and D

---

## Shared Discrete Timeline

All operations use ns-3's discrete event scheduler:

```cpp
// Scheduling quantum operations
Simulator::Schedule(delay, &QuantumPhyEntity::ApplyGate, ...);

// Scheduling classical communication
Simulator::Schedule(delay, &Socket::Send, ...);

// Timeout handling
EventId timeout = Simulator::Schedule(timeoutDelay, 
                                       &QuantumNetworkLayer::HandlePathTimeout, 
                                       this, pathId);

// Canceling scheduled events
timeout.Cancel();
```

This ensures quantum events (measurements, swaps) and classical events (routing messages, ACKs) are interleaved correctly on a unified timeline.

---

## Configuration Attributes

### QuantumNetworkLayer

| Attribute | Type | Default | Description |
|-----------|------|---------|-------------|
| Owner | String | "" | Node name |
| MaxRetries | Uinteger | 3 | Max path setup retries |
| RetryInterval | Time | 100ms | Delay between retries |
| EntanglementTimeout | Time | 1s | Timeout for link entanglement |

### DijkstraRoutingProtocol

| Attribute | Type | Default | Description |
|-----------|------|---------|-------------|
| MetricType | Enum | FIDELITY | Routing metric |

---

## Usage Example

```cpp
// 1. Create physical entity
Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(owners);

// 2. Create network layer
Ptr<QuantumNetworkLayer> netLayer = CreateObject<QuantumNetworkLayer>();
netLayer->SetOwner("Node0");
netLayer->SetPhyEntity(qphyent);

// 3. Create and set routing protocol (optional, will use default if not set)
Ptr<DijkstraRoutingProtocol> routing = CreateObject<DijkstraRoutingProtocol>();
netLayer->SetRoutingProtocol(routing);

// 4. Set link layer
Ptr<ILinkLayerService> linkLayer = CreateObject<SomeLinkLayerImpl>();
netLayer->SetLinkLayer(linkLayer);

// 5. Initialize
netLayer->Initialize();

// 6. Add neighbors
netLayer->AddNeighbor("Node1", channel, 0.95, 0.9);

// 7. Setup path
PathReadyCallback callback = MakeCallback(&OnPathReady);
PathId pathId = netLayer->SetupPath("Node0", "Node3", 0.85, callback);
```

---

## Extending the Architecture

### Adding a New Routing Protocol

1. Create class inheriting from `QuantumRoutingProtocol`
2. Implement `CalculateRoute()`
3. Implement other pure virtual methods
4. Set on network layer via `SetRoutingProtocol()`

Example:

```cpp
class LinkStateRouting : public QuantumRoutingProtocol {
public:
    std::vector<std::string> CalculateRoute(
        const std::string& src, 
        const std::string& dst
    ) override {
        // Implement link-state algorithm
        // Use m_networkLayer->GetNeighbors() for topology
    }
    // ... other implementations
};
```

### Adding a New Link Layer

1. Create class inheriting from `ILinkLayerService`
2. Implement entanglement generation logic
3. Invoke callback when entanglement is ready

---

## Testing Strategy

1. **Unit Tests**: Test each class independently
2. **Integration Tests**: Test layer interactions
3. **Routing Tests**: Test different routing protocols
4. **End-to-End Tests**: Full path establishment tests

---

## Performance Considerations

1. **Route Caching**: Routing protocols may cache computed routes
2. **Lazy Evaluation**: Defer expensive operations until needed
3. **Timer Management**: Cancel timers when no longer needed
4. **Memory Management**: Release paths when done to free resources

---

## Future Enhancements

1. **Dynamic Routing**: React to topology changes in real-time
2. **Multi-path**: Support multiple simultaneous paths
3. **QoS**: Quality of service classes with different priorities
4. **Congestion Control**: Monitor and react to link congestion
5. **Hierarchical Routing**: Support for large-scale networks
