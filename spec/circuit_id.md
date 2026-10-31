# Circuit Identifiers

## Format

Defined by `circuit_frame.md` and `resource_model.md`:

- **CircuitID** — 32-bit unsigned identifier, carried in the circuit
  frame at offset 4.
- **NamespaceID** — 16-bit unsigned identifier, carried in the circuit
  frame at offset 8. Namespace 0 is the local namespace.

A circuit is identified by the pair (NamespaceID, CircuitID).

## Reserved Values

- CircuitID = 0 is reserved and invalid (see `circuit_frame.md`).

## Uniqueness

From `state_machine.md`:

- No two circuits may share the same (NamespaceID, CircuitID) while
  both are non-FREE.

## Lifecycle

From `state_machine.md`:

- An identifier is in use while its circuit is in any state other
  than FREE.
- A circuit in FREE holds no resources; its identifier may be reused
  after release (RELEASED --recycle--> FREE).

## Resource Ownership

From `resource_model.md`:

- Every non-FREE circuit owns a circuit resource record keyed by its
  `circuit_id` and `namespace_id`, holding its reserved bandwidth,
  buffer credits, priority, QoS class, tariff, and counters.
- On failure or release, every reserved resource is returned exactly
  once (idempotent release).

## Serialization

From `state_machine.md` (determinism requirements):

- Concurrent events on the same circuit are serialized by the owning
  BEAM process; state change and its outputs are atomic with respect
  to the circuit identifier.
