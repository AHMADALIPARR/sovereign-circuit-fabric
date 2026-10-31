# Integer Circuit Pricing Engine

**Never use binary floating-point for financial settlement.**

## Units

- Currency is expressed in fixed-point minor units (e.g. 1/10000 of the major unit).
- All rates are integer values in minor-units per second, per bit, or per event.
- Rounding is performed with explicit floor / ceiling rules defined per tariff.

## Charge Formula

```
Charge = SetupFee
       + ReservedCapacityCharge
       + UsageCharge
       + PriorityCharge
```

Where:

- `SetupFee` = constant from tariff (minor units)
- `ReservedCapacityCharge` = reserved_bps * rate_per_bps_per_second * duration_seconds
  (integer multiply then divide with specified rounding)
- `UsageCharge` = bytes_transferred * rate_per_byte
- `PriorityCharge` = priority_multiplier * (SetupFee + ReservedCapacityCharge)

All multiplications are performed in 128-bit intermediate integers to avoid overflow; results are clamped or rejected according to tariff policy if they exceed 64-bit signed range.

## Tariff Versioning

Each tariff has:

- `tariff_id` (16-bit)
- `version` (32-bit monotonic)
- `effective_from` / `effective_to` timestamps

A circuit locks the tariff version at ADMITTED time. Later tariff changes do not affect already-admitted circuits.

## Accounting Events

Events are idempotent by `(circuit_id, event_seq)`.

- Establishment event (SetupFee)
- Periodic reservation charge events
- Final usage + priority reconciliation on RELEASED

Counter rollover is detected and reported; accounting never silently wraps.
