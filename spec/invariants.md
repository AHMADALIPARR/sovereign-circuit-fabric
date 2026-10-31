# Core Invariants

The invariants every subsystem must preserve. Sources:
`state_machine.md`, `resource_model.md`, `tariff_arithmetic.md`.

## State Invariants

From `state_machine.md` — these must hold after every transition:

- A circuit in FREE has zero reserved bandwidth and no table entries.
- A circuit in RESERVED or later has positive reserved bandwidth
  until RELEASED.
- No two circuits may share the same (NamespaceID, CircuitID) while
  both are non-FREE.
- Total reserved bandwidth on any link never exceeds link capacity.
- RELEASED implies a final accounting event has been generated
  exactly once.

## Resource Invariants

From `resource_model.md`:

- For every link L: `reserved_bps <= capacity_bps` at all times.
- Reservation of B on L succeeds only if
  `reserved_bps + B <= capacity_bps`.
- Release subtracts B and never goes negative.
- The sum of reserved_bps over all circuits that traverse a link
  equals the link's reserved_bps.
- On failure or release, every reserved resource is returned exactly
  once (idempotent release).

## Accounting Invariants

From `tariff_arithmetic.md`:

- Accounting events are idempotent by (circuit_id, event_seq).
- Counter rollover is detected and reported; accounting never
  silently wraps.

## Machine-Checked Invariants (TLA+)

The following invariants of `formal/tla/CircuitState.tla` have been
checked with TLC (configuration `formal/tla/CircuitState.cfg`,
Circuits = {c1, c2}, MaxBandwidth = 10; 6,271 states generated,
1,650 distinct, no error found):

- **TypeOK** — state, per-circuit reservations, and the total are
  always in their declared domains.
- **NoOverCommit** — `totalReserved <= MaxBandwidth` (the model's
  single capacity pool form of the per-link capacity invariant).
- **ReservedOnlyWhenHeld** — a circuit with reserved bandwidth is in
  RESERVED, INSTALLED, ACTIVE, DRAINING, or FAILED.
- **FreeHasZero** — a circuit in FREE has zero reserved bandwidth.

The TLC result covers the finite instance named in the
configuration; it is a model-check result for that instance, not a
proof for arbitrary circuit counts or capacities.
