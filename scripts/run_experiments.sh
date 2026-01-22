#!/bin/bash
#
# 四节点量子链协议参数扫描实验脚本
#

set -e

NS3_DIR="/home/wst/ns-3/ns-3.42"

# 参数数组
LINK_FIDELITIES=(0.85 0.90 0.95 0.99)
T2_VALUES=(10 50 100 500)
DELAYS=(1 5 10 50)

# 设置环境
export LIBRARY_PATH=/usr/lib/gcc/x86_64-linux-gnu/13:$LIBRARY_PATH
cd "$NS3_DIR"

# 编译
echo "编译量子模块..."
./ns3 build quantum 2>&1 | tail -3

echo ""
echo "========================================"
echo "  运行退相干测试"
echo "========================================"
./ns3 run decoherence-test 2>&1

echo ""
echo "========================================"
echo "  四节点链协议参数扫描"
echo "========================================"
echo ""

# 打印表头
printf "%-10s %-8s %-10s %-10s %-15s\n" "LinkF" "T2(ms)" "Delay(ms)" "Time(ms)" "Fidelity"
printf "%-10s %-8s %-10s %-10s %-15s\n" "------" "------" "---------" "--------" "--------"

# 参数扫描
for link_f in "${LINK_FIDELITIES[@]}"; do
    for t2 in "${T2_VALUES[@]}"; do
        for delay in "${DELAYS[@]}"; do
            total_time=$((delay * 3))
            
            result=$(./ns3 run "four-node-chain-test --linkFidelity=${link_f} --T2=${t2} --delay=${delay}" 2>&1 | \
                grep "Theoretical final fidelity" | awk '{print $NF}')
            
            printf "%-10.2f %-8d %-10d %-10d %-15s\n" "$link_f" "$t2" "$delay" "$total_time" "$result"
        done
    done
done

echo ""
echo "实验完成!"
