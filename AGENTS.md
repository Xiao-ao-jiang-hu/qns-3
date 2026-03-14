# AGENTS.md - Quantum Module for ns-3

## Project Overview

**qns-3** - A quantum network simulator built as a contrib module for ns-3.42 using ExaTN (Exascale Tensor Network) as the backend.

**Location**: `ns-3.42/contrib/quantum/`  
**Language**: C++ (C++20)  
**Build**: CMake via ns3 wrapper

## Build Environment

### Compiler: gcc-11.5.0 at /usr/local

The project **must** be built with `gcc-11.5.0` located at `/usr/local/gcc-11.5.0/`.  
Do **not** use the system gcc (Debian 14.x at `/usr/bin/gcc`) — it produces binaries that are
incompatible with the ExaTN shared libraries.

### Run as non-root (wst user)

`./ns3` refuses to run as root. Use the `wst` user:
```bash
su -s /bin/bash wst -c "cd /home/wst/Documents/ns-3.42/ns-3.42 && ./ns3 run <example>"
```

### Running compiled binaries directly

Because the binaries embed an RPATH pointing to `/usr/local/gcc-11.5.0/lib64/libstdc++.so.6`
(which is an older version), you must override the library search path when running them directly:
```bash
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:./build/lib ./build/contrib/quantum/examples/...
```
Using `./ns3 run` as the `wst` user handles this automatically.

## Build Commands

```bash
# Configure (from ns-3.42 root) — run as wst
su -s /bin/bash wst -c "cd /home/wst/Documents/ns-3.42/ns-3.42 && ./ns3 configure --enable-examples --enable-tests"

# Build
su -s /bin/bash wst -c "cd /home/wst/Documents/ns-3.42/ns-3.42 && /usr/bin/cmake --build cmake-cache -j 15"
# or via wrapper:
su -s /bin/bash wst -c "cd /home/wst/Documents/ns-3.42/ns-3.42 && ./ns3 build"

# Clean
su -s /bin/bash wst -c "cd /home/wst/Documents/ns-3.42/ns-3.42 && ./ns3 clean"
```

**Prerequisites**: gcc-11, Python 3.6-3.10 (NOT 3.11), CMake 3.13+, OpenBLAS, OpenMPI, ExaTN at `~/.exatn`

## Test Commands

```bash
# Run all tests
./test.py

# Run specific test suite
./test.py -s quantum-basis

# Run single test case
./ns3 run "test-runner --suite=quantum-basis --testcase=QuantumBasisTestCase1"

# Verbose output
./test.py -v
```

## Run Examples

```bash
# Must run as wst user (not root)
su -s /bin/bash wst -c "cd /home/wst/Documents/ns-3.42/ns-3.42 && ./ns3 run telep-lin-example"
su -s /bin/bash wst -c "cd /home/wst/Documents/ns-3.42/ns-3.42 && ./ns3 run ent-swap-example"
su -s /bin/bash wst -c "cd /home/wst/Documents/ns-3.42/ns-3.42 && ./ns3 run distill-nested-example"

# With debug logging
NS_LOG="QuantumNetworkSimulator=info:QuantumPhyEntity=info|logic" \
  su -s /bin/bash wst -c "cd /home/wst/Documents/ns-3.42/ns-3.42 && ./ns3 run telep-lin-example"

# Q-CAST Examples (random topology + delay + fidelity recording)
su -s /bin/bash wst -c "cd /home/wst/Documents/ns-3.42/ns-3.42 && \
  NS_LOG='QCastRandomTopologyExample=info' \
  ./ns3 run 'q-cast-random-topology-example --numNodes=10 --numRequests=5 --topologyType=1'"

# Signaling Delay Example (multi-run simulation)
su -s /bin/bash wst -c "cd /home/wst/Documents/ns-3.42/ns-3.42 && \
  ./ns3 run signaling-delay-example -- --numRuns=10 --numNodes=5 --packetLossProb=0.05"
```

**Topology types**: `0`=RandomGeometric, `1`=ErdosRenyi, `2`=ScaleFree, `3`=GridRandom (dynamic only)

## Code Style

### Naming
- **Files**: `lowercase-hyphenated.h/cc`
- **Classes**: PascalCase (e.g., `QuantumNetworkSimulator`)
- **Private members**: `m_camelCase`
- **Public methods**: PascalCase (e.g., `GetAddress()`)
- **Macros**: UPPERCASE_WITH_UNDERSCORES
- **Constants**: `lowercase_with_underscores`
- **Reserved prefix**: `QNS_` for internal names

### Includes
```cpp
// 1. Module header
#include "ns3/quantum-basis.h"
// 2. ns-3 headers
#include "ns3/object.h"
// 3. Standard library
#include <string>
#include <vector>
// 4. Third-party
#include <exatn.hpp>
```

### Header Guards
```cpp
#ifndef FILENAME_H
#define FILENAME_H
// ... content ...
#endif /* FILENAME_H */
```

### Namespace
All code in `namespace ns3`, close with `} // namespace ns3`

### Formatting
- Column limit: 100 characters
- Indent: 4 spaces (C++)
- Pointer: Left (`int* ptr`)
- Qualifier: Left (`const int`)

