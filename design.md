# 量子网络栈设计文档

## 1. 概述

本项目在 ns-3.42 网络模拟器中实现了一个量子网络模拟模块，支持量子态演化、退相干建模、纠缠分发和纠缠交换等核心量子网络功能。

### 1.1 设计目标

1. **物理真实性**: 使用密度矩阵和Kraus算子进行真实的量子态演化模拟
2. **可扩展性**: 模块化设计，便于添加新的量子协议和错误模型
3. **与ns-3集成**: 利用ns-3的事件调度、网络协议栈和统计框架
4. **性能**: 使用ExaTN张量网络后端进行高效的量子态计算

### 1.2 核心组件

```
┌─────────────────────────────────────────────────────────────┐
│                    应用层 (Applications)                      │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐   │
│  │EntSwapApp│ │ TelepApp │ │DistillApp│ │FourNodeChain │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                    协议层 (Protocols)                         │
│  ┌────────────────────┐ ┌─────────────────────────────┐    │
│  │DistributeEPRProtocol│ │   QuantumNetStackHelper    │    │
│  └────────────────────┘ └─────────────────────────────┘    │
├─────────────────────────────────────────────────────────────┤
│                    量子物理层 (Quantum Physical Layer)        │
│  ┌────────────────┐ ┌─────────────────┐ ┌──────────────┐   │
│  │QuantumPhyEntity│ │QuantumMemoryModel│ │QuantumChannel│   │
│  └────────────────┘ └─────────────────┘ └──────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                    张量网络后端 (Tensor Network Backend)      │
│  ┌─────────────────────────────────────────────────────┐   │
│  │            QuantumNetworkSimulator (ExaTN)           │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## 2. 量子物理层设计

### 2.1 QuantumPhyEntity - 量子物理实体

`QuantumPhyEntity` 是量子网络的核心类，管理所有量子态和操作。

**主要职责:**
- 量子比特生成和管理
- 量子门操作
- 测量和部分迹
- 错误模型应用
- 保真度计算

**关键接口:**
```cpp
// 生成纯态量子比特
bool GenerateQubitsPure(const std::string &owner,
                        const std::vector<std::complex<double>> &data,
                        const std::vector<std::string> &qubits);

// 应用量子门
bool ApplyGate(const std::string &owner,
               const std::string &gate,
               const std::vector<std::complex<double>> &data,
               const std::vector<std::string> &qubits);

// 应用量子操作（Kraus通道）
bool ApplyOperation(const QuantumOperation &quantumOperation,
                    const std::vector<std::string> &qubits);

// 测量
std::pair<unsigned, std::vector<double>>
Measure(const std::string &owner, const std::vector<std::string> &qubits);

// 部分迹
bool PartialTrace(const std::vector<std::string> &qubits);

// 计算Bell态保真度
double CalculateFidelity(const std::pair<std::string, std::string> &epr, double &fidel);
```

### 2.2 QuantumMemoryModel - 量子存储器退相干模型

实现基于T1/T2的物理退相干模型。

**物理模型:**
- **T1 (振幅阻尼)**: |1⟩ → |0⟩ 的能量弛豫，概率 p = 1 - exp(-t/T1)
- **T2 (纯退相)**: 相干性衰减，有效退相时间 1/T_φ = 1/T2 - 1/(2T1)

**Kraus算子表示:**

振幅阻尼:
```
K0 = |0⟩⟨0| + √(1-p)|1⟩⟨1| = [[1, 0], [0, √(1-p)]]
K1 = √p|0⟩⟨1|              = [[0, √p], [0, 0]]
```

纯退相:
```
K0 = √(1-p/2) I = [[√(1-p/2), 0], [0, √(1-p/2)]]
K1 = √(p/2) Z   = [[√(p/2), 0], [0, -√(p/2)]]
```

**关键特性:**
- 周期性退相干调度（可配置时间步长）
- 量子比特时间戳跟踪
- 支持单独或批量应用退相干

**已知问题与解决方案:**

在张量网络中对纠缠量子比特应用Kraus通道时存在非迹保持问题。解决方案是在每次退相干操作前对所有注册的量子比特应用恒等门：

```cpp
// 解决方案：在ApplyOperation前刷新张量网络状态
for (const auto &pair : m_qubitTimestamps)
{
    m_qphyent->ApplyGate("God", QNS_GATE_PREFIX + "I", pauli_I, {pair.first});
}
QuantumOperation dephas({"I", "PZ"}, {pauli_I, pauli_Z}, {1.0 - prob, prob});
m_qphyent->ApplyOperation(dephas, {qubit});
```

### 2.3 QuantumChannel - 量子信道

表示两个量子节点之间的量子链路。

**功能:**
- 源节点和目标节点管理
- 退极化错误模型（链路保真度）
- EPR对分发支持

### 2.4 错误模型层次

```
QuantumErrorModel (基类)
├── DephaseModel      - 退相模型（门操作相关）
├── DepolarModel      - 退极化模型（链路相关）
├── TimeModel         - 时间相关退相模型
└── QuantumMemoryModel - T1/T2存储器模型（新增）
```

## 3. 量子协议实现

### 3.1 EPR分发协议

**流程:**
1. 源节点生成Bell态 |Φ+⟩ = (|00⟩ + |11⟩)/√2
2. 应用链路退极化噪声
3. 通过经典信道通知目标节点
4. 目标节点接收量子比特所有权

### 3.2 纠缠交换协议

**Bell态测量 (BSM):**
```cpp
// CNOT门 + Hadamard门 + 测量
m_qphyent->ApplyGate("God", "QNS_GATE_CNOT", {}, {qubit2, qubit1});
m_qphyent->ApplyGate("God", "QNS_GATE_H", {}, {qubit1});
auto outcome0 = m_qphyent->Measure(owner, {qubit1});
auto outcome1 = m_qphyent->Measure(owner, {qubit2});
```

**Pauli校正:**
- 测量结果 (m0, m1) 决定校正操作
- X校正: 基于 m1
- Z校正: 基于 m0

### 3.3 四节点链协议

**网络拓扑:** A - B - C - D

**协议时序:**
```
t=0:        生成 A-B, B-C, C-D EPR对
t=delay:    B 执行 BSM，广播结果
t=2*delay:  C 执行 BSM，广播结果  
t=3*delay:  D 应用Pauli校正，协议完成
```

**保真度模型:**
```
F_final = F_link^3 × F_decoherence
F_decoherence = (1 + exp(-2t/T2)) / 2
```

## 4. 张量网络后端

### 4.1 ExaTN集成

使用ExaTN库进行张量网络表示和收缩。

**优势:**
- 高效的张量网络收缩算法
- 支持大规模量子态模拟
- GPU加速支持

**密度矩阵表示:**
- 每个量子比特对应一个2×2张量
- 多比特态通过张量积构建
- 门操作通过张量收缩实现

### 4.2 已知限制

1. **测量后部分迹问题**: 在多量子比特纠缠系统中，测量后执行部分迹可能导致张量网络状态异常
2. **数值精度**: 复数张量的虚部需要在容差范围内

## 5. 与ns-3的集成

### 5.1 事件调度

利用ns-3的 `Simulator::Schedule` 进行：
- 退相干事件调度
- 协议阶段转换
- 经典消息传递模拟

### 5.2 网络栈

```cpp
// 创建经典网络
CsmaHelper csmaHelper;
csmaHelper.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delay)));
NetDeviceContainer devices = csmaHelper.Install(nodes);

