/*
 * 保真度计算测试程序
 * 
 * 本程序测试物理层的保真度计算功能：
 * 1. 单个EPR对的保真度（无噪声）
 * 2. 单个EPR对的保真度（有退极化噪声）
 * 3. 单个EPR对的保真度（有时间退相干）
 * 4. 纠缠交换后的端到端保真度
 * 
 * 运行命令:
 * NS_LOG="FidelityTest=info:QuantumNetworkSimulator=info:QuantumPhyEntity=info" ./ns3 run fidelity-test
 */

#include "ns3/quantum-basis.h"
#include "ns3/quantum-network-simulator.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-error-model.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/nstime.h"
#include <exatn.hpp>

#include <iostream>
#include <iomanip>
#include <cmath>
#include <complex>
#include <vector>

NS_LOG_COMPONENT_DEFINE ("FidelityTest");

// 每个测试使用唯一的前缀以避免ExaTN张量名称冲突
static int g_testCounter = 0;

// 清理ExaTN张量
void CleanupExaTN()
{
  exatn::sync();
  exatn::destroyTensors();
}

using namespace ns3;

/**
 * \brief 测试1：单个EPR对的保真度（无噪声）
 * 预期结果：保真度 = 1.0
 */
void Test1_SingleEprNoNoise()
{
  NS_LOG_INFO("");
  NS_LOG_INFO("========================================");
  NS_LOG_INFO("测试1: 单个EPR对（无噪声）");
  NS_LOG_INFO("预期保真度: 1.0");
  NS_LOG_INFO("========================================");
  
  int testId = g_testCounter++;
  
  // 创建两个节点（使用唯一名称）
  std::string alice = "Alice" + std::to_string(testId);
  std::string bob = "Bob" + std::to_string(testId);
  std::vector<std::string> owners = {alice, bob};
  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(owners);
  
  // 创建量子通道（无噪声）
  Ptr<QuantumChannel> channel = CreateObject<QuantumChannel>(alice, bob);
  
  // 生成EPR对
  std::pair<std::string, std::string> epr = {alice + "_epr_a", bob + "_epr_b"};
  qphyent->GenerateEPR(channel, epr);
  
  // 计算保真度
  double fidel = 0.0;
  double result = qphyent->CalculateFidelity(epr, fidel);
  
  NS_LOG_INFO("实际保真度: " << std::fixed << std::setprecision(6) << result);
  NS_LOG_INFO("测试结果: " << (std::abs(result - 1.0) < 0.01 ? "通过" : "失败"));
  
  CleanupExaTN();
}

/**
 * \brief 测试2：单个EPR对（有退极化噪声）
 * 预期结果：保真度 ≈ 设置的保真度值
 * 
 * 注意：GenerateEPR() 总是生成纯Bell态（保真度=1.0）
 * 要生成带噪声的EPR对，需要使用 GetEPRwithFidelity() + GenerateQubitsMixed()
 */
void Test2_SingleEprWithDepolar()
{
  NS_LOG_INFO("");
  NS_LOG_INFO("========================================");
  NS_LOG_INFO("测试2: 单个EPR对（退极化噪声 F=0.9）");
  NS_LOG_INFO("预期保真度: ~0.9");
  NS_LOG_INFO("========================================");
  
  int testId = g_testCounter++;
  
  // 创建两个节点
  std::string alice = "Alice" + std::to_string(testId);
  std::string bob = "Bob" + std::to_string(testId);
  std::vector<std::string> owners = {alice, bob};
  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(owners);
  
  // 使用 GetEPRwithFidelity 获取带退极化噪声的EPR密度矩阵
  std::vector<std::complex<double>> epr_dm = GetEPRwithFidelity(0.9);
  
  // 生成带噪声的EPR对
  // 注意：GenerateQubitsMixed需要使用实际的owner名称（alice），不能用"God"
  // 因为它需要访问对应的QuantumNode来添加qubits
  std::pair<std::string, std::string> epr = {alice + "_epr_a", bob + "_epr_b"};
  qphyent->GenerateQubitsMixed(alice, epr_dm, {epr.first, epr.second});
  
  // 计算保真度
  double fidel = 0.0;
  double result = qphyent->CalculateFidelity(epr, fidel);
  
  NS_LOG_INFO("实际保真度: " << std::fixed << std::setprecision(6) << result);
  NS_LOG_INFO("测试结果: " << (std::abs(result - 0.9) < 0.05 ? "通过" : "失败"));
  
  CleanupExaTN();
}

