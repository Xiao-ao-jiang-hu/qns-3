#!/usr/bin/env python3
"""
量子路由测试配置生成器

生成CSV格式的测试配置，用于quantum-routing-benchmark程序。
"""

import csv
import argparse
import itertools

def generate_default_configs(output_file):
    """生成默认测试配置"""
    
    configs = []
    
    # 定义测试参数
    routing_protocols = ["QCAST"]
    topology_types = ["chain", "grid", "random"]
    node_counts = [5, 10, 20, 30, 50]
    fidelities = [0.7, 0.8, 0.9, 0.95, 0.99]
    request_counts = [5, 10, 20, 50, 100]
    
    # 计数器
    test_id = 0
    
    # 套件1：不同拓扑规模
    print("生成套件1：不同拓扑规模...")
    for protocol in routing_protocols:
        for topology in topology_types:
            for nodes in node_counts:
                if topology == "grid" and nodes > 25:
                    continue  # 网格拓扑节点数不宜过大
                
                test_id += 1
                config = {
                    "test_name": f"suite1_{protocol.lower()}_{topology}_{nodes}nodes",
                    "topology_type": topology,
                    "num_nodes": nodes,
                    "link_fidelity": 0.95,
                    "protocol_name": protocol,
                    "num_requests": 20,
                    "min_fidelity": 0.8,
                    "max_delay": 1.0,
                    "num_qubits": 2,
                    "duration": 30.0,
                    "simulation_duration": 15.0,
                    "request_interval_min": 0.1,
                    "request_interval_max": 5.0
                }
                configs.append(config)
    
    # 套件2：不同链路质量
    print("生成套件2：不同链路质量...")
    for protocol in routing_protocols:
        for fidelity in fidelities:
            test_id += 1
            config = {
                "test_name": f"suite2_{protocol.lower()}_grid_9nodes_fidelity{int(fidelity*100)}",
                "topology_type": "grid",
                "num_nodes": 9,
                "link_fidelity": fidelity,
                "protocol_name": protocol,
                "num_requests": 20,
                "min_fidelity": fidelity * 0.8,  # 适当的最小保真度要求
                "max_delay": 1.0,
                "num_qubits": 2,
                "duration": 30.0,
                "simulation_duration": 15.0,
                "request_interval_min": 0.1,
                "request_interval_max": 5.0
            }
            configs.append(config)
    
    # 套件3：不同请求负载
    print("生成套件3：不同请求负载...")
    for protocol in routing_protocols:
        for requests in request_counts:
            test_id += 1
            config = {
                "test_name": f"suite3_{protocol.lower()}_grid_9nodes_{requests}req",
                "topology_type": "grid",
                "num_nodes": 9,
                "link_fidelity": 0.95,
                "protocol_name": protocol,
                "num_requests": requests,
                "min_fidelity": 0.8,
                "max_delay": 1.0,
                "num_qubits": 2,
                "duration": 30.0,
                "simulation_duration": max(15.0, requests * 0.5),  # 根据请求数调整模拟时间
                "request_interval_min": 0.1,
                "request_interval_max": 5.0
            }
            configs.append(config)
    
    # 套件4：极端情况测试
    print("生成套件4：极端情况测试...")
    extreme_tests = [
        ("tiny_network", "chain", 3, 0.99, 10),
        ("large_network", "random", 100, 0.9, 50),
        ("low_fidelity", "grid", 16, 0.6, 20),
        ("high_fidelity", "grid", 16, 0.999, 20),
        ("many_requests", "chain", 10, 0.95, 200),
    ]
    
    for test_desc in extreme_tests:
        name, topology, nodes, fidelity, requests = test_desc
        test_id += 1
        config = {
            "test_name": f"suite4_{name}",
            "topology_type": topology,
            "num_nodes": nodes,
            "link_fidelity": fidelity,
            "protocol_name": "QCAST",
            "num_requests": requests,
            "min_fidelity": fidelity * 0.8 if fidelity > 0.7 else 0.5,
            "max_delay": 1.0,
            "num_qubits": 2,
            "duration": 30.0,
            "simulation_duration": max(20.0, requests * 0.3),
            "request_interval_min": 0.05 if requests > 100 else 0.1,
            "request_interval_max": 2.0 if requests > 100 else 5.0
        }
        configs.append(config)
    
    # 写入CSV文件
    fieldnames = [
        "test_name", "topology_type", "num_nodes", "link_fidelity",
        "protocol_name", "num_requests", "min_fidelity", "max_delay",
        "num_qubits", "duration", "simulation_duration",
        "request_interval_min", "request_interval_max"
    ]
    
    with open(output_file, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()
        for config in configs:
            writer.writerow(config)
    
    print(f"已生成 {len(configs)} 个测试配置到 {output_file}")
    
    # 打印摘要
    print("\n配置摘要:")
    print(f"  总测试数: {len(configs)}")
    print(f"  路由协议: {', '.join(routing_protocols)}")
    print(f"  拓扑类型: {', '.join(topology_types)}")
    print(f"  节点数量范围: {min(node_counts)} - {max(node_counts)}")
    print(f"  链路保真度范围: {min(fidelities)} - {max(fidelities)}")
    print(f"  请求数量范围: {min(request_counts)} - {max(request_counts)}")
    
    return configs

def generate_custom_configs(output_file, params):
    """生成自定义测试配置"""
    
    configs = []
    test_id = 0
    
    # 解析参数
    protocols = params.get("protocols", ["QCAST"])
    topologies = params.get("topologies", ["chain", "grid", "random"])
    node_range = params.get("node_range", (5, 30))
    fidelity_range = params.get("fidelity_range", (0.7, 0.99))
    request_range = params.get("request_range", (5, 50))
    
    # 生成所有组合
    for protocol in protocols:
        for topology in topologies:
            # 为每种拓扑生成几个不同规模的测试
            if topology == "chain":
                node_counts = [node_range[0], 
                              (node_range[0] + node_range[1]) // 2, 
                              node_range[1]]
            elif topology == "grid":
                # 网格拓扑选择完全平方数
                node_counts = [4, 9, 16, 25]
                node_counts = [n for n in node_counts 
                              if node_range[0] <= n <= node_range[1]]
            else:  # random
                node_counts = [node_range[0], 
                              (node_range[0] + node_range[1]) // 3 * 2, 
                              node_range[1]]
            
            for nodes in node_counts:
                for fidelity in [fidelity_range[0], 
                                (fidelity_range[0] + fidelity_range[1]) / 2, 
                                fidelity_range[1]]:
                    for requests in [request_range[0], 
                                    (request_range[0] + request_range[1]) // 2, 
                                    request_range[1]]:
                        test_id += 1
                        config = {
                            "test_name": f"custom_{test_id:03d}_{protocol.lower()}_{topology}_{nodes}nodes",
                            "topology_type": topology,
                            "num_nodes": nodes,
                            "link_fidelity": round(fidelity, 3),
                            "protocol_name": protocol,
                            "num_requests": requests,
                            "min_fidelity": round(fidelity * 0.8, 3),
                            "max_delay": 1.0,
                            "num_qubits": 2,
                            "duration": 30.0,
                            "simulation_duration": max(10.0, requests * 0.4),
                            "request_interval_min": 0.1,
                            "request_interval_max": 5.0
                        }
                        configs.append(config)
    
    # 写入CSV文件
    fieldnames = [
        "test_name", "topology_type", "num_nodes", "link_fidelity",
        "protocol_name", "num_requests", "min_fidelity", "max_delay",
        "num_qubits", "duration", "simulation_duration",
        "request_interval_min", "request_interval_max"
    ]
    
    with open(output_file, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()
        for config in configs:
            writer.writerow(config)
    
    print(f"已生成 {len(configs)} 个自定义测试配置到 {output_file}")
    return configs

def main():
    parser = argparse.ArgumentParser(
        description="量子路由测试配置生成器",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s --output test_configs.csv
  %(prog)s --output custom_configs.csv --protocols QCAST --topologies chain,grid --node-range 5 20
        """
    )
    
    parser.add_argument("--output", default="test_configs.csv",
                       help="输出CSV文件路径 (默认: test_configs.csv)")
    parser.add_argument("--default", action="store_true",
                       help="生成默认测试套件 (默认)")
    parser.add_argument("--custom", action="store_true",
                       help="生成自定义测试套件")
    parser.add_argument("--protocols", default="QCAST",
                       help="路由协议列表，逗号分隔 (默认: QCAST)")
    parser.add_argument("--topologies", default="chain,grid,random",
                       help="拓扑类型列表，逗号分隔 (默认: chain,grid,random)")
    parser.add_argument("--node-range", type=int, nargs=2, default=[5, 30],
                       help="节点数量范围 (默认: 5 30)")
    parser.add_argument("--fidelity-range", type=float, nargs=2, default=[0.7, 0.99],
                       help="链路保真度范围 (默认: 0.7 0.99)")
    parser.add_argument("--request-range", type=int, nargs=2, default=[5, 50],
                       help="请求数量范围 (默认: 5 50)")
    
    args = parser.parse_args()
    
    if args.custom:
        # 解析参数
        params = {
            "protocols": args.protocols.split(','),
            "topologies": args.topologies.split(','),
            "node_range": tuple(args.node_range),
            "fidelity_range": tuple(args.fidelity_range),
            "request_range": tuple(args.request_range)
        }
        
        print("生成自定义测试配置...")
        print(f"  协议: {params['protocols']}")
        print(f"  拓扑: {params['topologies']}")
        print(f"  节点范围: {params['node_range'][0]} - {params['node_range'][1]}")
        print(f"  保真度范围: {params['fidelity_range'][0]} - {params['fidelity_range'][1]}")
        print(f"  请求范围: {params['request_range'][0]} - {params['request_range'][1]}")
        
        generate_custom_configs(args.output, params)
    else:
        print("生成默认测试套件...")
        generate_default_configs(args.output)
    
    print("\n使用方法:")
    print(f"  1. 运行基准测试: ./ns3 run 'quantum-routing-benchmark --config={args.output}'")
    print(f"  2. 查看结果: cat quantum_routing_benchmark.csv")

if __name__ == "__main__":
    main()