// IPv6地址配置
InternetStackHelper stack;
stack.Install(nodes);
Ipv6AddressHelper address;
address.SetBase("2001:1::", Ipv6Prefix(64));
```

### 5.3 应用层接口

量子应用继承自 `ns3::Application`，支持：
- `StartApplication()` / `StopApplication()` 生命周期
- UDP套接字通信
- 属性系统配置

## 6. 性能分析框架

### 6.1 参数扫描

支持的参数：
- **链路保真度**: 0.85, 0.90, 0.95, 0.99
- **T2时间**: 10ms, 50ms, 100ms, 500ms
- **经典延迟**: 1ms, 5ms, 10ms, 50ms

### 6.2 指标收集

- 最终EPR保真度
- 退相干事件计数
- 协议完成时间
- 张量网络收缩统计

## 7. 目录结构

```
contrib/quantum/
├── model/
│   ├── quantum-phy-entity.{h,cc}      # 量子物理实体
│   ├── quantum-memory-model.{h,cc}    # 退相干模型（新增）
│   ├── quantum-network-simulator.{h,cc} # 张量网络后端
│   ├── quantum-channel.{h,cc}         # 量子信道
│   ├── quantum-node.{h,cc}            # 量子节点
│   ├── quantum-operation.{h,cc}       # 量子操作
│   ├── quantum-error-model.{h,cc}     # 错误模型
│   ├── ent-swap-app.{h,cc}            # 纠缠交换应用
│   ├── distribute-epr-protocol.{h,cc} # EPR分发协议
│   └── ...
├── helper/
│   ├── quantum-net-stack-helper.{h,cc}
│   ├── ent-swap-helper.{h,cc}
│   └── ...
├── examples/
│   ├── decoherence-test.cc            # 退相干测试（新增）
│   ├── four-node-chain-test.cc        # 四节点链测试（新增）
│   ├── ent-swap-example.cc
│   └── ...
├── test/
│   └── quantum-test-suite.cc
├── CMakeLists.txt
└── design.md                          # 本文档
```

## 8. 构建和运行

### 8.1 编译

```bash
cd /home/wst/ns-3/ns-3.42
export LIBRARY_PATH=/usr/lib/gcc/x86_64-linux-gnu/13:$LIBRARY_PATH
./ns3 build quantum
```

### 8.2 运行测试

```bash
# 退相干测试
./ns3 run decoherence-test

# 四节点链测试
./ns3 run four-node-chain-test

# 参数扫描
./ns3 run "four-node-chain-test --sweep"

# 自定义参数
./ns3 run "four-node-chain-test --linkFidelity=0.95 --T2=100 --delay=10"
```

## 9. 未来工作

### 9.1 短期目标

1. 修复张量网络测量后部分迹的数值问题
2. 实现完整的纠缠交换协议模拟
3. 添加纠缠纯化协议

### 9.2 长期目标

1. 量子中继器网络模拟
2. 量子纠错码支持
3. 量子密钥分发(QKD)协议
4. 大规模网络拓扑支持
5. 与实际量子硬件参数的校准

## 10. 参考文献

1. Nielsen, M. A., & Chuang, I. L. (2010). Quantum computation and quantum information.
2. Briegel, H. J., et al. (1998). Quantum repeaters: the role of imperfect local operations in quantum communication.
3. ns-3 Documentation: https://www.nsnam.org/documentation/
4. ExaTN: https://github.com/ORNL-QCI/exatn
