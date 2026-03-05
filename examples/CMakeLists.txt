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

build_lib_example(
    NAME swap-fidelity-test
    SOURCE_FILES swap-fidelity-test.cc
    LIBRARIES_TO_LINK ${libquantum}
)

# quantum network layer

build_lib_example(
    NAME quantum-network-layer-example
    SOURCE_FILES quantum-network-layer-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

# Q-CAST routing

build_lib_example(
    NAME q-cast-example
    SOURCE_FILES q-cast-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

build_lib_example(
    NAME q-cast-random-topology-example
    SOURCE_FILES q-cast-random-topology-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

build_lib_example(
    NAME q-cast-dynamic-delay-example
    SOURCE_FILES q-cast-dynamic-delay-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

# signaling delay simulation

build_lib_example(
    NAME signaling-delay-example
    SOURCE_FILES signaling-delay-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

# routing comparison

build_lib_example(
    NAME routing-comparison-example
    SOURCE_FILES routing-comparison-example.cc
    LIBRARIES_TO_LINK ${libquantum}
)

build_lib_example(
    NAME routing-detailed-analysis
    SOURCE_FILES routing-detailed-analysis.cc
    LIBRARIES_TO_LINK ${libquantum}
)

build_lib_example(
    NAME dijkstra-vs-qcast
    SOURCE_FILES dijkstra-vs-qcast.cc
    LIBRARIES_TO_LINK ${libquantum}
)