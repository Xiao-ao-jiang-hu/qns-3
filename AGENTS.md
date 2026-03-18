# AGENTS.md - Quantum Network Simulator (qns-3)

---

## ⛔ ABSOLUTE REQUIREMENT: Fidelity via Density Matrix ONLY

**THIS RULE IS NON-NEGOTIABLE AND APPLIES TO ALL CODE IN THIS REPOSITORY.**

All fidelity measurements and quantum state evolution calculations **MUST** use ExaTN density matrix operations via `QuantumPhyEntity`. It is **ABSOLUTELY FORBIDDEN** to compute fidelity or qubit state decay using analytical closed-form formulas.

### ✅ Correct — use `CalculateFidelity` via ExaTN tensor contractions:
```cpp
double fidel;
qphyent->CalculateFidelity({qubit_a, qubit_b}, fidel);   // density matrix ⟨bell|ρ|bell⟩
```

### ❌ FORBIDDEN — analytical approximations:
```cpp
double fidel = channelFidelity * std::exp(-time / tau);   // FORBIDDEN
double fidel = F_A * F_C * std::exp(-T_a / tau_m);        // FORBIDDEN
```

Even for "sanity checks" or "comparison" purposes, analytical fidelity formulas must NOT appear in simulation code. Use ExaTN density matrices exclusively.

The time-dependent decoherence (`TimeModel`) is applied **automatically** by the simulator via `EnsureDecoherence()` before every gate or measurement. Manually computing `exp(-t/τ)` duplicates this mechanism and is wrong.

---

## Project Overview

**qns-3** - A quantum network simulator built as a contrib module for ns-3.42 using ExaTN (Exascale Tensor Network) as the quantum state backend.

**Location**: `ns-3.42/contrib/quantum/`  
**Language**: C++ (C++20)  
**Build System**: CMake via ns3 wrapper  
**Quantum Backend**: ExaTN tensor network library for density matrix operations

---

## Build Environment

### Compiler: gcc-11.5.0 (REQUIRED)

The project **must** be built with `gcc-11.5.0` at `/usr/local/gcc-11.5.0/`.  
Do **not** use the system gcc (Debian 14.x at `/usr/bin/gcc`) — it produces binaries incompatible with ExaTN shared libraries.

### Run as Non-Root User (wst)

`./ns3` refuses to run as root. The current environment is already the `wst` user — run commands directly:

```bash
cd /home/wst/Documents/ns-3.42/ns-3.42
./ns3 run telep-lin-example
```

No `su` or `sudo` is needed.

### Running Compiled Binaries Directly

Binaries embed an RPATH to `/usr/local/gcc-11.5.0/lib64/libstdc++.so.6` (older version).  
Override the library search path when running directly:

```bash
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:./build/lib ./build/contrib/quantum/examples/telep-lin-example
```

Using `./ns3 run` as the `wst` user handles this automatically.

---

## Build Commands

```bash
# Configure (from ns-3.42 root)
cd /home/wst/Documents/ns-3.42/ns-3.42
./ns3 configure --enable-examples --enable-tests

# Build
./ns3 build

# Clean
./ns3 clean
```

**Prerequisites**:
- gcc-11.5.0 at `/usr/local/gcc-11.5.0/`
- Python 3.6-3.10 (NOT 3.11+)
- CMake 3.13+
- OpenBLAS
- OpenMPI
- ExaTN installed at `~/.exatn`

---

## Test Commands

```bash
# Run all quantum tests
./test.py -s quantum-basis

# Run specific test case
./ns3 run "test-runner --suite=quantum-basis --testcase=QuantumBasisTestCase1"

# Verbose output
./test.py -v
```

---

## Run Examples

### Basic Protocols

```bash
cd /home/wst/Documents/ns-3.42/ns-3.42

# Teleportation
./ns3 run telep-lin-example

# Entanglement swapping
./ns3 run ent-swap-example

# Entanglement distillation
./ns3 run distill-nested-example

# With debug logging
NS_LOG="QuantumNetworkSimulator=info:QuantumPhyEntity=info|logic" ./ns3 run telep-lin-example
```

### Q-CAST Routing Examples

