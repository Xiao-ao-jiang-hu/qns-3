# 量子网络实现细节 (Implementation Details)

本文档详细说明了 `contrib/quantum/model` 文件夹中各文件的功能及其在量子网络模拟中的实现方式。

## 1. 核心仿真与物理层 (Core Simulation & Physical Layer)

### [quantum-network-simulator.h/cc](quantum-network-simulator.h)
*   **功能**: 量子仿真后端接口。
*   **实现**: 封装了 **ExaTN** (Exascale Tensor Network) 库。它维护一个全局的张量网络（Tensor Network）来表示系统的密度矩阵。支持量子比特的生成、门操作、测量和追踪（Trace out）操作。

### [quantum-phy-entity.h/cc](quantum-phy-entity.h)
*   **功能**: 量子物理实体，是节点与仿真后端之间的桥梁。
*   **实现**: 
    *   提供 `GenerateQubitsPure/Mixed` 用于初始化量子态。
    *   提供 `ApplyGate` 执行量子逻辑门。
    *   提供 `Measure` 执行测量并返回经典结果。
    *   管理物理层误差模型的触发。

### [quantum-node.h/cc](quantum-node.h)
*   **功能**: 扩展了 ns-3 的 `Node` 类，使其具备量子能力。
*   **实现**: 
    *   持有 `QuantumMemory` 和 `QuantumPhyEntity` 的引用。
    *   管理节点的量子地址、端口分配以及量子比特的归属权。

### [quantum-memory.h/cc](quantum-memory.h)
*   **功能**: 模拟量子存储器。
*   **实现**: 维护一个字符串向量 `m_qubits`，记录当前节点存储的所有量子比特名称。提供添加、删除和查询量子比特的方法。

### [quantum-channel.h/cc](quantum-channel.h)
*   **功能**: 量子信道。
*   **实现**: 连接两个量子节点。目前主要负责在纠缠分发过程中引入信道误差（如去极化误差）。

### [qubit.h/cc](qubit.h)
*   **功能**: 量子比特的轻量级表示。
*   **实现**: 主要用于在应用层之间传递量子比特的元数据（如名称和初始态向量），不直接存储物理态。

## 2. 误差模型 (Error Models)

### [quantum-error-model.h/cc](quantum-error-model.h)
*   **功能**: 定义量子操作和存储过程中的噪声。
*   **实现**: 
    *   `QuantumOperation`: 定义一组带有概率的酉算子（Kraus算子）。
    *   `DephaseModel / DepolarModel`: 实现随时间演化的退相干模型。

## 3. 传输层协议与应用 (Transport Protocols & Applications)

### [distribute-epr-protocol.h/cc](distribute-epr-protocol.h)
*   **功能**: 基础纠缠分发协议。
*   **实现**: 
    *   `SrcProtocol`: 在本地生成 EPR 对，将其中一个量子比特通过 `QuantumChannel` 发送给远端。
    *   `DstProtocol`: 接收远端发送的量子比特并存入存储器。

### [ent-swap-app.h/cc](ent-swap-app.h)
*   **功能**: 纠缠交换应用。
*   **实现**: 
    *   中继节点执行贝尔基测量（BSM）。
    *   通过经典 Socket 将测量结果发送至端点。
    *   端点根据结果执行 Pauli 修正门，完成端到端纠缠。

### [telep-app.h/cc](telep-app.h)
*   **功能**: 量子隐形传态应用。
*   **实现**: 
    *   基于已建立的 EPR 对，发送方执行 BSM。
    *   接收方根据经典信令执行修正，恢复量子态。

### [distill-app.h/cc](distill-app.h)
*   **功能**: 纠缠纯化应用。
*   **实现**: 
    *   消耗两对低保真度 EPR 对。
    *   执行 CNOT 和测量操作。
    *   双方交换测量结果，若一致则保留剩余的一对高保真度 EPR 对。

## 4. 辅助工具 (Utilities)

### [quantum-basis.h/cc](quantum-basis.h)
*   **功能**: 定义量子力学常数。
*   **实现**: 预定义了常用的量子门矩阵（H, X, Y, Z, CNOT, S, T 等）和基矢状态。
