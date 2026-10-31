# Phase I — Mathematical Exchange Model

This directory contains the foundational definitions that all other subsystems must obey.

## Contents

| File | Purpose |
|------|---------|
| `circuit_frame.md` | Exact binary circuit-frame format |
| `circuit_id.md` | Namespace and identifier rules |
| `state_machine.md` | Deterministic lifecycle states and transitions |
| `resource_model.md` | Bandwidth, buffers, capacity arithmetic |
| `tariff_arithmetic.md` | Fixed-point integer pricing formulas |
| `invariants.md` | Core invariants that formal models later prove |

No forwarding pipeline, control plane, or hardware may violate these definitions.