```bash
cd /home/wst/Documents/ns-3.42/ns-3.42

# Random topology with delay and fidelity recording
NS_LOG='QCastRandomTopologyExample=info' \
  ./ns3 run 'q-cast-random-topology-example --numNodes=10 --numRequests=5 --topologyType=1'

# Topology types: 0=RandomGeometric, 1=ErdosRenyi, 2=ScaleFree, 3=GridRandom
```

### Signaling Delay Example

```bash
# Multi-run simulation with packet loss and timeout
cd /home/wst/Documents/ns-3.42/ns-3.42
./ns3 run signaling-delay-example -- --numRuns=10 --numNodes=5 --packetLossProb=0.05
```

---

## Architecture

### Layered Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Quantum Applications                              │
│  (telep-app, ent-swap-app, distill-app, distribute-epr-protocol)   │
├─────────────────────────────────────────────────────────────────────┤
│                Quantum Network Layer (L3)                            │
│  quantum-network-layer.h/cc                                          │
│  - End-to-end path establishment                                     │
│  - Entanglement swapping coordination                                │
│  - Delegates routing to: QuantumRoutingProtocol (interface)          │
├─────────────────────────────────────────────────────────────────────┤
│                     Routing Layer                                     │
│  ├── DijkstraRoutingProtocol (shortest path by fidelity)            │
│  └── QCastRoutingProtocol (Q-CAST with recovery paths)              │
├─────────────────────────────────────────────────────────────────────┤
│                Link Layer Service (L2)                               │
│  quantum-link-layer-service.h/cc                                     │
│  - Neighbor-to-neighbor entanglement (ILinkLayerService interface)  │
├─────────────────────────────────────────────────────────────────────┤
│                Quantum Physical Layer (L1)                           │
│  ├── QuantumPhyEntity: Physical layer manager                        │
│  ├── QuantumMemory: Qubit storage with decoherence tracking         │
│  ├── QuantumChannel: Quantum link abstraction                       │
│  └── QuantumErrorModel: Dephase, Depolar, Time models               │
├─────────────────────────────────────────────────────────────────────┤
│                     ExaTN Backend                                    │
│  quantum-network-simulator.h/cc                                      │
│  - Tensor network representation of density matrices                 │
│  - Gate operations via tensor contractions                           │
│  - Fidelity calculation: F = ⟨bell|ρ|bell⟩                          │
└─────────────────────────────────────────────────────────────────────┘
```

### Supporting Infrastructure

```
┌─────────────────────────────────────────────────────────────────────┐
│              Classical Signaling Channel                             │
│  quantum-signaling-channel.h/cc                                      │
│  - Classical control message delay simulation                        │
│  - Packet loss modeling                                               │
│  - Logarithmic swap scheduling (O(log h) rounds)                     │
├─────────────────────────────────────────────────────────────────────┤
│                     Delay Models                                      │
│  quantum-delay-model.h/cc                                            │
│  - BurstDelayModel: Network congestion with burst periods            │
│  - Configurable base delay, deviation, burst probability             │
├─────────────────────────────────────────────────────────────────────┤
│                      Helpers                                          │
│  quantum-topology-helper.h/cc: Topology generation                   │
│  quantum-net-stack-helper.h/cc: Network stack installation          │
│  Protocol-specific helpers: telep-helper, ent-swap-helper, etc.     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Implementation Verification

### ✅ Discrete Time Axis Integration with Classical Networks

The simulator correctly implements a **shared discrete time axis** with ns-3's classical network simulation:

**Implementation Details**:

1. **Time Source**: All quantum operations use `Simulator::Now()` from ns-3's discrete event scheduler
   ```cpp
   // quantum-phy-entity.cc
   Time moment = Simulator::Now();  // Current simulation time
   m_qubit2time[qubit] = moment;    // Track qubit operation time
   ```

2. **Event Scheduling**: Quantum operations are scheduled via ns-3's event system
   ```cpp
   // Scheduling gate operations
   Simulator::Schedule(Seconds(CLASSICAL_DELAY), &QuantumPhyEntity::ApplyGate, ...);
   Simulator::ScheduleNow(&QuantumPhyEntity::ApplyGate, ...);
   ```

3. **Time Type**: Uses ns-3's `Time` class throughout (not `std::chrono`)
   ```cpp
   std::map<std::string, Time> m_qubit2time;       // Last operation time per qubit
   std::map<std::string, Time> m_qubit2lastTime;   // Last decoherence time
   ```

