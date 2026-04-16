# AGENTS.md - qns-3 Agent Guide

This file is for AI agent development guidance only.

Keep design notes, architecture writeups, and implementation explanations in Markdown docs such as `doc/*.md`, not here. When this file and prose docs disagree, treat the code and `CMakeLists.txt` as the source of truth.

## Hard Rules

### 1. Fidelity and state evolution must use ExaTN density matrices

Do not introduce analytical fidelity or manual decoherence formulas in simulator logic.

Correct pattern:

```cpp
double fidel = 0.0;
qphyent->CalculateFidelity ({qubitA, qubitB}, fidel);
```

Forbidden pattern:

```cpp
double fidel = channelFidelity * std::exp (-time / tau);
```

Relevant code paths:

- `model/quantum-network-simulator.cc`
- `model/quantum-phy-entity.cc`
- `model/quantum-memory.cc`
- `model/quantum-error-model.cc`

Important detail: time-based decoherence is already applied through `QuantumMemory::EnsureDecoherence()` before gates and measurements. Do not duplicate that logic with hand-written `exp(-t/tau)` decay.

Note: some analysis/comparison examples under `examples/` contain simplified math for experiments. Do not copy those formulas into simulator physics code.

### 2. Use ns-3 simulation time for simulated behavior

Use `Simulator::Now()`, `Simulator::Schedule()`, and `ns3::Time` for protocol timing and decoherence-sensitive behavior. Do not model simulated timing with wall-clock time or ad hoc counters.

### 3. Keep this file agent-facing

If you are tempted to add architecture tutorials, protocol walkthroughs, or long implementation narratives here, put them in a Markdown doc instead.

## Build And Run

Work from the ns-3 root:

```bash
cd /home/wst/Documents/ns-3.42/ns-3.42
```

Compiler requirements:

- Use `gcc-11.5.0` from `/usr/local/gcc-11.5.0/`
- Do not rely on the system GCC for this module

Build commands:

```bash
./ns3 configure --enable-examples --enable-tests
./ns3 build
./ns3 clean
```

Run as the non-root `wst` user. Prefer `./ns3 run ...` over direct binary execution.

Common commands:

```bash
./ns3 run telep-lin-example
./ns3 run ent-swap-example
./ns3 run distill-nested-example
./ns3 run 'q-cast-random-topology-example --numNodes=10 --numRequests=5 --topologyType=1'
./test.py -s quantum-basis
```

If you must run a built binary directly, use:

```bash
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:./build/lib ./build/contrib/quantum/examples/telep-lin-example
```

## Code Map

The build-linked library sources are the ones listed in `CMakeLists.txt`.

Core physics and state handling:

- `model/quantum-basis.*`
- `model/quantum-network-simulator.*`
- `model/quantum-operation.*`
- `model/quantum-error-model.*`
- `model/quantum-phy-entity.*`
- `model/quantum-memory.*`
- `model/quantum-node.*`
- `model/quantum-channel.*`

Network, routing, and signaling:

- `model/quantum-link-layer-service.*`
- `model/quantum-network-layer.*`
- `model/quantum-routing-protocol.*`
- `model/dijkstra-routing-protocol.*`
- `model/q-cast-routing-protocol.*`
- `model/quantum-delay-model.*`
- `model/quantum-signaling-channel.*`

Protocol applications:

- `model/distribute-epr-protocol.*`
- `model/telep-*.cc` / `model/telep-*.h`
- `model/ent-swap-*.cc` / `model/ent-swap-*.h`
- `model/distill-*.cc` / `model/distill-*.h`

Helpers and runnable examples:

- `helper/*.cc`, `helper/*.h`
- `examples/*.cc`

Tests:

- `test/quantum-test-suite.cc`

Unlinked or legacy code exists in the tree. As of the current `CMakeLists.txt`, these are not part of the built `quantum` library:

- `model/quantum-dijkstra-routing.*`
- `model/quantum-header.*`
- `model/quantum-l3-protocol.*`

## Working Rules For Agents

### Adding or changing code

- Update `CMakeLists.txt` whenever you add a new build-linked source or header.
- Follow the existing ns-3 style in this module: `namespace ns3`, PascalCase types and methods, `m_memberName` fields, lowercase-hyphenated filenames.
- Use `Ptr<>`, `TypeId`, and `NS_OBJECT_ENSURE_REGISTERED` for ns-3 object types.

### Timing and decoherence

- If you need fidelity at an arbitrary time without an intervening gate or measurement, apply pending decoherence first with `EnsureAllDecoherence()` or the relevant node-level `EnsureDecoherence()` call before measuring fidelity.

### Network-layer caveats that matter when editing

- `QuantumNetworkLayer` is an `Object`, not an `Application`.
- `LinkMetrics::latency` exists, but `DijkstraRoutingProtocol::CalculateLinkCost()` currently uses only fidelity and success rate.
- `QuantumSignalingChannel` and `BurstDelayModel` are implemented, but `QuantumNetworkLayer` path setup currently uses direct link-layer requests, a fixed `EntanglementTimeout`, and placeholder sequential swap scheduling.
- The test suite is minimal; examples are the main validation surface for this module.

### Examples worth using for validation

- `telep-lin-example`
- `ent-swap-example`
- `distill-nested-example`
- `q-cast-example`
- `q-cast-random-topology-example`
- `signaling-delay-example`
- `routing-mismatch-example`

