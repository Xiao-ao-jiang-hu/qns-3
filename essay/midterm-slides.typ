#import "@preview/touying:0.6.1": *
#import themes.stargazer: *
#import "@preview/numbly:0.1.0": numbly
#import "@preview/cetz:0.3.4": canvas, draw

#show: stargazer-theme.with(
  aspect-ratio: "16-9",
  footer-a: [王思图],
  config-info(
    title: [保真度感知的自适应量子路由协议研究与仿真],
    author: [王思图 \ 导师：施新刚老师],
    date: datetime(year: 2026, month: 3, day: 24),
    institution: [计算机科学与技术系],
  ),
)

#title-slide()

#outline-slide()

= 研究背景与意义
#slide(title: [量子互联网的愿景与挑战])[
  #align(center)[
    #image(height: 80%, "quan-net.png")
  ]
  
  量子互联网的目标在分布式节点间建立高质量量子纠缠，为分布式量子计算、QKD和量子传感网络等应用提供支持。

  *核心需求*：
  1. *高保真度*：纠缠保真度衡量实际纠缠态与理想态的相似程度，量化噪声、退相干等导致的品质衰减，直接决定量子网络任务的可靠性。
  2. *多跳互联*：量子信号随距离指数衰减，需通过量子中继器进行纠缠交换与纯化，实现多跳纠缠分发，建立长距离端到端量子连接。

  *关键挑战*：
  - 纠缠资源*易衰减*（噪声、存储退相干）
  - 生成过程*概率性*（纠缠建立成功率 $< 1$ ）
  - 路由决策需在*保真度、成功率、时延*之间进行复杂权衡。
]

#slide(title: [问题的提出：现有路由机制研究的局限])[
  - *最优性问题*：
    Q-CAST等现有算法使用的路由度量*不满足保序性*，无法保证选出的路径具有最优端到端保真度。而保证最优性的算法已证明是 NP 难问题，无法在实际生产中应用。

  - *路由优化目标局限*：
    多数协议（如Q-OSPF）使用"尽力而为"或"阈值满足"的思路，即将保真度仅视为由用户指定的*硬阈值*（$F > F_"th"$）。
    $arrow$ *缺陷*：忽略了在合格路径中进一步优化保真度的空间。

  - *缺乏异质网络验证*：
    现有研究多在参数均匀的纯量子模拟器（NetSquid等）中验证，未考虑不同链路类型（如卫星与光纤）的经典时延差异对量子存储退相干的影响。



  *本课题目标*：分析现有路由度量在异质网络中的非保序性缺陷，并设计高效且......的保真度感知路由算法，在 QNS-3 混合仿真平台中验证。
]

= 现有工作调研与分析

#slide(title: [现有工作：量子路由协议])[
  *Caleffi (IEEE JSAC 2017)*
  - *机制*：为量子中继网络建立端到端纠缠率精确模型，以最大化纠缠率为路由优化目标。
  - 最早指出量子路由度量的*非保序性*——路径拼接后优劣顺序可能逆转，Dijkstra 贪心选择不再保证全局最优；提出路径枚举求解，但复杂度为指数级。

  *Q-CAST (Shi & Qian, SIGCOMM 2020 / TON 2024)*
  - *机制*：以期望吞吐量 $E_t$ 为度量，扩展 Dijkstra 算法（EDA）+ 多路径备份，高效求解并发纠缠请求。
  - *局限*：$E_t$ 完全忽略经典信令时延差异与节点相干时间，在链路参数异质的网络中*非保序性问题依然存在*，本研究正是针对此展开分析。
]

#slide(title: [现有工作：直接相关工作])[
  *EFiRAP (Zhao et al., INFOCOM 2022)*
  - *机制*：首次量化 E2E 保真度，协同优化路由与纯化策略。
  - *局限*：仍遵循"满足保真度阈值、最大化吞吐量"范式；未分析经典时延异质性对路由最优性的影响。

  *KOP (Wang et al., SIGMETRICS 2025)*
  - 基于路由代数框架分析量子路由度量的非保序性，设计保证收敛性的 K-最优路径算法。与本研究思路最为接近，但未针对时延异质（卫星-地面混合）场景具体分析。

  *QuESat (Gu et al., INFOCOM 2025)*
  - 提出 LEO 卫星星座量子中继网络，卫星链路高时延与地面节点低相干时间并存，为本研究的异质场景提供了现实依据。
]

#slide(title: [仿真平台：现有工具与本工作的选择])[
  *NetSquid, SeQUeNCe（已有量子仿真器）*：
  - 物理层建模细致（密度矩阵演化），但量子事件与经典协议栈（TCP/IP）调度分离。
  - 无法模拟不同类型链路（如卫星 vs 地面光纤）的时延差异对量子存储退相干的影响，难以复现异质网络中的路由失效场景。

  *QNS-3（本工作采用的平台）*：
  - 已有工作：基于 NS-3 构建了量子物理层，量子事件与经典报文在同一时间轴调度。
  - *本工作新增*：在此基础上实现量子链路层（L2）与网络层（L3），补全完整的量子网络协议栈，支持异质网络路由的仿真实验。
]

= 前期工作与阶段性成果