4. **Unified Timeline**: Quantum and classical events interleave on the same scheduler
   ```cpp
   // Both quantum gates and classical messages use the same timeline
   Simulator::Schedule(gateDelay, &ApplyGate, ...);          // Quantum
   Simulator::Schedule(classicalDelay, &Socket::Send, ...);  // Classical
   ```

**Verification**: ✅ Correctly implemented. All time-dependent quantum phenomena (decoherence, gate operations, fidelity decay) use ns-3's `Simulator::Now()` and `Simulator::Schedule()`.

---

### ✅ Fidelity Calculation from Density Matrices

The simulator **correctly computes fidelity from density matrices** using ExaTN tensor network operations, NOT analytical formulas.

**Implementation** (`quantum-network-simulator.cc:831-973`):

```cpp
double QuantumNetworkSimulator::CalculateFidelity(
    const std::pair<std::string, std::string> &epr, double &fidel)
{
  // 1. Prepare ideal Bell state vector |bell⟩
  PrepareTensor(QNS_PREFIX + "BellSV", {2, 2}, q_bell);
  
  // 2. Extract EPR pair density matrix ρ from tensor network
  //    via partial trace over all other qubits
  exatn::TensorNetwork circuit_peek = m_dm;
  // ... partial trace operations ...
  Evaluate(&circuit_peek);
  
  // 3. Access density matrix data from ExaTN tensor
  auto talsh_tensor = exatn::getLocalTensor(circuit_peek.getTensor(0)->getName());
  const std::complex<double> *body_ptr;
  talsh_tensor->getDataAccessHostConst(&body_ptr);
  // body_ptr now points to the 4×4 density matrix
  
  // 4. Calculate F = ⟨bell|ρ|bell⟩
  //    Build tensor network: |bell⟩⟨bell| ⊗ ρ and contract
  exatn::TensorNetwork circuit_fidel;
  circuit_fidel.appendTensor(bell_ket);   // |bell⟩
  circuit_fidel.appendTensor(bell_bra);   // ⟨bell|
  circuit_fidel.appendTensor(rho);        // ρ
  Evaluate(&circuit_fidel);
  
  // 5. Extract fidelity scalar from result
  talsh_tensor = exatn::getLocalTensor(circuit_fidel.getTensor(0)->getName());
  talsh_tensor->getDataAccessHostConst(&body_ptr);
  fidel = (*body_ptr).real();  // F = Tr(|bell⟩⟨bell| ρ) = ⟨bell|ρ|bell⟩
  
  return fidel;
}
```

**Key Points**:

1. **Full Density Matrix**: The tensor network `m_dm` maintains the complete quantum state as a density matrix
2. **Partial Trace**: Before fidelity calculation, other qubits are traced out via `PartialTrace()`
3. **Tensor Contraction**: Fidelity is computed by contracting the tensor network ⟨bell|ρ|bell⟩
4. **No Analytical Shortcuts**: The implementation uses actual matrix operations, not closed-form approximations

**Density Matrix Representation**:

```cpp
class QuantumNetworkSimulator {
  exatn::numerics::TensorNetwork m_dm;  // Tensor network for density matrix
  unsigned m_dm_id;                      // Next tensor ID
  
  // Maps track qubit legs in the tensor network
  std::map<std::string, std::pair<unsigned, unsigned>> m_qubit2tensor;     // "ket" half
  std::map<std::string, std::pair<unsigned, unsigned>> m_qubit2tensor_dag; // "bra" half
};
```

**Verification**: ✅ Correctly implemented. Fidelity is calculated from actual density matrices via ExaTN tensor contractions, following the quantum mechanical definition F = ⟨ψ|ρ|ψ⟩.

---

### ✅ Automatic Decoherence Tracking

Quantum memory correctly tracks and applies decoherence based on elapsed simulation time.

**Implementation** (`quantum-memory.cc`):

```cpp
void QuantumMemory::ApplyDecoherence(const std::string& qubit) {
    Time now = Simulator::Now();
    Time lastTime = m_qubit2lastTime[qubit];
    
    if (now > lastTime) {
        // Get error model for this qubit
        Ptr<QuantumErrorModel> model = m_qphyent->GetErrorModel(qubit);
        if (model) {
            // Set the reference time for error model calculation
            m_qphyent->SetQubitTime(qubit, lastTime);
            // Apply error for duration (now - lastTime)
            model->ApplyErrorModel(m_qphyent, {qubit}, now);
        }
    }
    
    m_qubit2lastTime[qubit] = now;  // Update timestamp
}
```