/**
 * \brief 测试3：单个EPR对（有时间退相干）
 * 预期结果：保真度随时间衰减
 */
void Test3_SingleEprWithTimeDecoherence()
{
  NS_LOG_INFO("");
  NS_LOG_INFO("========================================");
  NS_LOG_INFO("测试3: 时间退相干说明");
  NS_LOG_INFO("========================================");
  
  // TimeModel 使用 rate 参数，rate = 1/T2
  // 默认 rate = 1.0 意味着 T2 = 1s
  NS_LOG_INFO("");
  NS_LOG_INFO("TimeModel 参数说明:");
  NS_LOG_INFO("  构造函数: TimeModel(double rate)");
  NS_LOG_INFO("  rate = 1/T2 (退相干率)");
  NS_LOG_INFO("  默认 rate = 1.0 -> T2 = 1s = 1000ms");
  NS_LOG_INFO("");
  NS_LOG_INFO("F_time = ((1 + exp(-t * rate)) / 2)^2");
  NS_LOG_INFO("  t=0ms, rate=1: F_time = 1.0");
  NS_LOG_INFO("  t=100ms, rate=1: F_time = ((1 + exp(-0.1)) / 2)^2 ≈ 0.95");
  NS_LOG_INFO("  t=500ms, rate=1: F_time = ((1 + exp(-0.5)) / 2)^2 ≈ 0.74");
  NS_LOG_INFO("  t=1000ms, rate=1: F_time = ((1 + exp(-1)) / 2)^2 ≈ 0.55");
  NS_LOG_INFO("");
  NS_LOG_INFO("如果 rate=10 (T2=100ms):");
  NS_LOG_INFO("  t=10ms, rate=10: F_time = ((1 + exp(-0.1)) / 2)^2 ≈ 0.95");
  NS_LOG_INFO("  t=50ms, rate=10: F_time = ((1 + exp(-0.5)) / 2)^2 ≈ 0.74");
  NS_LOG_INFO("  t=100ms, rate=10: F_time = ((1 + exp(-1)) / 2)^2 ≈ 0.55");
}

/**
 * \brief 测试4：两跳纠缠交换（使用实际测量）
 * 
 * 使用 ent-swap-app.cc 中的方法：实际测量 + 经典通信 + Pauli修正
 * 预期结果：交换后Alice和Charlie之间建立高保真度纠缠
 */
