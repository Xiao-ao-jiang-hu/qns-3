# 量子网络层 (Quantum Network Layer) 架构设计

本文档描述了在 ns-3 框架下，基于 qns-3 已有的物理层和链路层实现，设计的量子网络层架构。该设计旨在实现量子网络与经典网络的协同模拟，并提供高度的可扩展性以支持各种量子路由协议。

## 1. 设计目标

*   **兼容性**：遵循 ns-3 的架构规范（如 `Node`, `NetDevice`, `Protocol` 等基类）。
*   **统一时间轴**：量子事件（纠缠产生、退相干、测量）与经典事件（分组发送、处理）在同一个 `ns3::Simulator` 时间轴中调度。
*   **协议无关性**：通过抽象路由接口，支持从简单的最短路径路由到复杂的动态多路径路由协议。
*   **经典-量子协同**：量子网络层依赖经典网络层进行信令传输（如 BSM 结果通知、纠缠纯化协调）。

## 2. 架构概述

量子网络采用分层架构，确保物理模拟的准确性与网络协议的灵活性。

### 2.1 核心组件关系

```text
+-------------------------------------------------------+
|                  Quantum Application                  |
| (Teleportation, Distillation, Entanglement Swapping)  |
+-------------------------------------------------------+
                           |
+-------------------------------------------------------+
|                 Quantum Transport Layer               |
|  (End-to-End Entanglement Management & Reliability)    |
+-------------------------------------------------------+
                           |
+-------------------------------------------------------+
|                 Quantum Network Layer                 |
|  +-----------------------+  +-----------------------+ |
|  |  EntanglementManager  |  | QuantumRoutingProtocol| |
|  +-----------------------+  +-----------------------+ |
+-------------------------------------------------------+
                           |
+-------------------------------------------------------+
|                 Quantum Physical Layer                |
|  +-----------------------+  +-----------------------+ |
|  |    QuantumMemory      |  |    QuantumPhyEntity   | |
|  +-----------------------+  +-----------------------+ |
|  |    QuantumChannel     |  |    QuantumBackend     | |
|  +-----------------------+  +-----------------------+ |
+-------------------------------------------------------+
```

## 3. 关键组件设计

### 3.1 量子物理层 (Quantum Physical Layer)
物理层负责量子态的底层模拟和硬件资源管理。
*   **QuantumPhyEntity**: 物理层核心，提供量子门（H, CNOT, Pauli）、测量和量子比特生成的统一接口。它直接与仿真后端交互。
*   **QuantumBackend (ExaTN)**: 基于张量网络的仿真后端，负责维护全局量子态并执行实际的数学运算。
*   **QuantumMemory**: 模拟量子存储器，管理节点内量子比特的槽位、相干时间及索引。
*   **Error Models**: 
    *   **信道误差**: `QuantumChannel` 中的去极化模型。
    *   **存储误差**: 随时间演化的退相干模型（Dephase/Depolar）。

### 3.2 量子传输层 (Quantum Transport Layer)
传输层负责端到端的量子任务调度，确保量子信息的可靠传输。
*   **功能**:
    *   **纠缠交换 (Entanglement Swapping)**: 实现跨多跳节点的端到端纠缠建立。
    *   **纠缠纯化 (Entanglement Distillation)**: 通过消耗多对低质量纠缠对来提升目标纠缠对的保真度。
    *   **隐形传态 (Teleportation)**: 实现量子比特状态的端到端迁移。
*   **机制**: 传输层协议（如 `TelepApp`, `DistillApp`）通过经典网络交换 BSM 结果或同步信号，驱动物理层执行相应的量子操作。

### 3.3 量子网络层 (Quantum Network Layer)
(此处保留之前设计的路由和资源管理逻辑)
*   **QuantumL3Protocol**: 类似于经典网络中的 `Ipv4L3Protocol`。
*   **功能**：
    *   管理节点上的所有 `QuantumNetDevice`。
    *   维护端到端纠缠请求的状态（`EntanglementContext`）。
    *   调用 `QuantumRoutingProtocol` 进行路径决策。
    *   处理来自经典协议栈的量子控制信令。