**Time-Based Error Model** (`quantum-error-model.cc`):

```cpp
void TimeModel::ApplyErrorModel(Ptr<QuantumPhyEntity> qphyent, 
                                const std::vector<std::string> &qubits,
                                const Time &moment) const {
    for (const std::string &qubit : qubits) {
        // Calculate elapsed time since last operation
        Time duration = moment - qphyent->m_qubit2time[qubit];
        
        // Exponential decay: prob = (1 - e^(-t/τ)) / 2
        double prob_time = (1 - exp(-duration.GetSeconds() / m_rate)) / 2;
        
        // Apply as mixed quantum operation: (1-p)|ψ⟩⟨ψ| + p Z|ψ⟩⟨ψ|Z
        QuantumOperation time = {
            {"I", "PZ"}, 
            {pauli_I, pauli_Z}, 
            {1 - prob_time, prob_time}
        };
        qphyent->ApplyOperation(time, qubits);
    }
}
```

**Decoherence Trigger Points**:

Decoherence is applied **automatically** before quantum operations:

```cpp
// In QuantumPhyEntity::ApplyGate()
for (const std::string &qubit : qubits) {
    pnode->EnsureDecoherence(qubit);  // Apply pending decoherence
    ApplyErrorModel({qubit}, moment);
}

// In QuantumPhyEntity::Measure()
for (const std::string &qubit : qubits) {
    pnode->EnsureDecoherence(qubit);  // Apply before measurement
}
```

**Verification**: ✅ Correctly implemented. Decoherence is automatically tracked via timestamps and applied based on actual elapsed simulation time using the exponential decay formula.

---

### ⚠️ Classical Signaling Delays: Partially Implemented

**Status**: Modeled but not fully integrated into main path establishment flow.

#### What IS Implemented:

1. **QuantumSignalingChannel Class** (`quantum-signaling-channel.h/cc`):
   ```cpp
   class QuantumSignalingChannel {
       // Message types
       enum SignalingMessageType {
           ENTANGLEMENT_REQUEST, ENTANGLEMENT_RESPONSE,
           SWAP_REQUEST, SWAP_RESPONSE,
           MEASUREMENT_RESULT, CORRECTION_COMMAND
       };
       
       // Delay simulation
       Time GetLinkDelay(const string &srcNode, const string &dstNode);
       Time CalculateTotalSignalingDelay(const vector<string> &pathNodes);
       
       // Packet loss
       double m_packetLossProbability;  // Default: 5%
       
       // Logarithmic swap scheduling (O(log h) rounds)
       vector<SwapScheduleEntry> GenerateLogarithmicSwapSchedule(...);
   };
   ```

2. **Delay Models** (`quantum-delay-model.h/cc`):
   ```cpp
   class BurstDelayModel : public QuantumDelayModel {
       Time m_baseDelay;          // Default: 10ms
       Time m_maxDeviation;       // Random variation
       double m_burstProbability; // 5% chance of burst
       Time m_burstDuration;      // Burst period length
       double m_burstMultiplier;  // 2.0x delay during burst
   };
   ```

3. **Link Metrics Include Latency**:
   ```cpp
   struct LinkMetrics {
       double fidelity;
       double successRate;
       double latency;      // ⚠️ Defined but NOT used in routing!
       bool isAvailable;
   };
   ```

#### What is NOT Integrated:

1. **Routing Protocols Don't Use Latency**:
   ```cpp
   // DijkstraRoutingProtocol::CalculateLinkCost()
   double fidelityCost = 1.0 / (metrics.fidelity + 1e-10);
   double successCost = 1.0 / (metrics.successRate + 1e-10);
   return 0.5 * fidelityCost + 0.5 * successCost;
   // metrics.latency is IGNORED!
   ```

2. **NetworkLayer Uses Fixed Timeout**:
   ```cpp
   // QuantumNetworkLayer configuration
   Time m_entanglementTimeout = Seconds(1.0);  // Fixed, not delay-based
   ```

