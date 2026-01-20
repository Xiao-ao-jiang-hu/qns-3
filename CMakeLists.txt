check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/quantum-examples-test-suite.cc
        )
endif()

set(ExaTN_DIR ~/.exatn) # Your exatn install path
find_package(ExaTN REQUIRED)

# include_directories(SYSTEM ${EXATN_INCLUDE_DIRS} ${EXATN_INCLUDE_ROOT})

build_lib(
    LIBNAME quantum
    SOURCE_FILES
                model/quantum-basis.cc
                model/quantum-network-simulator.cc
                model/quantum-operation.cc
                model/quantum-error-model.cc
                model/quantum-phy-entity.cc
                model/qubit.cc
                model/quantum-memory.cc
                model/quantum-node.cc
                model/quantum-channel.cc
                model/quantum-packet.cc
                model/quantum-route-types.cc
                model/quantum-metric.cc
                model/quantum-network-layer.cc
                model/quantum-resource-manager.cc
                model/quantum-forwarding-engine.cc
                model/quantum-routing-protocol.cc
                model/qcast/expected-throughput-metric.cc
                model/qcast/qcast-route-types.cc
                model/qcast/qcast-routing-protocol.cc
                model/qcast/qcast-forwarding-engine.cc

                model/distribute-epr-protocol.cc
                model/telep-app.cc
                model/telep-adapt-app.cc
                model/telep-lin-adapt-app.cc
                model/ent-swap-app.cc
                model/ent-swap-adapt-app.cc
                model/ent-swap-adapt-local-app.cc
                model/distill-app.cc
                model/distill-nested-app.cc
                model/distill-nested-adapt-app.cc
                
                helper/quantum-basis-helper.cc
                helper/quantum-net-stack-helper.cc
                helper/distribute-epr-helper.cc
                helper/telep-helper.cc
                helper/telep-adapt-helper.cc
                helper/telep-lin-adapt-helper.cc
                helper/ent-swap-helper.cc
                helper/ent-swap-adapt-helper.cc
                helper/ent-swap-adapt-local-helper.cc
                helper/distill-helper.cc
                helper/distill-nested-helper.cc
                helper/distill-nested-adapt-helper.cc
    HEADER_FILES
                model/quantum-basis.h
                model/quantum-network-simulator.h
                model/quantum-operation.h
                model/quantum-error-model.h
                model/quantum-phy-entity.h
                model/qubit.h
                model/quantum-memory.h
                model/quantum-node.h
                model/quantum-channel.h
                model/quantum-packet.h
                model/quantum-route-types.h
                model/quantum-metric.h
                model/quantum-network-layer.h
                model/quantum-resource-manager.h
                model/quantum-forwarding-engine.h
                model/quantum-routing-protocol.h
                model/qcast/expected-throughput-metric.h
                model/qcast/qcast-route-types.h
                model/qcast/qcast-routing-protocol.h
                model/qcast/qcast-forwarding-engine.h

                model/distribute-epr-protocol.h
                model/telep-app.h
                model/telep-adapt-app.h
                model/telep-lin-adapt-app.h
                model/ent-swap-app.h
                model/ent-swap-adapt-app.h
                model/ent-swap-adapt-local-app.h
                model/distill-app.h
                model/distill-nested-app.h
                model/distill-nested-adapt-app.h
                
                helper/quantum-basis-helper.h
                helper/quantum-net-stack-helper.h
                helper/distribute-epr-helper.h
                helper/telep-helper.h
                helper/telep-adapt-helper.h
                helper/telep-lin-adapt-helper.h
                helper/ent-swap-helper.h
                helper/ent-swap-adapt-helper.h
                helper/ent-swap-adapt-local-helper.h
                helper/distill-helper.h
                helper/distill-nested-helper.h
                helper/distill-nested-adapt-helper.h


    LIBRARIES_TO_LINK ${libcore}
                      ${libpoint-to-point}
                      ${libcsma}
                      ${libinternet}
                      ${libapplications}
		      exatn::exatn
    TEST_SOURCES test/quantum-test-suite.cc
                 ${examples_as_tests_sources}
)

target_include_directories(libquantum-obj PUBLIC ${EXATN_INCLUDE_DIRS} ${EXATN_INCLUDE_ROOT})

