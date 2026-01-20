# 量子网络层设计文档

## 1. 项目背景与目标

### 1.1 背景
qns-3（基于ns-3的量子网络模拟器）已经提供了基础的量子网络模拟能力，包括量子节点、量子通道、物理实体等基础设施。然而，当前的qns-3缺少一个**协议无关的量子网络层**，这使得实现不同的量子路由协议变得困难。

### 1.2 目标
设计并实现一个**通用的量子网络层**，具有以下特点：
1. **协议无关性**：网络层与具体的路由协议解耦，支持多种路由算法（如Dijkstra、距离向量等）
2. **可扩展性**：提供插件式架构，易于添加新的度量系统、转发策略和资源管理策略
3. **与现有qns-3组件集成**：与已有的`QuantumNode`、`QuantumChannel`、`DistributeEPRProtocol`和`EntSwapApp`等组件无缝集成
4. **资源感知**：路由协议可以考虑节点的量子比特内存和通道的EPR对容量等资源约束

## 2. 整体架构设计

### 2.1 架构概览
量子网络层采用了分层设计，主要包含以下核心组件：

```
┌─────────────────────────────────────────┐
│           Quantum Network Layer          │
├─────────────────────────────────────────┤
│  QuantumRoutingProtocol (抽象基类)       │
│  ├── 路由算法实现 (Dijkstra, DV等)       │
│  └── 邻居发现和路由表管理                │
├─────────────────────────────────────────┤
│  QuantumForwardingEngine (抽象基类)      │
│  ├── 数据包转发逻辑                      │
│  ├── 纠缠建立和交换管理                  │
│  └── 转发策略 (预建立/按需/混合)         │
├─────────────────────────────────────────┤
│  QuantumResourceManager (抽象基类)       │
│  ├── 量子比特内存管理                    │
│  ├── EPR对容量管理                       │
│  └── 资源预约和释放                      │
├─────────────────────────────────────────┤
│  QuantumMetric (抽象基类)                │
│  ├── 保真度度量                          │
│  ├── 延迟度量                            │
│  ├── 错误率度量                          │
│  └── 复合度量                            │
└─────────────────────────────────────────┘
```

### 2.2 数据流
1. **路由请求**：应用层 → `QuantumNetworkLayer` → `QuantumRoutingProtocol`
2. **资源检查**：`QuantumRoutingProtocol` → `QuantumResourceManager`
3. **路由计算**：使用`QuantumMetric`计算路径成本
4. **转发执行**：`QuantumNetworkLayer` → `QuantumForwardingEngine`
5. **纠缠建立**：`QuantumForwardingEngine` → `DistributeEPRProtocol`/`EntSwapApp`

## 3. 文件详细说明

### 3.1 基础数据结构

#### `contrib/quantum/model/quantum-route-types.h/cc`
**目的**：定义量子网络层所需的核心数据结构。

**主要结构**：
- `QuantumForwardingStrategy`：转发策略枚举（预建立、按需、混合）
- `QuantumRouteRequirements`：路由需求（最小保真度、最大延迟、量子比特数等）
- `QuantumRoute`：量子路由（源、目的、路径、成本、估计保真度等）
- `QuantumRouteEntry`：路由表项（目的、下一跳、成本、时间戳等）
- `QuantumNetworkStats`：网络统计信息

**关键修改**：
- 添加了`#include "quantum-channel.h"`以解决`Ptr<QuantumChannel>`的编译错误
- 实现了`QuantumRoute::ToString()`方法用于调试输出

### 3.2 度量系统

#### `contrib/quantum/model/quantum-metric.h/cc`
**目的**：定义量子网络度量系统的抽象接口和具体实现。

**抽象基类**：`QuantumMetric`
- `CalculateChannelCost()`：计算单个通道的成本
- `CalculatePathCost()`：计算整条路径的成本
- `GetName()`：获取度量名称
- `IsAdditive()`：检查是否可加性度量
- `IsMonotonic()`：检查是否单调性度量

**具体实现**：
1. `FidelityMetric`：基于保真度的度量
   - 成本 = 1 - 保真度（保真度越高，成本越低）
   - 路径成本 = 1 - 乘积(通道保真度)（乘性度量）

