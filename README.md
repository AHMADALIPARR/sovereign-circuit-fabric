# Sovereign Circuit Fabric

**Native Telecommunications Exchange**

Independent virtual-circuit switching system built from first principles.
No Linux dependency for the core exchange, no inherited Unix networking stack, no TCP/IP for internal switching.

## Architecture Overview

| Subsystem            | Language     | Responsibility                                      |
|----------------------|--------------|-----------------------------------------------------|
| Packet processing    | P4_16        | Parsing, classification, match-action forwarding    |
| Provisioning         | Tcl 9        | Configuration, circuit commands, tariff management  |
| Circuit logic        | Gödel        | Declarative route selection and admission           |
| Orchestration        | Dylan        | Route compilation, circuit control, interfaces      |
| Exchange runtime     | Erlang/BEAM  | Circuit actors, state machines, supervision         |
| State verification   | TLA+         | Distributed invariants and temporal properties      |
| Mathematical proofs  | WhyML/Why3   | Capacity, routing, arithmetic correctness           |
| Contract verification| Dafny        | Preconditions, postconditions, accounting contracts |
| Hardware             | VHDL         | Switching fabric, buffers, arbitration, ports, DMA  |

## Circuit Lifecycle

```
FREE → REQUESTED → ADMITTED → RESERVED → INSTALLED → ACTIVE → DRAINING → RELEASED
```

Failure, rollback, timeout, and recovery transitions are explicit and deterministic.

## Implementation Phases

- **Phase I** — Mathematical exchange model (frame encoding, circuit IDs, state machine, resource model, tariff arithmetic)
- **Phase II** — Formal specification (TLA+, WhyML, Dafny)
- **Phase III** — Software control plane (Gödel, Dylan, Tcl, BEAM)
- **Phase IV** — Programmable datapath (P4_16)
- **Phase V** — Hardware fabric (VHDL)
- **Phase VI** — Native runtime (non-Linux platform integration)
- **Phase VII** — End-to-end validation

## Repository Layout

```
spec/                 Phase I mathematical model & binary formats
formal/               TLA+, WhyML, Dafny artifacts
control/
  godel/              Declarative circuit logic (spec + toolchain notes)
  dylan/              Orchestration
  tcl/                Provisioning interface
  beam/               Erlang circuit actors
datapath/
  p4/                 P4_16 forwarding pipeline
hardware/
  vhdl/               Switching fabric modules + testbenches
accounting/           Fixed-point integer pricing engine
docs/                 Architecture & interface specifications
```

## Engineering Principle

**Evidence or Silence.**

A compiled file is not proof of correct behavior.  
A simulation is not proof of hardware timing closure.  
A verified mathematical model is not automatically proof of its implementation.

Every claim is tracked to actual evidence. No placeholders, no stubs, no fabricated results.

## Getting Started

See `spec/README.md` for the binary circuit-frame format, resource model, and state machine invariants that must be established before any datapath or control-plane code is trusted.

## Status

Phase I artifacts are present. Subsequent phases contain real source where toolchains permit; blocked historical toolchains (Gödel) are documented with preserved specifications rather than fabricated execution.

## License

See LICENSE.
