# AGENTS.md - Quantum Model Layer

## Overview

Core quantum network simulation implementation. Contains all protocol layers, quantum operations, and ExaTN integration.

## Structure

```
model/
├── quantum-basis.h/cc              # Quantum state representation (ExaTN)
├── qubit.h/cc                      # Single qubit operations
├── quantum-memory.h/cc             # Quantum memory with decoherence
├── quantum-phy-entity.h/cc         # Physical layer entity
├── quantum-channel.h/cc            # Quantum channel (fidelity, loss)
├── quantum-node.h/cc               # Quantum node abstraction
├── quantum-link-layer-service.h/cc # Link layer entanglement
├── quantum-network-layer.h/cc      # Network layer (L3)
├── quantum-routing-protocol.h/cc   # Routing interface
├── q-cast-routing-protocol.h/cc    # Q-CAST routing implementation
├── distribute-epr-protocol.h/cc    # EPR distribution protocol
├── telep-app.h/cc                  # Teleportation application
├── ent-swap-app.h/cc               # Entanglement swapping application
├── distill-app.h/cc                # Entanglement distillation application
├── quantum-signaling-channel.h/cc  # Classical signaling delay simulation
└── quantum-delay-model.h/cc        # Delay models for signaling
```

## Key Classes

### Core Layer

| Class | Role | Key Methods |
|-------|------|-------------|
| `QuantumBasis` | Tensor network backend | `Initialize()`, `ApplyGate()` |
| `Qubit` | Single qubit state | `ApplySingleQubitGate()`, `Measure()` |
| `QuantumMemory` | Qubit storage | `StoreQubit()`, `RetrieveQubit()`, `GetDecoherence()` |
| `QuantumPhyEntity` | Physical layer manager | `GetNode()`, `CreateChannel()` |

### Network Layer

| Class | Role | Key Methods |
|-------|------|-------------|
| `QuantumNetworkLayer` | L3 protocol orchestrator | `Initialize()`, `SendEntanglementRequest()` |
| `QuantumRoutingProtocol` | Routing interface | `CalculateRoute()`, `UpdateTopology()` |
| `QCastRoutingProtocol` | Q-CAST implementation | `CalculateRoutesGEDA()`, `GenerateSwapSchedule()` |

### Applications

| Class | Role | Protocol |
|-------|------|----------|
| `TelepApp` | Teleportation | Standard teleportation |
| `EntSwapApp` | Entanglement swapping | Swapping protocol |
| `DistillApp` | Entanglement distillation | Nested distillation |
| `DistributeEPRSrcProtocol` | EPR distribution | Source protocol |

## Design Patterns

### Layered Architecture

```
Applications (telep-app, ent-swap-app)
    ↓
Network Layer (quantum-network-layer)
    ↓
Routing Protocol (q-cast-routing-protocol)
    ↓
Link Layer Service (quantum-link-layer-service)
    ↓
Physical Layer (quantum-phy-entity, quantum-memory)
    ↓
ExaTN Backend (quantum-basis, qubit)
```

### Pluggable Routing

`QuantumNetworkLayer` delegates routing to `QuantumRoutingProtocol` interface:
- `DijkstraRoutingProtocol`: Default shortest-path
- `QCastRoutingProtocol`: Q-CAST with recovery paths

### Discrete Event Simulation

All quantum operations scheduled via ns-3 `Simulator`:
- Gate operations: discrete time steps
- Decoherence: calculated per time unit
- Signaling: classical network delay simulation

## Conventions

### Member Variables

```cpp
// Private members: m_camelCase
Ptr<QuantumPhyEntity> m_qphyent;
const std::string m_owner;
uint32_t m_nextMessageId;
```

### Methods

```cpp
// Public: PascalCase
void SetAddress (Address addr);
Address GetAddress () const;

// Protected/Private: PascalCase
void DoSendMessage (SignalingMessageId id);
void HandleTimeout (SignalingMessageId id);
```

### Logging

```cpp
NS_LOG_COMPONENT_DEFINE ("QuantumSignalingChannel");
NS_LOG_FUNCTION (this);
NS_LOG_INFO ("Message " << id << " DELIVERED");
NS_LOG_DEBUG ("Delay: " << delay.As (Time::MS));
```

### TypeId Registration

```cpp
NS_OBJECT_ENSURE_REGISTERED (QuantumSignalingChannel);

TypeId
QuantumSignalingChannel::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("ns3::QuantumSignalingChannel")
          .SetParent<Object> ()
          .SetGroupName ("Quantum")
          .AddConstructor<QuantumSignalingChannel> ()
          .AddAttribute ("PacketLossProbability", "...", ...);
  return tid;
}
```