2. `DelayMetric`：基于延迟的度量
   - 成本 = 延迟时间
   - 路径成本 = 各通道延迟之和（加性度量）

3. `ErrorRateMetric`：基于错误率的度量
   - 成本 = 错误率
   - 路径成本 = 1 - 乘积(1 - 通道错误率)（乘性度量）

4. `CompositeMetric`：复合度量
   - 支持多个度量的加权组合
   - 可配置不同的权重

**关键修改**：
- 添加了`static TypeId GetTypeId(void)`声明到头文件
- 为`CompositeMetric`添加了默认构造函数以满足ns-3的`AddConstructor`要求

### 3.3 资源管理器

#### `contrib/quantum/model/quantum-resource-manager.h/cc`
**目的**：管理量子网络资源（量子比特内存、EPR对容量）的抽象接口和默认实现。

**抽象基类**：`QuantumResourceManager`
- `ReserveQubits()`：预约节点量子比特
- `ReserveEPRPairs()`：预约通道EPR对
- `GetAvailableQubits()`：获取可用量子比特数
- `GetAvailableEPRCapacity()`：获取可用EPR对容量
- `CheckRouteResources()`：检查路由资源是否足够
- `ReserveRouteResources()`：为路由预约资源

**默认实现**：`DefaultQuantumResourceManager`
- 节点资源：默认每个节点100个量子比特
- 通道资源：默认每个通道10个EPR对容量
- 支持定时自动释放资源
- 提供资源利用率和可用性查询

**关键修改**：
- 添加了`static TypeId GetTypeId(void)`声明到头文件
- 将`DefaultQuantumResourceManager::GetTypeId()`方法改为`static`

### 3.4 转发引擎

#### `contrib/quantum/model/quantum-forwarding-engine.h/cc`
**目的**：实现量子数据包转发和纠缠管理的抽象接口和默认实现。

**抽象基类**：`QuantumForwardingEngine`
- `ForwardPacket()`：转发量子数据包
- `ForwardQubits()`：转发特定量子比特
- `EstablishEntanglement()`：沿路由建立纠缠
- `PerformEntanglementSwap()`：执行纠缠交换
- `DistributeEPR()`：在通道上分发EPR对
- `GetEstimatedDelay()`：获取路由建立延迟估计

**默认实现**：`DefaultQuantumForwardingEngine`
- 支持三种转发策略：`QFS_PRE_ESTABLISHED`、`QFS_ON_DEMAND`、`QFS_HYBRID`
- 跟踪活跃路由状态
- 集成资源管理器进行资源检查
- 提供网络统计信息收集

**关键修改**：
- 添加了`static TypeId GetTypeId(void)`声明到头文件
- 将`DefaultQuantumForwardingEngine::GetTypeId()`方法改为`static`

### 3.5 路由协议

#### `contrib/quantum/model/quantum-routing-protocol.h/cc`
**目的**：定义量子路由协议的抽象接口和简单实现。

**抽象基类**：`QuantumRoutingProtocol`
- `DiscoverNeighbors()`：发现邻居节点
- `UpdateRoutingTable()`：更新路由表
- `RouteRequest()`：处理路由请求
- `GetRoute()`：获取到目的地的路由
- `AddRoute()`：添加路由表项
- `ReceivePacket()`：接收路由数据包
- `SendPacket()`：发送路由数据包

**简单实现**：`SimpleQuantumRoutingProtocol`
- 基于邻居发现的路由协议
- 使用度量系统计算路径成本
- 支持路由过期机制
- 提供路由回调通知

**关键修改**：
- 添加了`static TypeId GetTypeId(void)`声明到头文件
- 将`SimpleQuantumRoutingProtocol::GetTypeId()`方法改为`static`
- 添加了`#include "ns3/quantum-packet.h"`以解决`QuantumPacket`类型错误

### 3.6 网络层主类

#### `contrib/quantum/model/quantum-network-layer.h/cc`
**目的**：量子网络层的主类，协调路由协议、转发引擎和资源管理器。

**主要功能**：
- `SendPacket()`：发送量子数据包
- `ReceivePacket()`：接收量子数据包
- `InstallRoutingProtocol()`：安装路由协议
- `InstallForwardingEngine()`：安装转发引擎
- `InstallResourceManager()`：安装资源管理器
- `GetNeighbors()`：获取邻居通道列表
- `GetAddress()`：获取节点地址