3. **SignalingChannel Not Used by NetworkLayer**:
   - `QuantumNetworkLayer::SetupPath()` does not call `QuantumSignalingChannel`
   - Entanglement requests don't go through signaling delay simulation

#### Impact on Fidelity:

Despite the integration gaps, **delays DO affect fidelity** because:

```cpp
// Any waiting time causes decoherence
Time waitDuration = Simulator::Now() - lastOperationTime;
// QuantumMemory tracks this and applies TimeModel automatically
```

The `Simulator::Now()` timeline is unified, so any delay in classical communication (even implicit) increases the time qubits spend in memory, triggering more decoherence via `EnsureDecoherence()`.

**Verification**: ⚠️ Partially implemented. Delay models exist but aren't integrated into routing decisions or path establishment. However, delays still affect fidelity through automatic decoherence tracking.

---

### ⚠️ Routing Does Not Consider Signaling Delays

**Issue**: `LinkMetrics::latency` is defined but ignored by both `DijkstraRoutingProtocol` and `QCastRoutingProtocol`.

**Current Implementation**:
```cpp
// dijkstra-routing-protocol.cc
double CalculateLinkCost(const LinkMetrics &metrics) const {
    // Only considers fidelity and success rate
    return (1.0 / metrics.fidelity) + (1.0 / metrics.successRate);
    // metrics.latency is NOT used
}

// q-cast-routing-protocol.cc
double CalculateExpectedThroughput(const QCastLabel &label, double nextHopProb) {
    // E_t = (product of link probs) * S(hop_count)
    // Only considers success probabilities, not link delays
}
```

**Impact**:
- Routes are optimized for fidelity/success, not end-to-end latency
- Longer paths with higher fidelity may have more decoherence due to delay
- Classical control message delays are not factored into routing decisions

**Recommendation**:
```cpp
// Suggested improvement
double CalculateLinkCost(const LinkMetrics &metrics) const {
    double fidelityCost = 1.0 / metrics.fidelity;
    double latencyCost = metrics.latency.GetSeconds() * latency_weight;
    return fidelityCost + latencyCost;
}
```

**Verification**: ⚠️ Gap identified. Routing protocols should incorporate link latency into cost calculations.

---

## Error Models

### Three Error Model Types

| Model | Application | Formula | Parameters |
|-------|-------------|---------|------------|
| **TimeModel** | Memory decoherence | `prob = (1 - e^(-t/τ)) / 2` | `rate` (τ): decoherence time constant |
| **DephaseModel** | Gate errors | `prob = (1 - e^(-d/τ)) / 2` | `rate` (τ), `d = GATE_DURATION = 2e-4s` |
| **DepolarModel** | Channel noise | Mixed state with fidelity F | `fidel`: channel fidelity (0.0-1.0) |

### TimeModel Implementation Details

```cpp
// Applied when qubits idle in memory
void TimeModel::ApplyErrorModel(...) {
    Time duration = moment - qphyent->m_qubit2time[qubit];
    double prob = (1 - exp(-duration.GetSeconds() / m_rate)) / 2;
    
    // Kraus operator representation
    // ρ → (1-p) ρ + p Z ρ Z
    QuantumOperation time = {
        {"I", "PZ"},              // Operators
        {pauli_I, pauli_Z},       // Matrices
        {1 - prob, prob}          // Probabilities
    };
}
```

### Default Error Models

```cpp
const DephaseModel default_dephase_model = DephaseModel(1.0);    // Rate = 1.0
const DepolarModel default_depolar_model = DepolarModel(0.95);   // Fidelity = 0.95
const TimeModel default_time_model = TimeModel(1.0);             // Rate = 1.0
```

---

## Key Constants

| Constant | Value | Usage |
|----------|-------|-------|
| `EPS` | 1e-6 | Double comparison threshold |
| `ETERNITY` | 1e5 s | Large time value for "infinite" |
| `GATE_DURATION` | 2e-4 s | Gate error modeling duration |
| `CLASSICAL_DELAY` | 0.1 ms | Default classical comm delay |
| `TELEP_DELAY` | 0.5 s | Teleportation protocol delay |
| `DIST_EPR_DELAY` | 0.005 s | EPR distribution delay |
| `SETUP_DELAY` | 0.1 s | Setup phase delay |

---

## Code Style

### Naming Conventions