#slide(title: [成果一：路由非保序性的理论分析])[
  保序性要求：若 $F(A) >= F(B)$，则对任意后续路径 $C$，均有 $F(A C) >= F(B C)$。若不满足，Dijkstra 的逐跳贪心选择*无法保证全局最优*。

  #grid(columns: (1.15fr, 1fr), gutter: 1em)[
    #canvas(length: 0.88cm, {
      let node(pos, label, fill: white) = {
        draw.circle(pos, radius: 0.5, fill: fill, stroke: black)
        draw.content(pos, text(size: 0.82em, label))
      }
      // 节点：S(0,0)  M(6,0)  D(11,0)
      node((0, 0), $S$)
      node((6, 0), $M$)
      node((11, 0), $D$, fill: luma(230))
      // 路径 A：卫星，上弧（红色虚线）
      draw.bezier(
        (0.5, 0.1), (5.5, 0.1),
        (2.0, 2.8), (4.0, 2.8),
        stroke: (paint: red, dash: "dashed", thickness: 1.5pt),
        mark: (end: ">"),
      )
      // A 标注放在弧顶上方
      draw.content((3.0, 3.15), text(size: 0.7em, fill: red)[A（卫星）　$delta_A = 100 "ms"$，$tau_"relay" = +infinity$])
      // 路径 B：地面光纤，下弧（蓝色实线）
      draw.bezier(
        (0.5, -0.1), (5.5, -0.1),
        (2.0, -2.8), (4.0, -2.8),
        stroke: (paint: blue, thickness: 1.5pt),
        mark: (end: ">"),
      )
      // B 标注放在弧底下方
      draw.content((3.0, -3.15), text(size: 0.7em, fill: blue)[B（光纤）　$delta_B = 5 "ms"$，$tau_"relay" = 1 "s"$])
      // 中间标注（两弧之间空白处）
      draw.content((3.0, 0), align(center, text(size: 0.72em)[
        $F(A) > F(B)$\
        #text(fill: red.darken(10%))[Dijkstra 在此选 A ✗]
      ]))
      // 路径 C：M → D（灰色实线）
      draw.line(
        (6.5, 0), (10.5, 0),
        stroke: (paint: luma(120), thickness: 1.5pt),
        mark: (end: ">"),
      )
      // C 标注和 τ_D 分开放
      draw.content((8.5, 0.5), text(size: 0.7em, fill: luma(80))[C（共享）])
      draw.content((11.0, -0.8), text(size: 0.68em, fill: luma(60))[$tau_D = 0.1 "s"$])
    })
  ][
    在本例中，路径 A 经过完美存储器，保真度损耗小，$F(A) > F(B)$，Dijkstra 贪心选 A。节点 $D$ 相干时间 $tau_D = 0.1$ s。路径 A 积累了 100 ms 时延，在 $D$ 处造成严重退相干；路径 B 仅 5 ms。

    $
      F(A C) slash F(B C) approx 17%
    $

    *排序逆转*：$F(A) > F(B)$，但 $F(A C) < F(B C)$，*保序性不成立*。
  ]
]

#slide(title: [成果二：QNS-3 量子网络层协议栈实现])[
  已在 QNS-3 平台中实现*量子链路层（L2）与网络层（L3）*：

  - *量子链路层*（`QuantumLinkLayerService`）：邻居间纠缠对分发、状态机管理（PENDING / READY / CONSUMED）
  - *量子网络层*（`QuantumNetworkLayer`）：多跳路径建立与生命周期管理、纠缠交换协调
  - *路由协议*：实现 `DijkstraRoutingProtocol` 与 `QCastRoutingProtocol`，支持对比实验
  - *经典信令*：通过 IPv6/UDP 套接字承载，时延由协议栈产生，与量子退相干同步

  已验证的仿真场景：隐形传态（线性链路）、纠缠蒸馏、纠缠交换。
]

= 研究计划

#slide(title: [时间规划（更新）])[

  1. *✓ 第一阶段：仿真平台开发*（已完成）：在 QNS-3 中实现 L2/L3 协议栈，移植 Dijkstra 与 Q-CAST

  2. *✓ 第二阶段：现有算法分析与失效识别*（已完成）：在异质拓扑上定量测量路由决策偏差，理论分析 Q-CAST 非保序性

  3. *→ 第三阶段：新算法设计*（进行中）：针对非保序性根源，设计在时延异质网络中保证路径最优性的路由算法

  4. *第四阶段：实验评估与论文撰写*：对比新旧算法的端到端保真度与成功率，完成论文与答辩
]

= 总结
#slide()[
  #v(2em)
  *研究问题*：现有量子路由算法（Q-CAST等）在经典参数异质网络中的路由度量不满足保序性，无法保证选出最优路径。

  前期成果：
  1. 证明了 Q-CAST 在卫星-地面等时延差异大的场景下的非保序性，给出保序比下界。
  2. 在 QNS-3 中实现了量子链路层与网络层，支持异质场景下的路由对比实验。

  后续计划：设计在时延异质网络中保证路径最优性的路由算法，完成实验评估。

  #align(center)[
    #v(3em)
    #text(size: 1.5em, weight: "bold")[汇报完毕]

    #v(1em)
    敬请各位老师批评指正
  ]
]