**关键特性**：
- 协调路由协议、转发引擎和资源管理器的交互
- 提供统一的API给上层应用
- 管理邻居信息和网络状态

### 3.7 数据包类

#### `contrib/quantum/model/quantum-packet.h/cc`
**目的**：定义量子网络数据包。

**主要功能**：
- 携带源和目的地址
- 包含量子比特引用列表
- 支持不同类型的量子数据包（数据、路由、控制等）
- 支持不同的协议类型（量子路由、量子转发、EPR分发等）
- 可关联路由信息

**与网络层的集成**：
- 网络层使用`QuantumPacket`进行数据包转发
- 路由协议使用`QuantumPacket`进行路由信息交换

### 3.8 与现有组件的集成

#### `contrib/quantum/model/quantum-node.h/cc`
**修改**：添加了量子网络层成员和访问方法
- `SetQuantumNetworkLayer()`：设置网络层
- `GetQuantumNetworkLayer()`：获取网络层
- `InstallQuantumNetworkLayer()`：安装网络层

#### `contrib/quantum/helper/quantum-net-stack-helper.h/cc`
**修改**：扩展了量子网络栈助手
- `InstallNetworkLayer()`：安装量子网络层
- 在`Install()`方法中自动安装网络层

#### `contrib/quantum/model/quantum-network-simulator.cc`
**修改**：修复符号比较警告
- 第1047行：`for (int i = ...)` → `for (unsigned i = ...)`

#### `contrib/quantum/model/quantum-basis.cc`
**修改**：修复符号比较警告
- 多个循环：`for (int i = 0; ...)` → `for (size_t i = 0; ...)`

### 3.9 示例程序

#### `contrib/quantum/examples/quantum-network-layer-example.cc`
**目的**：演示量子网络层的使用。

**演示功能**：
1. 创建量子节点和通道
2. 安装量子网络层
3. 演示路由协议邻居发现
4. 演示路由请求和资源预约
5. 演示度量系统使用
6. 演示数据包发送

**关键修改**：
- 修复协议常量：`PROTO_QUANTUM` → `PROTO_QUANTUM_FORWARDING`
- 添加Internet栈安装以支持`DistributeEPRProtocol`的socket需求

**更新（2026-01-20）**：
- **包地址修复**：修复`QuantumPacket`构造函数调用问题，使用`SetSourceAddress()`和`SetDestinationAddress()`方法显式设置包地址
- **路由发现机制**：实现`PerformRouteDiscovery()`函数，模拟完整路由发现过程（路由请求、转发、回复、路由表更新）
- **包创建修复**：使用`CreateObject<QuantumPacket>()`无参构造函数，然后手动设置地址，避免类型转换错误
- **路由获取优化**：使用`GetRoute()`方法从路由表获取路由，替代可能失败的`RouteRequest()`调用


## 5. 设计模式与架构原则

### 5.1 设计模式应用

1. **策略模式**：路由协议、度量系统、转发引擎和资源管理器都是可插拔的策略
2. **抽象工厂模式**：提供默认实现的工厂方法（如`GetDefaultResourceManager()`）
3. **观察者模式**：使用回调函数通知路由状态变化
4. **组合模式**：`CompositeMetric`可以组合多个度量

### 5.2 架构原则

1. **开闭原则**：对扩展开放，对修改关闭。新的路由算法可以继承`QuantumRoutingProtocol`而不修改现有代码。
2. **依赖倒置原则**：高层模块不依赖低层模块，二者都依赖抽象。
3. **单一职责原则**：每个类有明确的单一职责。
4. **接口隔离原则**：使用多个专门的接口而不是一个庞大的接口。

### 5.3 与ns-3的集成模式

1. **继承ns-3的Object类**：所有主要组件都继承自`ns3::Object`，支持ns-3的对象系统。
2. **使用TypeId系统**：支持ns-3的类型识别和对象创建系统。
3. **使用Ptr智能指针**：使用ns-3的引用计数智能指针管理对象生命周期。
4. **使用日志系统**：集成ns-3的日志系统进行调试输出。

