# Handoff — Sovereign Circuit Fabric

**To:** Grok Build (or any continuation agent)  
**From:** Initial scaffolding session (Phase I + core sources)  
**Repo:** https://github.com/AHMADALIPARR/sovereign-circuit-fabric  
**Date:** 2026-10-31

This document records what has been delivered, what remains to satisfy the master engineering prompt, and what cannot be completed on the current VM. Use it as the single source of truth for the next work package.

---

## 1. Engineering principle (do not violate)

**Evidence or Silence.**

- No placeholders, stubs, TODOs, fabricated compiler output, invented proof results, or fake benchmarks.
- If a toolchain is missing or historical, preserve the specification and mark the implementation **blocked**.
- Every claim of correctness must point to actual evidence (source, model-check result, simulation log, proof certificate).

---

## 2. What is already in the repository

### Phase I — Mathematical exchange model (complete as specification)

| Artifact | Path | Notes |
|----------|------|-------|
| Binary circuit-frame format | `spec/circuit_frame.md` | Magic 0xSCF1, 20-byte header, custom format |
| Deterministic state machine | `spec/state_machine.md` | FREE→…→RELEASED + FAILED, legal transitions, invariants |
| Resource model | `spec/resource_model.md` | Bandwidth conservation, buffer pools, circuit resource record |
| Tariff / pricing arithmetic | `spec/tariff_arithmetic.md` | Fixed-point only, charge formula, versioning |
| Spec index | `spec/README.md` | |

### Executable / compilable sources present

| Subsystem | Path | Toolchain required to exercise |
|-----------|------|--------------------------------|
| P4_16 forwarding | `datapath/p4/circuit_forward.p4` | p4c + BMv2 or hardware target |
| BEAM circuit actor | `control/beam/circuit_actor.erl` | Erlang/OTP (gen_statem) |
| Tcl 9 provisioning | `control/tcl/provision.tcl` | Tcl 9.0+ |
| VHDL crossbar | `hardware/vhdl/scf_crossbar.vhd` | GHDL / Vivado / Yosys + simulator |
| TLA+ model | `formal/tla/CircuitState.tla` | TLC |
| Integer pricing | `accounting/pricing.erl` | Erlang/OTP (+ eunit) |

### Explicitly documented blockage

| Item | Path | Reason |
|------|------|--------|
| Gödel circuit logic | `control/godel/README.md` | Historical language; no modern reproducible toolchain. Relations specified only. |

### Meta

- `README.md` — architecture table + phase plan  
- `STATUS.md` — artifact vs blocked list  
- `LICENSE` — MIT  
- `HANDOFF.md` — this file  

---

## 3. What the master prompt still requires (gap list)

Work through these in the order given by the prompt. Do not claim a phase complete until its evidence exists.

### Phase II — Formal specification (largely missing)

- [ ] **TLA+**  
  - Expand `formal/tla/CircuitState.tla` (or add modules) for: transaction rollback, distributed state consistency across multiple switches, failure recovery, no double reservation under concurrent requests.  
  - Produce TLC model-check logs (or explicit “unchecked” status with configuration used).

- [ ] **WhyML / Why3**  
  - Capacity arithmetic, routing feasibility, bounded queues, reservation conservation, counter correctness.  
  - Distinguish discharged proofs from assumptions.  
  - Location suggestion: `formal/why3/`.

- [ ] **Dafny**  
  - Circuit installation contracts, state-transition contracts, resource ownership, accounting invariants, release postconditions.  
  - Location suggestion: `formal/dafny/`.

### Phase III — Software control plane (partial)

- [ ] **Gödel**  
  - Still blocked. Keep specification only unless a viable toolchain is obtained. Do **not** invent executable Gödel.

- [ ] **Dylan orchestration**  
  - Route compilation, switch-table installation, resource reservation, provisioning transactions, rollback on partial install, configuration validation, accounting-event generation.  
  - Idempotent commands + explicit transaction IDs.  
  - Open Dylan exists (opendylan.org); concrete modules are not yet in the repo.  
  - Location: `control/dylan/`.

- [ ] **Tcl 9**  
  - Current `provision.tcl` covers basic lifecycle. Still needed: full tariff management, idempotent command protocol, transaction identifiers, integration with Dylan/BEAM decision records.

- [ ] **BEAM**  
  - Supervision tree, one process per circuit (scalability limits measured, not assumed).  
  - Timeouts, failure notification, recovery/reconciliation, accounting lifecycle.  
  - Investigation of standalone BEAM without Linux (VM port, scheduler, memory, timer, I/O, platform services). Document required services; do **not** claim bare-metal compatibility without an executable demonstration.

### Phase IV — Programmable datapath

- [ ] Complete P4_16 behaviour:  
  - Admission-state enforcement, label translation, priority classification, metering/accounting metadata, drop/error handling, deparser.  
  - Custom frame only (already started).  
  - Separate P4-programmable logic from target-specific queueing/scheduling.  
  - Reference execution tests (BMv2 or equivalent) with real packet traces.