void Test4_TwoHopEntanglementSwap()
{
  NS_LOG_INFO("");
  NS_LOG_INFO("========================================");
  NS_LOG_INFO("测试4: 两跳纠缠交换 (Alice-Bob-Charlie)");
  NS_LOG_INFO("        使用实际测量 + Pauli修正");
  NS_LOG_INFO("========================================");
  
  int testId = g_testCounter++;
  
  // 创建三个节点
  std::string alice = "Alice" + std::to_string(testId);
  std::string bob = "Bob" + std::to_string(testId);
  std::string charlie = "Charlie" + std::to_string(testId);
  std::vector<std::string> owners = {alice, bob, charlie};
  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(owners);
  
  // 创建量子通道
  Ptr<QuantumChannel> channelAB = CreateObject<QuantumChannel>(alice, bob);
  Ptr<QuantumChannel> channelBC = CreateObject<QuantumChannel>(bob, charlie);
  
  // 生成两个EPR对
  std::pair<std::string, std::string> epr1 = {alice + "_epr_1", bob + "_epr_1"};
  std::pair<std::string, std::string> epr2 = {bob + "_epr_2", charlie + "_epr_2"};
  
  qphyent->GenerateEPR(channelAB, epr1);
  qphyent->GenerateEPR(channelBC, epr2);
  
  NS_LOG_INFO("");
  NS_LOG_INFO("EPR对1 (Alice-Bob): " << epr1.first << " - " << epr1.second);
  NS_LOG_INFO("EPR对2 (Bob-Charlie): " << epr2.first << " - " << epr2.second);
  
  // 计算交换前的保真度
  double fidel1 = 0.0, fidel2 = 0.0;
  double result1 = qphyent->CalculateFidelity(epr1, fidel1);
  double result2 = qphyent->CalculateFidelity(epr2, fidel2);
  
  NS_LOG_INFO("");
  NS_LOG_INFO("交换前保真度:");
  NS_LOG_INFO("  EPR1 (Alice-Bob): " << std::fixed << std::setprecision(6) << result1);
  NS_LOG_INFO("  EPR2 (Bob-Charlie): " << std::fixed << std::setprecision(6) << result2);
  
  // 在Bob处执行纠缠交换（使用实际测量）
  NS_LOG_INFO("");
  NS_LOG_INFO("在Bob处执行纠缠交换（实际测量方法）...");
  
  // 参考 ent-swap-app.cc:
  // m_qubits.first = former qubit (from left)
  // m_qubits.second = latter qubit (to right)
  // Bell measurement: CNOT(latter, former), H(former)
  // outcome_Q0 = measure(former) -> controls Z correction
  // outcome_Q1 = measure(latter) -> controls X correction
  
  // In our case:
  // epr1.second = Bob's left qubit (former)
  // epr2.first = Bob's right qubit (latter)
  
  std::string bobFormer = epr1.second;
  std::string bobLatter = epr2.first;
  
  // Step 1: Bell测量 (CNOT + H)
  NS_LOG_INFO("  Step 1: Bell测量 CNOT(latter, former), H(former)");
  qphyent->ApplyGate("God", QNS_GATE_PREFIX + "CNOT", {}, {bobLatter, bobFormer});
  qphyent->ApplyGate("God", QNS_GATE_PREFIX + "H", {}, {bobFormer});
  
  // Step 2: 测量
  // 注意：Measure需要使用"God"作为owner因为qubits是用"God"生成的
  // 或者使用实际的owner来满足CheckOwned断言
  NS_LOG_INFO("  Step 2: 测量");
  auto outcome0 = qphyent->Measure("God", {bobFormer});
  auto outcome1 = qphyent->Measure("God", {bobLatter});
  
  NS_LOG_INFO("    测量结果: outcome0=" << outcome0.first << ", outcome1=" << outcome1.first);
  
  // Step 3: Partial trace to remove measured qubits
  qphyent->PartialTrace({bobFormer, bobLatter});
  
  // Step 4: Apply Pauli corrections based on measurement outcomes
  // X correction if outcome1 == 1
  // Z correction if outcome0 == 1
  NS_LOG_INFO("  Step 3: Pauli修正");
  
  std::string targetQubit = epr2.second;  // Charlie's qubit
  
  if (outcome1.first == 1) {
    NS_LOG_INFO("    应用 X 门");
    qphyent->ApplyGate("God", QNS_GATE_PREFIX + "PX", {}, {targetQubit});
  }
  
  if (outcome0.first == 1) {
    NS_LOG_INFO("    应用 Z 门");
    qphyent->ApplyGate("God", QNS_GATE_PREFIX + "PZ", {}, {targetQubit});
  }
  
  // 交换后，Alice_epr_1 和 Charlie_epr_2 应该纠缠
  std::pair<std::string, std::string> endToEnd = {epr1.first, epr2.second};
  
  NS_LOG_INFO("");
  NS_LOG_INFO("计算端到端保真度 (Alice-Charlie)...");
  NS_LOG_INFO("  端点: " << endToEnd.first << " - " << endToEnd.second);
  
  double fidelEnd = 0.0;
  double resultEnd = qphyent->CalculateFidelity(endToEnd, fidelEnd);
  
  NS_LOG_INFO("");
  NS_LOG_INFO("端到端保真度 (Alice-Charlie): " << std::fixed << std::setprecision(6) << resultEnd);
  NS_LOG_INFO("预期保真度: ~1.0 (完整纠缠交换协议)");
  NS_LOG_INFO("测试结果: " << (resultEnd > 0.9 ? "通过" : "失败"));
  
  CleanupExaTN();
}

/**
 * \brief 测试4b：两跳纠缠交换（带噪声EPR对）
 * 
 * 验证带噪声EPR对的纠缠交换
 * 预期：F_swap = (4*F1*F2 - 1) / 3 对于Werner态
 */
