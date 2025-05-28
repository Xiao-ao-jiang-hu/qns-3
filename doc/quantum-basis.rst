Quantum Module Documentation
----------------------------

.. include:: replace.txt
.. highlight:: cpp

.. heading hierarchy:
   ------------- Chapter
   ************* Section (#.#)
   ============= Subsection (#.#.#)
   ############# Paragraph (no number)


This project is based on the paper *"Control Flow Adaptation: An Efficient Simulation Method For Noisy Quantum Networks"*, proposed by a team from Tsinghua University. The paper introduces a novel quantum network simulation method called Control Flow Adaptation (CFA), designed to enhance the efficiency of classical tensor network simulations. By analyzing and adapting the control flow structures of quantum network protocols, this method enables more accurate and scalable simulations.

Key contributions of the research include:
1. **Control Flow Adaptation (CFA):** A technique that compresses and adapts classical control flow information in quantum protocols, significantly reducing the complexity of tensor networks and supporting the simulation of larger-scale quantum network protocols.
2. **qns-3 Simulator:** A quantum network simulation module developed for the widely-used classical network simulator ns-3. By integrating the CFA technique, qns-3 lowers the barriers for researchers and engineers to explore quantum networks.

The primary goal of this project is to provide a flexible and scalable platform for testing and validating quantum network protocols on classical computers, addressing the current limitations of scarce quantum hardware resources.

Key features of the platform include:
- A tensor-network-based backend enhanced by the CFA technique, optimizing the simulation of noisy quantum network protocols.
- APIs that seamlessly integrate quantum protocols with classical network components, making it accessible to traditional network researchers.
- Open-source code and an extensible framework for further development and exploration.

This project represents a significant step forward in quantum network research, particularly in optimizing simulation efficiency and fostering cross-disciplinary collaboration.



Model Description
*****************

The source code for the new module lives in the directory ``contrib/quantum-basis``.

This is a module for quantum network simulations which
provides a series of typical abstractions and functionalities.
The top (i.e. application) layer is open for users
to design and track all kinds of customized quantum network protocols.

Design
======

The design goal was to make the ns-3 Quantum API simple and user friendly
in order to better facilitate user research and experimentation.

The module follows the layered architecture of the classical network stack,
and the discrete-event simulation framework of ns-3,

Some components inherit from their classical counterparts in |ns3|,
such as ``Node`` and ``Application``.
Some are only named after their classical counterparts,
such as ``QuantumErrorModel`` and ``QuantumPhyEntity``.
Others are newly designed, such as ``QuantumOperation`` and ``QuantumMemory``.

Key features include:

* **Control Flow Adaptation:** Implements CFA to compress classical control flow, reducing tensor network contraction complexity and avoiding the need for Monte Carlo simulations.

* **Integration with ns-3:** Extends the ns-3 discrete-event simulator with quantum modules to simulate hybrid quantum-classical networks.

* **Tensor Network Backend:** Utilizes tensor networks for simulating quantum operations, measurements, and noise with reduced computational overhead.

* **Support for Quantum Protocols:** Includes implementations of protocols such as entanglement swapping and nested entanglement distillation.

* **Efficient Noise Modeling:** Supports a range of noise models, including loss error, physical operation noise, and time-relevant noise.


For more details, please refer to the fifth section of our paper.

Benchmarking
============

The qns-3 simulator was benchmarked against other quantum network simulators, such as NetSquid, for its performance in simulating chained entanglement swapping and nested entanglement distillation protocols. The results demonstrate the efficiency and scalability of the CFA technique.

1. **Chained Entanglement Swapping:**
   - **Runtime:** The use of CFA reduced simulation runtime from exponential to linear scaling with the number of nodes.
   - **Memory Usage:** Memory requirements were significantly lowered compared to naive implementations.
   - **Comparison with NetSquid:** qns-3 achieved shorter runtime while providing exact results without relying on Monte Carlo methods.

2. **Nested Entanglement Distillation:**
   - **Runtime:** CFA enabled linear scaling with the number of entanglement pairs, significantly outperforming traditional methods.
   - **Memory Usage:** Tensor contraction complexity was reduced, enabling efficient simulation of larger-scale protocols.

The experiments validate the advantages of qns-3 in both time and space efficiency, making it a highly scalable platform for quantum network research.

Scope and Limitations
=====================

In its current state, the quantum network stack is only equipped with L2 EPR distribution and distillation protocols, as well as L5 applications. The L3 routing protocols and L4 transport protocols are not simulated yet.

As for now, the module is only implemented in the C++ language. Python bindings are not available yet.

References
==========

1. Huiping Lin, Ruixuan Deng, Chris Z. Yao, Zhengfeng Ji, Mingsheng Ying, *"Control Flow Adaptation: An Efficient Simulation Method For Noisy Quantum Networks,"* 2024. [arXiv:2412.08956](https://arxiv.org/abs/2412.08956)



Usage
*****

.. This section is principally concerned with the usage of your model, using
.. the public API.  Focus first on most common usage patterns, then go
.. into more advanced topics.

.. Building New Module
.. ===================

.. Include this subsection only if there are special build instructions or
.. platform limitations.

Helpers
=======

To have a node run quantum applications, the easiest way would be
to use the QuantumNetStackHelper for L2 functionalities,
along with the QuantumApplicationHelper for some specific L5 protocols.

For instance:

::

   QuantumNetStackHelper stack;
   stack.Install (nodes);

   DistillHelper dstHelper (qphyent, true, qconn);
   dstHelper.SetAttribute ("Qubits", PairValue<StringValue, StringValue> ({"Bob0", "Bob1"}));
   ApplicationContainer dstApp = dstHelper.Install (bob);


The example scripts inside ``contrib/quantum/examples/`` demonstrate the use of Quantum based nodes
in different scenarios. The helper source can be found inside ``contrib/quantum/helper/``


Attributes
==========

All quantum applications and protocols are configured through attributes.
Basically, the attributes can be divided into three categories, for:
* identification, such as ``Checker`` and ``PNode``,
* communication, such as the ``QChannel`` and ``EPR``,
* operation, such as ``QPhyEntity`` and ``Qubit``.

Output
======

.. What kind of data does the model generate?  What are the key trace
.. sources?   What kind of logging output can be enabled?

The quantum network stack generates a series of trace sources 
to track the quantum network protocols.

At the logging level ``NS_LOG_INFO``, users can track the moment and description of
the local operation events as well as the communication events.
Users can also peek into the quantum memory to check the density matrix of certain qubits
by scheduling a PeekDM() event.

At the logging level ``NS_LOG_LOGIC``, users can track more detailed information,
such as the tensor network size, the error appliance events, 
and the addresses and ports in a classical connection.

.. Advanced Usage
.. ==============

.. Go into further details (such as using the API outside of the helpers)
.. in additional sections, as needed.

Examples
========

The following examples have been written, which can be found in ``examples/``:

* distill-nested-example.cc: Implementation of nested quantum distillations in order to simulate a realistic process of generating a higher fidelity EPR pair.
        
* distill-nested-adapt-example.cc: Implementation of nested quantum distillations with control flow adaption in order to optimise the simulation process.
        
* telep-lin-example.cc: Implementation of a chain of consecutive quantum teleportations, each time transferring the original qubit to a new node.
    
* telep-lin-adapt-example.cc: Implementation of a chain of consecutive quantum teleportations with control flow adaption in order to optimise the simulation process.

* ent-swap-example.cc: Implementation of the chained entanglement swapping protocol used for establishing long-distance entanglement between two nodes.

* ent-swap-adapt-example.cc: Implementation of the chained entanglement swapping protocol with control flow adaption in order to optimise the simulation process.

* ent-swap-adapt-local-example.cc: Implementation of the chained entanglement swapping protocol with another possible control flow adaption in order to further optimise the simulation process, getting around long distance operations.

Troubleshooting
===============

Please check the convention described in the ``\note`` line
in the header file when calling a function.


How to simulate a **new** quantum network protocol
==================================================

::

   Whenever you create a new file, remember to declare it in ``./contrib/quantum/CMakelists.txt`` for |ns-3| to recognize it.

#. Start by creating a pair of header and source files under the ``./contrib/quantum/model/`` directory.
Specify what the roles of nodes could be in the new protocol, and define a new class for each role to depict their behaviors.

#. Create a helper class under the ``./contrib/quantum/helper/`` directory to install the new protocol on nodes.

#. Formalize the simulation setup in a new script named ``./contrib/quantum/examples/<example_name>.cc``. This script should include the following steps. Let us show them with the example lines from ``./contrib/quantum/examples/``.

   1. Declare a node list and create a quantum physical entity. Notice they are fully connected in the current specification of qns-3.
      ::

         std::vector<std::string> owners = {"God", "Alice", "Bob", "Charlie"};
         Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (owners);

   2. Set error models.

      * Set a time relevant error model for a qubit, specified by the name of the qubit. It is applied before a gate is applied to the qubit, unless overwritten by the model below.
         ::

            Ptr<QuantumErrorModel> pmodel = CreateObject<TimeModel> (rate);
            qphyent->SetErrorModel (pmodel, "SomeQubitOwnedByAlice");
   
      * Set a time relevant error model, specified by the dephasing rate, for a quantum node. This is applied to each qubit generated by the node before a gate is applied to it, unless overwritten by the model above.
         ::

            alice->SetTimeModel (rate);
   
      * Set a dephasing error model, specified by the dephasing rate, (notice that the gate duration is set in ``./contrib/quantum/model/quantum-basis.h``,) for a kind of quantum gate, specified by the name of the gate, of a quantum node. It is applied after the gate is applied by the node.
         ::

            alice->SetDephaseModel (QNS_GATE_PREFIX + "CNOT", rate);
   
      * Set a depolarizing error model, specified by the fidelity, for a quantum connection (also called the quantum channel, the quantum equivalent of a classical channel, not the CPTP map). It is applied to the qubit distributed through the connection.
         ::

            Ptr<QuantumChannel> qconn = CreateObject<QuantumChannel> (alice, bob);
            qconn->SetDepolarModel (fidelity, qphyent);

   3. Configure the classical connection and install the classical network stack according to the standards of |ns-3|.
      ::

         CsmaHelper csmaHelper;
         csmaHelper.SetChannelAttribute ("DataRate", DataRateValue (DataRate ("1000kbps")));
         csmaHelper.SetChannelAttribute ("Delay",
                                          TimeValue (MilliSeconds (CLASSICAL_DELAY)));
         NetDeviceContainer devices = csmaHelper.Install (nodes); 

         InternetStackHelper stack;
         stack.Install (nodes);
         Ipv6AddressHelper address;
         address.SetBase ("2001:1::", Ipv6Prefix (64));
         Ipv6InterfaceContainer interfaces = address.Assign (devices); 

         unsigned rank = 0;
         for (const std::string &owner : owners)
            {
               qphyent->SetOwnerAddress (owner, interfaces.GetAddress (rank, 1));
               qphyent->SetOwnerRank (owner, rank);
               ++rank;
            }

   4. Install the quantum network stack.
      ::

         QuantumNetStackHelper qstack;
         qstack.Install (nodes);

   5. Configure the attributes (concerning both quantum information and classical information) of the quantum applications needed for your new protocol, and install them on the nodes.
      ::

         Ptr<Qubit> input = CreateObject<Qubit> (std::vector<std::complex<double>>{
               {sqrt (5. / 7.), 0.0},
               {0.0, sqrt (2. / 7.)}});
         TelepAdaptHelper helper (qphyent, qconn);
         helper.SetAttribute ("Qubits", PairValue<StringValue, StringValue> ({"Alice0", "Alice1"}));
         helper.SetAttribute ("Qubit", StringValue ("Bob0"));
         helper.SetAttribute ("Input", PointerValue (input));      
         ApplicationContainer srcApp = helper.Install (alice);
         srcApp.Start (Seconds (2.));
         srcApp.Stop (Seconds (20.));


   6. Run the simulation by |ns-3| simulator.
      ::

         Simulator::Stop (Seconds (20.));
         Simulator::Run ();
         Simulator::Destroy ();
      

#. Run the simulation script with the following command:
   ::

      ./ns3 run <example_name>