| Type | Convention | Example |
|------|-----------|---------|
| Files | lowercase-hyphenated | `quantum-phy-entity.h` |
| Classes | PascalCase | `QuantumNetworkLayer` |
| Private members | `m_camelCase` | `m_qphyent`, `m_owner` |
| Public methods | PascalCase | `GetAddress()`, `SetOwner()` |
| Macros | UPPERCASE_WITH_UNDERSCORES | `NS_LOG_COMPONENT_DEFINE` |
| Constants | lowercase_with_underscores | `default_time_model` |

### Include Order

```cpp
// 1. Module header (quotes)
#include "ns3/quantum-basis.h"

// 2. ns-3 headers (quotes)
#include "ns3/object.h"
#include "ns3/simulator.h"

// 3. Standard library (angle brackets)
#include <string>
#include <vector>

// 4. Third-party (angle brackets)
#include <exatn.hpp>
```

### Header Guards

```cpp
#ifndef QUANTUM_PHY_ENTITY_H
#define QUANTUM_PHY_ENTITY_H
// ... content ...
#endif /* QUANTUM_PHY_ENTITY_H */
```

### Namespace

All code in `namespace ns3`, close with comment:

```cpp
namespace ns3 {

// ... code ...

} // namespace ns3
```

---

## Common Pitfalls

### 1. Wrong gcc Version

**Problem**: System gcc produces incompatible binaries.  
**Symptoms**: `GLIBCXX_3.4.31 not found` errors.  
**Solution**: Use gcc-11.5.0 from `/usr/local/gcc-11.5.0/`.

### 2. Running as Root

**Problem**: `./ns3` refuses to run as root.  
**Solution**: Already running as `wst` user. Run `./ns3 run` directly from the ns-3.42 root directory.

### 3. libstdc++ Version Mismatch

**Problem**: Binary RPATH points to old libstdc++.  
**Solution**: Use `./ns3 run` as wst, or set `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu`.

### 4. QuantumNetworkLayer is NOT an Application

**Problem**: Trying to add it via `node->AddApplication()`.  
**Solution**: Initialize directly: `netLayer->Initialize()`.

### 5. Missing Header Includes

**Problem**: Incomplete type errors.  
**Solution**: Add required headers (e.g., `#include "ns3/core-module.h"` for `CommandLine`).

### 6. ExaTN State Not Cleaned

**Problem**: State persists between simulation runs.  
**Solution**: Call `Simulator::Destroy()` between runs.

### 7. Decoherence Not Applied

**Problem**: Fidelity doesn't decay during idle periods.  
**Solution**: Call `EnsureAllDecoherence()` before measuring fidelity at arbitrary times.

### 8. Multi-hop Path Not Found

**Problem**: Q-CAST only finds 1-hop paths.  
**Solution**: Call `routingProtocol->UpdateTopology(fullTopology)` after construction to enable multi-hop discovery.

---

## Adding New Code

### New Model

1. Create `model/my-class.h` and `model/my-class.cc`
2. Add to `CMakeLists.txt`:
   ```cmake
   SOURCE_FILES
       model/my-class.cc
   HEADER_FILES
       model/my-class.h
   ```

### New Example

1. Create `examples/my-example.cc`
2. Add to `examples/CMakeLists.txt`:
   ```cmake
   build_lib_example(
       NAME my-example
       SOURCE_FILES my-example.cc
       LIBRARIES_TO_LINK ${libquantum}
   )
   ```

### New Routing Protocol

1. Inherit from `QuantumRoutingProtocol`
2. Implement:
   - `CalculateRoute(src, dst)` → returns path as `vector<string>`
   - `UpdateTopology(topology)` → updates routing tables
   - `AddNeighbor()`, `RemoveNeighbor()`, `UpdateLinkMetrics()`
   - `HasRoute()`, `GetRouteMetric()`
3. Set on network layer: `netLayer->SetRoutingProtocol(routing)`

### New Link Layer Implementation

1. Inherit from `ILinkLayerService`
2. Implement:
   - `RequestEntanglement()` → establish neighbor entanglement
   - `ConsumeEntanglement()` → use entanglement for swapping
   - `GetEntanglementInfo()`, `IsEntanglementReady()`

---

## File Structure