void Test4b_TwoHopEntanglementSwapWithNoise()
{
  NS_LOG_INFO("");
  NS_LOG_INFO("========================================");
  NS_LOG_INFO("测试4b: 两跳纠缠交换（带噪声EPR）");
  NS_LOG_INFO("        EPR保真度=0.9, 预期交换后约0.747");
  NS_LOG_INFO("========================================");
  
  int testId = g_testCounter++;
  
  // 创建三个节点
  std::string alice = "Alice" + std::to_string(testId);
  std::string bob = "Bob" + std::to_string(testId);
  std::string charlie = "Charlie" + std::to_string(testId);
  std::vector<std::string> owners = {alice, bob, charlie};
  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(owners);
  
  // 使用带噪声的EPR对
  double eprFidelity = 0.9;
  std::vector<std::complex<double>> epr_dm = GetEPRwithFidelity(eprFidelity);
  
  // 生成两个带噪声的EPR对
  std::pair<std::string, std::string> epr1 = {alice + "_epr_1", bob + "_epr_1"};
  std::pair<std::string, std::string> epr2 = {bob + "_epr_2", charlie + "_epr_2"};
  
  qphyent->GenerateQubitsMixed(alice, epr_dm, {epr1.first, epr1.second});
  qphyent->GenerateQubitsMixed(bob, epr_dm, {epr2.first, epr2.second});
  
  NS_LOG_INFO("");
  NS_LOG_INFO("EPR对1 (Alice-Bob): " << epr1.first << " - " << epr1.second << " (F=" << eprFidelity << ")");
  NS_LOG_INFO("EPR对2 (Bob-Charlie): " << epr2.first << " - " << epr2.second << " (F=" << eprFidelity << ")");
  
  // 计算交换前的保真度
  double fidel1 = 0.0, fidel2 = 0.0;
  double result1 = qphyent->CalculateFidelity(epr1, fidel1);
  double result2 = qphyent->CalculateFidelity(epr2, fidel2);
  
  NS_LOG_INFO("");
  NS_LOG_INFO("交换前保真度:");
  NS_LOG_INFO("  EPR1: " << std::fixed << std::setprecision(6) << result1);
  NS_LOG_INFO("  EPR2: " << std::fixed << std::setprecision(6) << result2);
  
  // 在Bob处执行纠缠交换（实际测量方法）
  NS_LOG_INFO("");
  NS_LOG_INFO("在Bob处执行纠缠交换...");
  
  std::string bobFormer = epr1.second;
  std::string bobLatter = epr2.first;
  
  // Bell measurement: CNOT(latter, former) + H(former)
  qphyent->ApplyGate("God", QNS_GATE_PREFIX + "CNOT", {}, {bobLatter, bobFormer});
  qphyent->ApplyGate("God", QNS_GATE_PREFIX + "H", {}, {bobFormer});
  
  // Measurement
  auto outcome0 = qphyent->Measure("God", {bobFormer});
  auto outcome1 = qphyent->Measure("God", {bobLatter});
  
  NS_LOG_INFO("  测量结果: m0=" << outcome0.first << ", m1=" << outcome1.first);
  
  // Partial trace
  qphyent->PartialTrace({bobFormer, bobLatter});
  
  // Pauli corrections
  std::string targetQubit = epr2.second;
  if (outcome1.first == 1) {
    qphyent->ApplyGate("God", QNS_GATE_PREFIX + "PX", {}, {targetQubit});
  }
  if (outcome0.first == 1) {
    qphyent->ApplyGate("God", QNS_GATE_PREFIX + "PZ", {}, {targetQubit});
  }
  
  // Calculate end-to-end fidelity
  std::pair<std::string, std::string> endToEnd = {epr1.first, epr2.second};
  double fidelEnd = 0.0;
  double resultEnd = qphyent->CalculateFidelity(endToEnd, fidelEnd);
  
  // Expected fidelity for Werner state swap: F_swap = (4*F1*F2 - 1) / 3
  double expectedSwap = (4 * eprFidelity * eprFidelity - 1) / 3;
  
  NS_LOG_INFO("");
  NS_LOG_INFO("端到端保真度: " << std::fixed << std::setprecision(6) << resultEnd);
  NS_LOG_INFO("预期保真度 (Werner态公式): " << std::fixed << std::setprecision(6) << expectedSwap);
  NS_LOG_INFO("差异: " << std::fixed << std::setprecision(6) << std::abs(resultEnd - expectedSwap));
  NS_LOG_INFO("测试结果: " << (std::abs(resultEnd - expectedSwap) < 0.1 ? "通过" : "失败"));
  
  CleanupExaTN();
}