### 3.2 QuantumRoutingProtocol (抽象基类)
定义了量子路由协议必须实现的接口。
*   **接口方法**：
    *   `RouteOutput()`：为新的纠缠请求选择路径。
    *   `RouteInput()`：处理中继节点上的纠缠交换请求。
    *   `NotifyEntanglementFailure()`：当链路纠缠失败时更新路由表。

### 3.3 QuantumHeader (经典分组头部)
量子网络层不直接传输“量子比特分组”，而是通过经典分组携带控制信息。
*   **字段**：
    *   `SourceID / DestinationID`：端到端地址。
    *   `FlowID`：区分不同的纠缠流。
    *   `OperationType`：如 `EPR_GEN`, `ENT_SWAP`, `PURIFY`, `MEASURE`。
    *   `QubitIndex`：涉及的本地量子存储器索引。

### 3.4 EntanglementManager
负责本地量子资源的生命周期管理。
*   **功能**：
    *   跟踪量子存储器（`QuantumMemory`）中每个量子比特的状态。
    *   根据路由层的指令触发 `QuantumPhyEntity` 执行物理操作。
    *   处理退相干（Decoherence）导致的纠缠失效。

## 4. 协同模拟机制

量子网络与经典网络在同一时间轴运行的逻辑如下：
1.  **经典信令驱动**：量子操作（如纠缠交换）通常需要经典信息的到达作为触发条件。经典分组的传输延迟由 ns-3 物理层模型计算。
2.  **量子状态演化**：量子比特的保真度随时间衰减。`EntanglementManager` 在执行操作前，根据 `Simulator::Now()` 与上次操作时间的差值，调用 `QuantumErrorModel` 更新状态。
3.  **事件调度**：纠缠产生成功/失败事件被加入 `Simulator` 队列，与经典分组处理事件交织执行。

## 5. 路由协议实现示例

该架构可以轻松迁移到以下量子路由协议：

### 5.1 最短路径路由 (Quantum Dijkstra Routing)
*   **实现**：`QuantumDijkstraRouting` 类继承自 `QuantumRoutingProtocol`。
*   **逻辑**：
    *   维护一个全局拓扑图（Adjacency List）。
    *   权值（Cost）可配置为 $-\log(P_{success})$，其中 $P_{success}$ 是链路纠缠成功的概率。
    *   使用优先队列实现的 Dijkstra 算法计算从当前节点到目的节点的下一跳。
    *   该协议作为量子路由的基准实现，放置在 `model/` 目录下以供所有量子实验复用。

### 5.2 Q-Cast (多路径路由)
*   **实现**：路由协议不返回单一路径，而是返回一个转发图（Forwarding Graph）。
*   **逻辑**：`QuantumL3Protocol` 根据转发图在多个邻居节点同时尝试纠缠产生，提高端到端成功率。

### 5.3 SLMP (分段链路管理协议)
*   **实现**：将长路径划分为多个段（Segments）。
*   **逻辑**：`QuantumRoutingProtocol` 在 `RouteInput` 中识别段边界，仅在段内纠缠准备就绪时触发跨段交换。

## 6. 设计合理性说明

1.  **解耦控制与执行**：将路由逻辑（Routing）与资源管理（EntanglementManager）分离，使得研究人员可以专注于算法优化而无需修改底层物理模拟。
2.  **利用经典协议栈**：量子网络离不开经典控制。通过在 `QuantumL3Protocol` 中集成经典 Socket，直接复用了 ns-3 成熟的 TCP/IP 协议栈进行信令传输，符合量子互联网的实际物理架构。
3.  **状态一致性**：所有量子状态的变更都通过 `QuantumNetworkSimulator` (ExaTN 后端) 统一管理，确保了模拟的物理准确性，同时网络层提供了必要的抽象以应对大规模拓扑。