**Format**: `clang-format -i model/file.cc`

### Linting
**Configure**: `./ns3 configure --enable-examples --enable-tests --enable-clang-tidy`

## Code Patterns

### Class Template
```cpp
class QuantumNode : public Node
{
private:
  Ptr<QuantumPhyEntity> m_qphyent;
  const std::string m_owner;

public:
  QuantumNode (Ptr<QuantumPhyEntity> phyent_, std::string owner_);
  ~QuantumNode ();
  static TypeId GetTypeId (void);
  void DoDispose (void) override;
  void SetAddress (Address addr);
  Address GetAddress () const;
};
```

### Logging
```cpp
NS_LOG_COMPONENT_DEFINE ("QuantumBasis");
NS_LOG_INFO (LIGHT_YELLOW_CODE << "Message" << END_CODE);
```

### Test Case
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

class QuantumBasisTestSuite : public TestSuite
{
public:
  QuantumBasisTestSuite () : TestSuite ("quantum-basis", UNIT) {
    AddTestCase (new QuantumBasisTestCase1, TestCase::QUICK);
  }
};
static QuantumBasisTestSuite squantumBasisTestSuite;
```

## Adding Code

### New Model
1. Create `model/my-class.h` and `model/my-class.cc`
2. Add to `CMakeLists.txt` SOURCE_FILES and HEADER_FILES

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

### New Test
Add to `test/quantum-test-suite.cc` following TestCase/TestSuite pattern

## Key Constants

- `EPS` (1e-6): Double comparison threshold
- `ETERNITY` (1e5): Large time value
- `GATE_DURATION` (2e-4): Gate error duration
- `CLASSICAL_DELAY` (0.1): ms
- `TELEP_DELAY` (0.5): seconds

## Architecture

```
Quantum Applications
    ↓
Quantum Transport Layer
    ↓
Quantum Network Layer (quantum-network-layer.h)
    - Orchestrates end-to-end path establishment
    - Delegates routing to pluggable protocols
    ↓ (via QuantumRoutingProtocol interface)
Quantum Routing Protocol (quantum-routing-protocol.h)
    - Abstract interface for routing algorithms
    - DijkstraRoutingProtocol: Default implementation
    ↓
ILinkLayerService (quantum-link-layer-service.h)
    - Neighbor-to-neighbor entanglement
    ↓
Quantum Physical Layer (PhyEntity, Memory, Channel)
    ↓
ExaTN Tensor Network Backend
```

**Important**: The network layer (`QuantumNetworkLayer`) does NOT implement routing algorithms directly. Instead, it delegates all routing decisions to a pluggable `QuantumRoutingProtocol` interface. This separation allows different routing strategies to be used interchangeably without modifying the network layer.

See `doc/implementation.md` for detailed implementation documentation including:
- Complete class interfaces and APIs
- Design philosophy and principles
- Workflow details
- Extension guidelines

## Documentation

- **This file (AGENTS.md)**: Build instructions, code style, quick reference
- **doc/implementation.md**: Detailed implementation documentation, class specifications
- **doc/quantum-network-layer.md**: Network layer design documentation

## Common Issues

- **ExaTN not found**: Check CMakeLists.txt line 13 path
- **Python version**: Use < 3.11 for ExaTN build
- **Link errors**: Missing entry in CMakeLists.txt
- **Test failures**: Check test case names match suite
- **Cannot run as root**: Use `su -s /bin/bash wst -c "..."` or `su - wst`
- **libstdc++ version mismatch** (`GLIBCXX_3.4.31/32 not found`): Binary RPATH points to
  `/usr/local/gcc-11.5.0/lib64/libstdc++.so.6` (old). Fix: either use `./ns3 run` as wst
  (which sets env correctly), or prepend `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu`.
- **`QuantumNetworkLayer` is not an `Application`**: Never call
  `node->AddApplication(netLayer)` — it will fail to compile. `QuantumNetworkLayer` is
  initialized via `netLayer->Initialize()` directly, not through the Application scheduler.
- **`CommandLine` not declared**: Add `#include "ns3/core-module.h"` (not just csma/internet
  modules) to examples that use `CommandLine cmd`.
- **Incomplete type `DistributeEPRSrcProtocol`**: Add
  `#include "ns3/distribute-epr-protocol.h"` to files that call methods on this type.
- **Q-CAST only finds 1-hop paths**: By default `QCastRoutingProtocol::CalculateRoutesGEDA`
  and `ExtendedDijkstra` only know about the local node's direct neighbors (from
  `m_networkLayer->GetNeighbors()`). To enable multi-hop discovery, call
  `routingProtocol->UpdateTopology(fullTopology)` after construction, passing a
  `map<string, map<string, LinkMetrics>>` that covers **all** edges in the network.
  The fixed `q-cast-random-topology-example.cc` does this in "Step 5b": it iterates the
  topology helper's edge list and calls `routingProtocols[0]->UpdateTopology(...)`.
  The routing algorithm then expands via `m_linkProbabilities` (global) instead of the
  local neighbor map, producing correct multi-hop paths.
