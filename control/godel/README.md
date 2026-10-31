# Gödel Circuit Logic

## Status

Gödel (Hill & Lloyd, 1992–1995) is a historical typed logic programming language.
The last publicly available implementation dates from the mid-1990s and is no longer
maintained as a production toolchain.

**Implementation is currently blocked** by the absence of a reproducible, modern
Gödel compiler/runtime.

Per the engineering principle *Evidence or Silence*:

- The declarative relations required by the architecture are preserved below as
  a formal specification.
- No fabricated Gödel source that claims to execute is provided.
- When a viable Gödel toolchain becomes available, the relations can be
  transcribed directly.

## Required Declarative Relations

```
EndpointConnectivity(Src, Dst, Link)
AvailableCapacity(Link, RemainingBps)
CircuitEligibility(Circuit, Policy)
RouteFeasibility(Src, Dst, Path, Bandwidth)
ResourceConstraint(Path, Bandwidth, Available)
PriorityRule(Circuit, Priority, Preemptable)
TariffSelection(Circuit, TariffId)
AdmissionRejection(Circuit, Reason)
FailureReroute(Circuit, FailedLink, AlternativePath)
```

## Interface to Dylan

Validated decisions are transferred to the Dylan orchestration layer via an
explicit, versioned decision record:

```
Decision = {
  circuit_id,
  action: admit | reject | reroute,
  path: list of links,
  reserved_bps,
  tariff_id,
  reason_code
}
```

The Gödel layer performs pure logical inference; it never mutates switch state
directly.