```
contrib/quantum/
├── model/
│   ├── quantum-basis.h/cc              # Constants, utilities, gate matrices
│   ├── quantum-network-simulator.h/cc  # ExaTN backend, density matrix ops
│   ├── quantum-operation.h/cc          # Quantum operation abstraction
│   ├── quantum-error-model.h/cc        # Error models (Time, Dephase, Depolar)
│   ├── quantum-phy-entity.h/cc         # Physical layer manager
│   ├── quantum-memory.h/cc             # Qubit storage with decoherence
│   ├── quantum-node.h/cc               # Quantum node abstraction
│   ├── quantum-channel.h/cc            # Quantum link abstraction
│   ├── quantum-link-layer-service.h/cc # Link layer interface
│   ├── quantum-network-layer.h/cc      # Network layer (L3)
│   ├── quantum-routing-protocol.h/cc   # Routing interface
│   ├── dijkstra-routing-protocol.h/cc  # Dijkstra implementation
│   ├── q-cast-routing-protocol.h/cc    # Q-CAST implementation
│   ├── quantum-signaling-channel.h/cc  # Classical signaling delay
│   ├── quantum-delay-model.h/cc        # Delay models (Burst, etc.)
│   ├── distribute-epr-protocol.h/cc    # EPR distribution
│   ├── telep-app.h/cc                  # Teleportation application
│   ├── ent-swap-app.h/cc               # Entanglement swapping
│   └── distill-app.h/cc                # Entanglement distillation
├── helper/
│   ├── quantum-basis-helper.h/cc
│   ├── quantum-net-stack-helper.h/cc
│   ├── quantum-topology-helper.h/cc
│   └── [protocol-specific helpers]
├── examples/
│   ├── telep-lin-example.cc
│   ├── ent-swap-example.cc
│   ├── distill-nested-example.cc
│   ├── q-cast-random-topology-example.cc
│   ├── signaling-delay-example.cc
│   └── [more examples]
├── test/
│   └── quantum-test-suite.cc
├── doc/
│   ├── implementation.md
│   └── quantum-network-layer.md
└── CMakeLists.txt
```

---

## Documentation

| File | Purpose |
|------|---------|
| **AGENTS.md** (this file) | Build instructions, implementation verification, code style |
| **doc/implementation.md** | Detailed class interfaces, design philosophy |
| **doc/quantum-network-layer.md** | Network layer design documentation |

---

## Related Research

This simulator implements concepts from quantum networking research:

- **Q-CAST Routing**: Greedy Extended Dijkstra Algorithm (G-EDA), recovery paths, logarithmic swap scheduling
- **Entanglement Swapping**: Multi-hop entanglement via Bell State Measurements
- **Entanglement Distillation**: Nested protocols for fidelity improvement
- **Quantum Teleportation**: State transfer using pre-shared entanglement

---

## Summary of Verification Results

| Feature | Status | Notes |
|---------|--------|-------|
| **Discrete time axis integration** | ✅ Correct | Uses `Simulator::Now()` throughout |
| **Fidelity from density matrices** | ✅ Correct | F = ⟨bell\|ρ\|bell⟩ via ExaTN |
| **Automatic decoherence tracking** | ✅ Correct | Time-based with exponential decay |
| **Density matrix representation** | ✅ Correct | Tensor network with ket/bra legs |
| **Classical signaling delays** | ⚠️ Partial | Models exist but not integrated |
| **Delay-aware routing** | ❌ Not implemented | `LinkMetrics.latency` ignored |
| **Delay impact on fidelity** | ✅ Implicit | Any waiting triggers decoherence |
| **Logarithmic swap scheduling** | ✅ Implemented | O(log h) rounds in Q-CAST |

---

## Future Enhancements (Recommendations)

1. **Integrate SignalingChannel into NetworkLayer**: Route entanglement requests through `QuantumSignalingChannel` to apply delay models.

2. **Add Latency to Routing Cost**:
   ```cpp
   double cost = w1 * (1/fidelity) + w2 * (1/successRate) + w3 * latency;
   ```

3. **Continuous Decoherence Updates**: Add periodic `EnsureAllDecoherence()` calls during long operations.

4. **Dynamic Fidelity Monitoring**: Expose `CalculateFidelity()` for runtime monitoring.

5. **Signaling Delay Statistics**: Track and report cumulative signaling delays per path.
