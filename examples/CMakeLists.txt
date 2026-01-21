# teleportation

build_lib_example(
    NAME telep-lin-example
    SOURCE_FILES telep-lin-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

build_lib_example(
    NAME telep-lin-adapt-example
    SOURCE_FILES telep-lin-adapt-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

# entanglement swapping

build_lib_example(
    NAME ent-swap-example
    SOURCE_FILES ent-swap-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

build_lib_example(
    NAME ent-swap-adapt-example
    SOURCE_FILES ent-swap-adapt-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

build_lib_example(
    NAME ent-swap-adapt-local-example
    SOURCE_FILES ent-swap-adapt-local-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

# distillation

build_lib_example(
    NAME distill-nested-example
    SOURCE_FILES distill-nested-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

build_lib_example(
    NAME distill-nested-adapt-example
    SOURCE_FILES distill-nested-adapt-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

# quantum network layer
build_lib_example(
    NAME quantum-network-layer-example
    SOURCE_FILES quantum-network-layer-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

# Q-CAST performance test
build_lib_example(
    NAME qcast-performance-test
    SOURCE_FILES qcast-performance-test.cc
    LIBRARIES_TO_LINK ${libquantum}
)

# Q-CAST protocol example
build_lib_example(
    NAME qcast-protocol-example
    SOURCE_FILES qcast-protocol-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

# Q-CAST scaling test
build_lib_example(
    NAME qcast-scaling-test
    SOURCE_FILES qcast-scaling-test-simple.cc
    LIBRARIES_TO_LINK ${libquantum}
)

# Quantum routing benchmark
build_lib_example(
    NAME quantum-routing-benchmark
    SOURCE_FILES quantum-routing-benchmark.cc
    LIBRARIES_TO_LINK ${libquantum}
)

# Fidelity test
build_lib_example(
    NAME fidelity-test
    SOURCE_FILES fidelity-test.cc
    LIBRARIES_TO_LINK ${libquantum}
)