## 6. 使用示例

### 6.1 基本使用流程

```cpp
// 创建量子物理实体
std::vector<std::string> owners = {"Alice", "Bob", "Charlie"};
Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(owners);

// 创建量子节点
Ptr<QuantumNode> alice = CreateObject<QuantumNode>(qphyent, "Alice");
Ptr<QuantumNode> bob = CreateObject<QuantumNode>(qphyent, "Bob");

// 安装Internet栈（为DistributeEPRProtocol提供支持）
InternetStackHelper internet;
NodeContainer nodes;
nodes.Add(alice);
nodes.Add(bob);
internet.Install(nodes);

// 安装量子网络栈（包括网络层）
QuantumNetStackHelper netStackHelper;
netStackHelper.Install(alice, bob);

// 获取网络层和路由协议
Ptr<QuantumNetworkLayer> aliceLayer = alice->GetQuantumNetworkLayer();
Ptr<QuantumRoutingProtocol> aliceRouting = aliceLayer->GetRoutingProtocol();

// 发现邻居
aliceRouting->DiscoverNeighbors();

// 创建路由请求
QuantumRouteRequirements requirements;
requirements.minFidelity = 0.8;
requirements.maxDelay = Seconds(1.0);
requirements.numQubits = 2;
requirements.duration = Seconds(10.0);

QuantumRoute route = aliceRouting->RouteRequest("Alice", "Bob", requirements);

// 创建和发送量子数据包
Ptr<QuantumPacket> packet = CreateObject<QuantumPacket>("Alice", "Bob");
packet->SetType(QuantumPacket::DATA);
packet->SetProtocol(QuantumPacket::PROTO_QUANTUM_FORWARDING);
packet->SetRoute(route);

aliceLayer->SendPacket(packet);
```

### 6.2 自定义路由协议

```cpp
class MyQuantumRoutingProtocol : public QuantumRoutingProtocol
{
public:
  static TypeId GetTypeId(void)
  {
    static TypeId tid = TypeId("ns3::MyQuantumRoutingProtocol")
      .SetParent<QuantumRoutingProtocol>()
      .AddConstructor<MyQuantumRoutingProtocol>();
    return tid;
  }
  
  // 实现所有抽象方法
  void DiscoverNeighbors() override { /* 实现 */ }
  QuantumRoute RouteRequest(...) override { /* 实现 */ }
  // ... 其他方法
};
```

## 8. 编译和使用方式

### 8.1 编译量子模块
```bash
# 构建整个ns-3（包括量子模块）
./ns3 build

# 仅构建量子模块
./ns3 build quantum

# 构建并运行量子网络层示例
./ns3 run quantum-network-layer-example
```

### 8.2 运行示例程序
```bash
# 启用详细日志输出
NS_LOG="QuantumNetworkLayer=info:QuantumNetworkSimulator=info" ./ns3 run quantum-network-layer-example

# 启用调试级别日志
NS_LOG="QuantumNetworkLayer=debug:QuantumRoutingProtocol=debug" ./ns3 run quantum-network-layer-example
```

### 8.3 创建自定义量子应用
1. **继承现有类**：创建新的路由协议、度量系统或转发引擎
2. **集成到网络层**：使用`SetRoutingProtocol()`、`SetMetric()`等方法配置
3. **运行模拟**：编写示例程序测试功能

### 8.4 调试建议
1. **日志系统**：使用ns-3的日志系统输出调试信息
2. **统计信息**：通过`GetStatistics()`方法收集性能数据
3. **断点调试**：使用gdb或IDE调试ns-3程序

### 8.5 性能优化
1. **编译优化**：使用`./ns3 configure --build-profile=optimized`
2. **并行构建**：使用`./ns3 build -j$(nproc)`
3. **内存管理**：注意量子模拟的内存使用，适当调整网络规模

## 9. Q-CAST协议实现

### 9.1 协议概述
Q-CAST (Contention-Free pAth Selection at runTime) 是一种完全在线、无冲突的量子网络路径选择协议，包含四个阶段：
- **P1**：通过经典互联网获知源-目的地对
- **P2**：运行一致的路由算法，选择路径并预留资源
- **P3**：交换k跳邻居的链路状态信息
- **P4**：基于局部链路状态进行异或恢复决策

