# AGENTS.md - Quantum Module for ns-3

## Project Overview

**qns-3** - A quantum network simulator built as a contrib module for ns-3.42 using ExaTN (Exascale Tensor Network) as the backend.

**Location**: `ns-3.42/contrib/quantum/`  
**Language**: C++ (C++20)  
**Build**: CMake via ns3 wrapper

## Build Commands

```bash
# Configure (from ns-3.42 root)
./ns3 configure --enable-examples --enable-tests

# Build
./ns3 build

# Clean
./ns3 clean
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
./ns3 run telep-lin-example
./ns3 run ent-swap-example
./ns3 run distill-nested-example

# With debug logging
NS_LOG="QuantumNetworkSimulator=info:QuantumPhyEntity=info|logic" ./ns3 run telep-lin-example
```

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
