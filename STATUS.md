# Build & Verification Status

**Evidence or Silence.**

## Present Artifacts (real source)

| Component                    | Path                                      | Status |
|------------------------------|-------------------------------------------|--------|
| Binary frame format          | `spec/circuit_frame.md`                   | Spec complete |
| State machine                | `spec/state_machine.md`                   | Spec complete |
| Resource model               | `spec/resource_model.md`                  | Spec complete |
| Tariff arithmetic            | `spec/tariff_arithmetic.md`               | Spec complete |
| P4_16 pipeline               | `datapath/p4/circuit_forward.p4`          | Source present; requires p4c + target |
| BEAM circuit actor           | `control/beam/circuit_actor.erl`          | Source present; requires Erlang/OTP |
| Tcl provisioning             | `control/tcl/provision.tcl`               | Source present; requires Tcl 9 |
| VHDL crossbar                | `hardware/vhdl/scf_crossbar.vhd`          | Source present; requires VHDL simulator/synth |
| TLA+ model                   | `formal/tla/CircuitState.tla`             | Source present; requires TLC |
| Integer pricing engine       | `accounting/pricing.erl`                  | Source present |

## Explicitly Blocked / Documented

| Component | Reason |
|-----------|--------|
| Gödel circuit logic | Historical language; no modern reproducible toolchain. Specification preserved in `control/godel/README.md`. |
| Bare-metal BEAM | No executable demonstration of standalone BEAM without OS services yet. Investigation required. |
| WhyML / Why3 proofs | Not yet written; Phase II. |
| Dafny contracts      | Not yet written; Phase II. |
| Full VHDL testbenches & synthesis evidence | Minimal module only; full suite pending. |
| Dylan orchestration | Open Dylan exists; concrete modules not yet committed. |
| End-to-end integration | Phase VII. |

## Toolchain Notes

- **P4**: p4c (open-source) + a behavioral model or hardware target.
- **Erlang**: Any recent OTP; the actor is pure gen_statem.
- **Tcl**: Tcl 9.0+.
- **VHDL**: GHDL or commercial simulator; synthesis with Yosys/Vivado/etc.
- **TLA+**: TLC model checker.
- **Gödel**: Currently unavailable for execution.

No fabricated compiler output, proof results, or performance numbers are present in this repository.