### 9.2 实现架构
Q-CAST协议完全基于现有抽象接口实现，不修改任何基类：

#### 9.2.1 核心组件
1. **QCastRoutingProtocol**：继承`QuantumRoutingProtocol`，实现G-EDA算法
2. **ExpectedThroughputMetric**：继承`QuantumMetric`，计算期望吞吐量E_t
3. **QCastForwardingEngine**：继承`QuantumForwardingEngine`，实现异或恢复和对数时间交换调度

#### 9.2.2 辅助数据结构
```cpp
// Q-CAST专用路由信息
struct QCastRouteInfo {
  QuantumRoute mainRoute;                    // 主路径
  std::vector<QuantumRoute> recoveryPaths;   // 恢复路径列表
  std::map<uint32_t, std::vector<QuantumRoute>> rings; // 恢复环信息
  double successProbability;                 // 成功概率估计
};

// k跳链路状态（k=3）
struct KHopLinkState {
  std::map<std::string, LinkQuality> linkQuality;  // 3跳内链路质量
  Time lastUpdate;
};
```

### 9.3 关键算法实现

#### 9.3.1 G-EDA算法（贪婪扩展Dijkstra算法）
```
输入：残存网络图G，S-D对集合
输出：无冲突路径集合

for each S-D pair in 所有S-D对:
  使用EDA算法在G中找最优路径（按E_t度量）
  
选择全局最优路径P，通过ResourceManager预留资源
更新残存网络图G（移除P占用的资源）

重复直到找不到新路径或达到上限

// 恢复路径发现
for each node in 主路径:
  寻找连接到其前方3跳内节点的无冲突路径
```

#### 9.3.2 异或恢复决策
```
输入：主路径边集合E，恢复环边集合E_p1, E_p2, ...
输出：交换决策

每个节点基于本地3跳链路状态，计算：
  E_final = E ⊕ E_p1 ⊕ E_p2 ⊕ ...
  
如果源和目的在(V, E_final)中连通，则使用对应的恢复环
```

#### 9.3.3 对数时间交换调度
```
将h跳路径组织成高度为log(h)的二叉树：
- 叶子节点：直接相邻的纠缠对
- 内部节点：等待子节点完成交换后再执行交换
- 总交换步骤数：O(log h)
```

### 9.4 示例程序位置
Q-CAST协议的完整实现在`contrib/quantum/examples/qcast-protocol-example.cc`中，包含：
1. 协议类的定义和实现
2. 测试拓扑创建
3. 四阶段协议执行演示
4. 性能统计和分析

### 9.5 使用示例
```cpp
// 创建Q-CAST路由协议
Ptr<QCastRoutingProtocol> qcastProtocol = CreateObject<QCastRoutingProtocol>();

// 配置期望吞吐量度量
Ptr<ExpectedThroughputMetric> etMetric = CreateObject<ExpectedThroughputMetric>();
qcastProtocol->SetMetric(etMetric);

// 设置k=3的邻居发现
qcastProtocol->SetKHopDistance(3);

// 集成到网络层
Ptr<QuantumNetworkLayer> networkLayer = node->GetQuantumNetworkLayer();
networkLayer->SetRoutingProtocol(qcastProtocol);

// 执行四阶段协议
qcastProtocol->DiscoverNeighbors();  // P1+P3阶段
QuantumRoute route = qcastProtocol->RouteRequest(src, dst, requirements);  // P2阶段

// P4阶段通过转发引擎自动执行
```

### 9.6 编译和运行
```bash
# 编译Q-CAST示例
./ns3 build quantum

# 运行Q-CAST协议示例
./ns3 run qcast-protocol-example

# 带详细日志运行
NS_LOG="QCastRoutingProtocol=info:ExpectedThroughputMetric=debug" ./ns3 run qcast-protocol-example
```

## 10. 总结
量子网络层设计提供了灵活、可扩展的架构，支持多种量子路由协议的实现。Q-CAST协议作为具体实现示例，展示了如何在不修改抽象接口的情况下实现复杂的量子网络算法。

---
*文档版本：2.0*
*最后更新：2026年1月20日*
*作者：qns-3开发团队*