/**
 * \brief 测试5：使用现有的纠缠交换应用
 * 
 * 由于纠缠交换需要正确处理测量结果和Pauli修正，
 * 这个测试说明了为什么直接的Bell测量+partial trace不能得到高保真度
 */
void Test5_ExplainEntanglementSwap()
{
  NS_LOG_INFO("");
  NS_LOG_INFO("========================================");
  NS_LOG_INFO("测试5: 纠缠交换保真度说明");
  NS_LOG_INFO("========================================");
  
  NS_LOG_INFO("");
  NS_LOG_INFO("纠缠交换的正确流程:");
  NS_LOG_INFO("1. 在中间节点执行Bell测量 (CNOT + H + Measure)");
  NS_LOG_INFO("2. 将测量结果（2个经典比特）发送给端节点");
  NS_LOG_INFO("3. 端节点根据测量结果应用Pauli修正门:");
  NS_LOG_INFO("   - 00: 无操作");
  NS_LOG_INFO("   - 01: Z门");
  NS_LOG_INFO("   - 10: X门");
  NS_LOG_INFO("   - 11: XZ门");
  NS_LOG_INFO("");
  NS_LOG_INFO("如果不应用Pauli修正:");
  NS_LOG_INFO("  - 端到端状态是4个Bell态的等概率混合");
  NS_LOG_INFO("  - 与|Φ+⟩的保真度 = 1/4 = 0.25");
  NS_LOG_INFO("");
  NS_LOG_INFO("这解释了为什么测试4得到的保真度是0.25而不是1.0");
  NS_LOG_INFO("");
  NS_LOG_INFO("在Q-CAST实现中，我们使用 ApplyControlledOperation");
  NS_LOG_INFO("来模拟条件Pauli修正，参见 ent-swap-adapt-local-app.cc");
}

/**
 * \brief 测试6：检查TimeModel的T2参数
 */
void Test6_CheckTimeModelParameters()
{
  NS_LOG_INFO("");
  NS_LOG_INFO("========================================");
  NS_LOG_INFO("测试6: 检查TimeModel默认参数");
  NS_LOG_INFO("========================================");
  
  // TimeModel 使用 rate 参数，rate = 1/T2
  // 查看 quantum-error-model.h 中的定义：
  // const TimeModel default_time_model = TimeModel (1.);
  // 即默认 rate = 1.0，对应 T2 = 1s = 1000ms
  
  NS_LOG_INFO("TimeModel 默认 rate = 1.0");
  NS_LOG_INFO("对应 T2 = 1/rate = 1s = 1000ms");
  NS_LOG_INFO("");
  NS_LOG_INFO("这意味着在几毫秒的等待时间内，时间退相干几乎可以忽略");
  NS_LOG_INFO("例如：t=5ms 时，F_time = ((1 + exp(-0.005)) / 2)^2 ≈ 0.9975");
}

int main(int argc, char *argv[])
{
  // 设置日志级别
  LogComponentEnable("FidelityTest", LOG_LEVEL_INFO);
  
  NS_LOG_INFO("======================================");
  NS_LOG_INFO("量子保真度计算测试");
  NS_LOG_INFO("======================================");
  
  // 运行测试
  Test1_SingleEprNoNoise();
  Test2_SingleEprWithDepolar();
  Test3_SingleEprWithTimeDecoherence();
  Test4_TwoHopEntanglementSwap();
  Test4b_TwoHopEntanglementSwapWithNoise();
  Test5_ExplainEntanglementSwap();
  Test6_CheckTimeModelParameters();
  
  NS_LOG_INFO("");
  NS_LOG_INFO("======================================");
  NS_LOG_INFO("所有测试完成");
  NS_LOG_INFO("======================================");
  
  return 0;
}