## Anti-Patterns

### NEVER

- **Never** call `node->AddApplication()` on `QuantumNetworkLayer` — it's not an Application
- **Never** suppress type errors with `as any`, `@ts-ignore` equivalents
- **Never** directly access ExaTN without going through `QuantumBasis`
- **Never** mix quantum and classical timing without explicit conversion

### ALWAYS

- **Always** use `Ptr<>` for ns-3 objects (automatic memory management)
- **Always** call `NS_LOG_FUNCTION (this)` at function entry
- **Always** register TypeId with `NS_OBJECT_ENSURE_REGISTERED`
- **Always** use `Simulator::Schedule()` for time-based operations
- **Always** check for null pointers before dereferencing `Ptr<>`

## Key Constants

| Constant | Value | Usage |
|----------|-------|-------|
| `EPS` | 1e-6 | Double comparison threshold |
| `ETERNITY` | 1e5 | Large time value |
| `GATE_DURATION` | 2e-4 | Gate operation duration |
| `CLASSICAL_DELAY` | 0.1 | Classical communication delay (ms) |
| `TELEP_DELAY` | 0.5 | Teleportation protocol delay (s) |

## Extension Points

### Adding New Routing Protocol

1. Create `model/my-routing-protocol.h/cc`
2. Inherit from `QuantumRoutingProtocol`
3. Implement pure virtual methods:
   - `CalculateRoute()`
   - `UpdateTopology()`
   - `HasRoute()`, `GetRouteMetric()`
4. Add to `CMakeLists.txt`
5. Update network layer to use new protocol

### Adding New Application

1. Create `model/my-app.h/cc`
2. Inherit from `Application`
3. Implement `StartApplication()`, `StopApplication()`
4. Use `QuantumNetworkLayer` for entanglement requests
5. Add helper in `helper/` for easy instantiation

### Adding Signaling Message Type

1. Add to `SignalingMessageType` enum in `quantum-signaling-channel.h`
2. Implement handling logic in message processing
3. Update delay calculation if needed

## Testing

### Unit Test Pattern

```cpp
class QuantumBasisTestCase1 : public TestCase
{
public:
  QuantumBasisTestCase1 () : TestCase ("Description") {}
  virtual ~QuantumBasisTestCase1 () {}
private:
  virtual void DoRun (void) {
    NS_TEST_ASSERT_MSG_EQ (true, true, "msg");
    NS_TEST_ASSERT_MSG_EQ_TOL (0.01, 0.01, 0.001, "tol");
  }
};
```

### Example Test Command

```bash
su -s /bin/bash wst -c "cd /home/wst/Documents/ns-3.42/ns-3.42 && \
  ./ns3 run 'test-runner --suite=quantum-basis --testcase=QuantumBasisTestCase1'"
```

## Dependencies

### Internal (ns-3)
- `core-module`: Simulator, logging, objects
- `network-module`: Node, Application
- `point-to-point`: Channel modeling
- `internet`: Addressing

### External
- **ExaTN**: Tensor network backend at `~/.exatn`
  - `#include <exatn.hpp>`
  - All quantum state operations
  - Requires gcc-11.5.0

## Files Reference

### Recently Added

| File | Purpose | Lines |
|------|---------|-------|
| `quantum-signaling-channel.h/cc` | Classical signaling delay | ~700 |
| `q-cast-routing-protocol.h/cc` | Q-CAST routing algorithm | ~800 |
| `quantum-delay-model.h/cc` | Delay models (Burst, Dynamic) | ~300 |
| `quantum-topology-helper.h/cc` | Topology generation | ~600 |

### Core Files

| File | Purpose | Lines |
|------|---------|-------|
| `quantum-basis.h/cc` | ExaTN integration | ~1500 |
| `quantum-network-layer.h/cc` | L3 protocol | ~1200 |
| `quantum-phy-entity.h/cc` | Physical layer | ~1000 |
| `quantum-memory.h/cc` | Memory management | ~800 |

## Common Pitfalls

1. **ExaTN state not cleaned**: Call `Simulator::Destroy()` between runs
2. **Wrong gcc version**: Must use `/usr/local/gcc-11.5.0/bin/g++`
3. **Missing header includes**: Always include module header first
4. **Incorrect TypeId registration**: Use `NS_OBJECT_ENSURE_REGISTERED`
5. **Memory leaks**: Use `Ptr<>` not raw pointers
6. **Timing issues**: Use `Simulator::Now()` not `std::chrono`

## Related Documentation

- `../AGENTS.md`: Root module documentation
- `../doc/implementation.md`: Detailed design
- `../helper/`: Helper classes for easy setup
