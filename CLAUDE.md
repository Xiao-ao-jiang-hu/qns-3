# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Qns-3** is a quantum network simulator built as an ns-3 contrib module, developed by Tsinghua University Research Center for Quantum Software. It uses **ExaTN** (Exascale Tensor Network) as the quantum backend for density matrix simulation.

## Build & Run

### Prerequisites
- **gcc-11.5.0** (not the system default gcc — must use `gcc-11`/`g++-11`)
- Python 3.9 (managed via `.venv` in ns-3.42 root)
- CMake, OpenBLAS, OpenMPI
- ExaTN built and installed at `~/.exatn`

### Python Environment
The project uses a Python 3.9 virtual environment located at `.venv/` in the ns-3.42 root directory. Activate it before building or running:

```bash
# From ns-3.42 root
source .venv/bin/activate
```

### Building
```bash
# From ns-3.42 root (not contrib/quantum/), with .venv activated
./ns3 configure --enable-examples --enable-tests
./ns3 build quantum
```

### Running Examples
```bash
./ns3 run telep-lin-example
./ns3 run ent-swap-example
./ns3 run distill-nested-example
./ns3 run quantum-network-layer-example
```

With logging:
```bash
NS_LOG="QuantumNetworkSimulator=info:QuantumPhyEntity=info|logic" ./ns3 run telep-lin-example
```

### Running Tests
```bash
./test.py -s quantum-basis
./ns3 run "test-runner --suite=quantum-basis --testcase=QuantumBasisTestCase1"
```

### Adding a New Example
In `examples/CMakeLists.txt`, add:
```cmake
build_lib_example(
    NAME my-new-example
    SOURCE_FILES my-new-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)
```

## Architecture

The simulator implements a 5-layer quantum network stack:

```
┌──────────────────────────────────┐
│  Quantum Applications             │
│  (Teleportation, Distillation,   │
│   Entanglement Swapping)          │
├──────────────────────────────────┤
│  Quantum Network Layer (L3)       │
│  Path setup via routing protocol  │
├──────────────────────────────────┤
│  Link Layer Service (L2)          │
│  Neighbor-to-neighbor entanglement│
├──────────────────────────────────┤
│  Quantum Physical Layer (L1)      │
│  Gates, measurements, memory      │
├──────────────────────────────────┤
│  ExaTN Tensor Network Backend     │
│  Density matrix representation    │
└──────────────────────────────────┘
```

### Key Classes

| Class | File | Role |
|---|---|---|
| `QuantumNetworkSimulator` | `model/quantum-network-simulator.h` | ExaTN backend; creates/operates on density matrices |
| `QuantumPhyEntity` | `model/quantum-phy-entity.h` | Physical operations: gates, measurement, fidelity, decoherence |
| `QuantumMemory` | `model/quantum-memory.h` | Qubit storage with timestamp-based decoherence |
| `QuantumNetworkLayer` | `model/quantum-network-layer.h` | L3 path establishment and management |
| `QuantumLinkLayerService` | `model/quantum-link-layer-service.h` | L2 neighbor-to-neighbor entanglement |
| `QuantumRoutingProtocol` | `model/quantum-routing-protocol.h` | Abstract routing interface |
| `DijkstraRoutingProtocol` | `model/dijkstra-routing-protocol.h` | Shortest-path routing |
| `QCastRoutingProtocol` | `model/q-cast-routing-protocol.h` | Multi-path routing with recovery |

### Error Models (`model/quantum-error-model.h`)
- **TimeModel** — decoherence over time (applied automatically via memory timestamps)
- **DephaseModel** — gate errors (phase flip noise)
- **DepolarModel** — channel noise (depolarizing channel with default fidelity 0.95)

### Application Protocols
- **Teleportation**: `telep-app.h` (basic), `telep-adapt-app.h`, `telep-lin-adapt-app.h`
- **Entanglement Swapping**: `ent-swap-app.h`, `ent-swap-adapt-app.h`, `ent-swap-adapt-local-app.h`
- **Distillation**: `distill-app.h`, `distill-nested-app.h`, `distill-nested-adapt-app.h`
- **EPR Distribution**: `distribute-epr-protocol.h`

### Classical Signaling
Classical signaling uses IPv6 (`quantum-signaling-channel.h`). The `QuantumDelayModel` (with `BurstDelayModel`) handles probabilistic packet loss in signaling.

## Critical Rules

1. **Fidelity must always be computed via `CalculateFidelity()` using ExaTN density matrices.** Never use analytical formulas for fidelity.

2. **Use gcc-11**, not the system default compiler. Build failures often trace to the wrong compiler.

3. **All quantum state is stored as density matrices** in ExaTN. The `QuantumNetworkSimulator` singleton manages the global tensor network state.

4. **Decoherence is time-aware**: `QuantumMemory` stores qubit creation timestamps; `QuantumPhyEntity` applies the `TimeModel` based on elapsed simulation time before any operation.

5. **Path lifecycle**: `QuantumNetworkLayer` paths go through `PENDING → READY → CONSUMED/FAILED`. Applications must wait for `READY` before consuming entanglement.

## Code Conventions

- **Files**: lowercase-hyphenated (`quantum-phy-entity.h`)
- **Classes**: PascalCase (`QuantumNetworkLayer`)
- **Private members**: `m_camelCase` (`m_qphyent`)
- **Methods**: PascalCase (`GetAddress()`)
- **Header guards**: `QUANTUM_CLASS_NAME_H`
- **Namespace**: all code in `namespace ns3`
- **Include order**: module header → ns-3 headers → standard library → third-party

## Key Constants (`model/quantum-basis.h`)

| Constant | Value | Meaning |
|---|---|---|
| `EPS` | 1e-6 | Double comparison threshold |
| `ETERNITY` | 1e5 s | Conceptual "infinite" time |
| `GATE_DURATION` | 2e-4 s | Duration used in gate error modeling |
| `CLASSICAL_DELAY` | 0.1 ms | Default classical comm delay |
| `TELEP_DELAY` | 0.5 s | Teleportation protocol delay |
| `DIST_EPR_DELAY` | 0.005 s | EPR distribution delay |
