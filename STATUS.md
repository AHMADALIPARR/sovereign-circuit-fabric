# Build & Verification Status

**Evidence or Silence.**

## Present Artifacts (real source)

| Component                    | Path                                      | Status |
|------------------------------|-------------------------------------------|--------|
| Binary frame format          | `spec/circuit_frame.md`                   | Spec complete |
| Circuit identifiers          | `spec/circuit_id.md`                      | Spec complete |
| State machine                | `spec/state_machine.md`                   | Spec complete |
| Resource model               | `spec/resource_model.md`                  | Spec complete |
| Tariff arithmetic            | `spec/tariff_arithmetic.md`               | Spec complete |
| Core invariants              | `spec/invariants.md`                      | Spec complete |
| UUIE-Array accounting spec   | `spec/uuie_array.md`                      | Spec complete (Specification 0.1) |
| UUIE-Array formal semantics  | `spec/uuie_semantics.md`                  | Spec complete (round_div, promotion, broadcasting, reconciliation policy) |
| UUIE-Array engine + language | `accounting/uuie/`                        | C11 codec, SoA batch engine, event log, double-entry journal, lexer/parser/typed IR/evaluator; 117 acceptance checks pass under ASan/UBSan (`make -C accounting/uuie test`) |
| P4_16 pipeline               | `datapath/p4/circuit_forward.p4`          | Source present; header layout verified against the frame specification; p4c compilation outstanding (compiler not available in the current environment) |
| BEAM circuit actor           | `control/beam/circuit_actor.erl`          | Compiles under Erlang/OTP 25; lifecycle, reject, timeout, recycle, and illegal-transition tests pass (`control/beam/run_tests.sh`) |
| Tcl provisioning             | `control/tcl/provision.tcl`               | Source present; requires Tcl 9 |
| VHDL crossbar                | `hardware/vhdl/scf_crossbar.vhd`          | Source present; requires VHDL simulator/synth |
| TLA+ model                   | `formal/tla/CircuitState.tla`             | TLC checked with `formal/tla/CircuitState.cfg`: 6,271 states generated, 1,650 distinct, TypeOK / NoOverCommit / ReservedOnlyWhenHeld / FreeHasZero hold (finite instance: 2 circuits, MaxBandwidth 10) |
| Integer pricing engine       | `accounting/pricing.erl`                  | Compiles under Erlang/OTP 25; documented example charges 2500 (test in `control/beam/run_tests.sh`) |

## Explicitly Blocked / Documented

| Component | Reason |
|-----------|--------|
| Gödel circuit logic | Historical language; no modern reproducible toolchain. Specification preserved in `control/godel/README.md`. |
| Bare-metal BEAM | No executable demonstration of standalone BEAM without OS services yet. Investigation required. |
| WhyML / Why3 proofs | Not yet written; Phase II. |
| Dafny contracts      | Not yet written; Phase II. |
| Full VHDL testbenches & synthesis evidence | Minimal module only; full suite pending. |
| Dylan orchestration | Open Dylan exists; concrete modules not yet committed. |
| UUIE-Array integration | Engine core implemented and tested in C11; Dylan orchestration and BEAM event-coordination integration are the next stage. P4/VHDL metering integration pending the datapath work. |
| UUIE formal obligations | TLA+/Why3/Dafny obligations for the accounting extension (spec section 8) not yet discharged; the evaluator generates verification conditions, which are obligations, not proofs. |
| End-to-end integration | Phase VII. |

## Toolchain Notes

- **P4**: p4c (open-source) + a behavioral model or hardware target.
- **Erlang**: Any recent OTP; the actor is pure gen_statem.
- **Tcl**: Tcl 9.0+.
- **VHDL**: GHDL or commercial simulator; synthesis with Yosys/Vivado/etc.
- **TLA+**: TLC model checker.
- **Gödel**: Currently unavailable for execution.

No fabricated compiler output, proof results, or performance numbers are present in this repository.