### Phase V — Hardware fabric

- [ ] Full set of synthesizable VHDL modules:  
  - Frame ingress/egress, circuit-label lookup, FIFO buffers, DMA descriptor interface, hardware counters, backpressure, reset & clock-domain crossing, control-plane register interface.  
  - Current repo has only a simplified crossbar.  
- [ ] Simulation testbenches.  
- [ ] Synthesis evidence (resource utilisation, timing) for at least one target; do not invent numbers.

### Phase VI — Native runtime

- [ ] Dedicated I/O subsystem design: fixed-capacity contiguous buffers, ring queues, explicit memory ownership, DMA descriptors, interrupt handling, deterministic arbitration, backpressure, counters, byte ordering/alignment.  
- [ ] Hardware/software ABI, buffer layouts, descriptor ownership transitions, memory-ordering rules.  
- [ ] Integration path that does **not** depend on Linux for the core exchange. Document every remaining OS dependency honestly.

### Phase VII — End-to-end validation

Demonstrate (with logs/evidence):

```
Provision → Admit → Reserve → Install → Switch → Meter → Bill → Release
```

Also: concurrent circuits, resource exhaustion, switch failures, replayed commands, accounting consistency, recovery.

### Cross-cutting deliverables still missing or incomplete

1. Complete architecture & interface specifications (beyond Phase I).  
2. Gödel route/admission rules as executable logic (blocked) or permanent specification-only status.  
3. Dylan orchestration implementation.  
4. Fuller Tcl provisioning surface.  
5. BEAM supervision + recovery.  
6. Complete P4 program + tests.  
7. Full VHDL suite + testbenches + synthesis evidence.  
8. WhyML/Why3 + Dafny artifacts.  
9. Integer pricing engine integrated with accounting events (idempotent).  
10. Reproducible build, test, and integration procedures for every executable subsystem (compiler versions, actual test results, proof status, simulation status, unresolved boundaries).

---

## 4. What this VM / session could not fulfill

- No interactive run of p4c, Erlang, GHDL, TLC, Why3, Dafny, or Open Dylan inside the scaffolding environment.  
- No GPU / FPGA synthesis or timing closure.  
- No bare-metal or non-Linux BEAM port.  
- No Gödel compiler.  
- No end-to-end multi-process demonstration.  
- No formal proof certificates or model-check logs beyond the source models themselves.

All of the above must be performed on a machine that has the real toolchains (or explicitly marked blocked with evidence of the attempt).

---

## 5. Recommended next actions for Grok Build

1. **Clone** `https://github.com/AHMADALIPARR/sovereign-circuit-fabric` and read `STATUS.md` + this `HANDOFF.md` + entire `spec/` directory before writing any new code.

2. **Phase II first**  
   - Flesh out TLA+ (rollback, multi-switch, recovery).  
   - Add WhyML/Why3 and Dafny modules that discharge the invariants stated in `spec/`.  
   - Commit actual `.tla` / `.mlw` / `.dfy` files and any successful check logs.

3. **Control plane**  
   - Implement Dylan orchestration modules that consume the decision record defined in `control/godel/README.md`.  
   - Extend Tcl with transaction IDs and idempotency.  
   - Add BEAM supervisor and measure process scalability; document limits.

4. **Datapath**  
   - Extend the P4 program to the full 10-stage pipeline listed in the master prompt.  
   - Provide a BMv2 (or equivalent) test harness with SCF-framed packets.

5. **Hardware**  
   - Expand VHDL to the full module list; add testbenches; run simulation; attempt synthesis on a concrete target and record real utilisation/timing.

6. **Accounting**  
   - Wire `accounting/pricing.erl` into the RELEASED transition so a single idempotent final event is emitted.

7. **Native I/O & Phase VI**  
   - Write the ABI and buffer/descriptor ownership rules as a specification under `docs/` or `spec/`, then implement only what can be evidenced.

8. **Update `STATUS.md`** after every real milestone. Never mark something “done” without pointing at the evidence file or log.

9. **Do not**  
   - Substitute Python/Go/C++/Rust/JS for the mandated languages.  
   - Claim bare-metal BEAM or Gödel execution without a runnable demonstration.  
   - Invent performance numbers or proof results.

---

## 6. Success criteria for the handoff recipient

The master prompt is satisfied only when:

- Every mandatory language has either real source + reproducible build/test **or** an explicit, justified blockage note.  
- Phase I invariants are formalised and (where possible) machine-checked.  
- An end-to-end run of Provision → … → Release can be replayed from scripts/logs.  
- `STATUS.md` and this handoff accurately reflect evidence, not aspiration.

Begin with Phase II formalisation; do not skip ahead to more code without the mathematical and logical foundation the prompt demands.

---

*End of handoff.*
