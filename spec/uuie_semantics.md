# UUIE-Array — Formal Semantics 0.1

Definitions required by `uuie_array.md` section 4 before
implementation: `round_div`, indexing, broadcasting, and arithmetic
promotion, plus the event reconciliation and counter rules of
sections 3 and 10. Where this document fixes a choice the
specification leaves open, the choice is stated as such.

## 1. Types and values

Fixed-width types: `u8`, `u16`, `u32`, `u64`, `u128`, `i64`.
A column is a contiguous array of one fixed-width type with an
explicit capacity and an explicit current length. A batch is a set
of columns of equal length processed together. Lengths and
capacities are part of every operation; there are no implicit
sizes.

## 2. Checked arithmetic and promotion

Elementwise arithmetic (`⊙`, `+`) is computed exactly and then
range-checked into the destination type:

- Operands are promoted to a 128-bit intermediate: `u128` when both
  operands are unsigned, `i128` when either is signed (`i64` setup
  values make the tariff computation signed).
- Multiplication of two `u64` values is exact in `u128`.
  Multiplication whose promoted operands exceed the 128-bit
  intermediate is rejected.
- After the exact result is computed, it must fit the destination
  column type. If it does not, the element's error bit is set in
  the batch error mask and the batch is rejected: no wrapping, no
  saturation, no partial commit.
- Division is by a declared positive scale only (see `round_div`);
  division by zero is a static error in a program and a rejected
  batch at runtime.

## 3. Broadcasting and indexing

- `array ⊙ array`: the operands have equal length; the operation
  is pairwise by row index. Unequal lengths are a type error.
- `array ⊙ scalar` / `scalar ⊙ array`: the scalar is broadcast to
  the array's length.
- Selection indexing `#Sel.column`: the column restricted to the
  rows of the selection `#Sel`, in the selection's row order.
- Tariff indexing `Rate[tariff]`: a gather. The `tariff` column of
  the circuit batch holds, per row, the ordinal index of a row of
  the `Rate` table (row 0 is the first declared row). The gather
  produces, per circuit row, the named `Rate` field of that row.
  An index outside the Rate table's length sets the batch error
  mask and rejects the batch.
- Selection `#`: `Table # (predicate)` evaluates the predicate per
  row and produces the ascending list of row indices for which the
  predicate is true. Predicate comparisons are exact integer
  comparisons. Named state constants (for example `ACTIVE`) are
  bound by the program environment; in the circuit state encoding
  of `state_machine.md` the states are numbered in declaration
  order starting at 0 (FREE=0, REQUESTED=1, ADMITTED=2, RESERVED=3,
  INSTALLED=4, ACTIVE=5, DRAINING=6, RELEASED=7, FAILED=8).

## 4. round_div

`round_div(x, s)` for an exact integer numerator `x` (promoted,
non-negative in the tariff algebra) and a declared scale `s > 0`:

```
round_div(x, s) = floor((2x + s) / (2s))
```

This is round-half-up: a remainder of exactly half the scale
rounds up, anything less rounds down, anything more rounds up.
All tariff quantities (bandwidth, duration, bytes, rates) are
unsigned, so no negative-numerator case arises in the tariff
algebra; `round_div` on a negative numerator is a batch error.

Scales are declared named constants in the program, never
implicit. For the reference tariff program:

- `CAPACITY_SCALE = 1000000000` — `capacity_rate` is minor units
  per bit-per-second per second, and `duration_ns` is nanoseconds,
  so dividing by nanoseconds-per-second makes the capacity term
  minor units.
- `TRAFFIC_SCALE = 1` — `byte_rate` is minor units per byte.

A tariff that uses different units declares different scale
constants; the algebra does not change.

## 5. sum_checked

`sum_checked(column)` accumulates the column in a `u128`
accumulator. If any partial sum exceeds the destination type of
the reduction (declared by the program), the reduction fails and
the batch is rejected. Integer addition is exact, so a successful
`sum_checked` result is independent of summation order.

## 6. Transfer (→)

`expr → Name` validates the batch: the error mask must be empty
and every invariant attached to the target must hold (for the
journal: total debits equal total credits). Only a validated
result is committed, atomically; a rejected batch changes no
committed state. Committed batches are immutable; corrections are
compensating entries (section 8).

## 7. Events: identity, sequence, reconciliation

- `event_id` is unique across the ledger. An event whose
  `event_id` has already been applied is a duplicate and is
  rejected; it changes nothing.
- Per circuit, `sequence` starts at 0 for the first applied event
  and increases by exactly 1 per applied event.
- Reconciliation policy (the choice fixed by this document for
  semantics 0.1):
  - `sequence == expected`: the event is applied.
  - `sequence < expected`: stale or replayed; rejected as a
    duplicate-sequence event.
  - `sequence > expected`: a gap. The event is **not** applied;
    the batch reports a missing-sequence error naming the circuit
    and the expected sequence. The operator replays the missing
    events in order; there is no reorder buffer in 0.1.
- Usage values in events are deltas supplied by trusted switch
  metering (specification section 3), never by UUIE fields.

## 8. Counters, rollover, and postings

- Hardware usage counters are cumulative `u64` values per circuit.
  The delta for an interval is `cur - prev` computed in `u64`
  wraparound arithmetic, which is exact provided the counter wraps
  at most once per interval (the metering contract). This is how
  counter rollover is handled.
- A settlement of amount A for event E creates exactly two
  postings: a debit of A to the customer receivable account and a
  credit of A to the circuit revenue account, both carrying E's
  `event_id`. Amounts are `u128`; each posting has exactly one of
  debit/credit nonzero.
- Journal invariant: over all committed postings,
  `Σ debit_minor == Σ credit_minor`, accumulated in `u128` and
  checked on every commit.
- A correction is a compensating settlement: a new event whose two
  postings swap the debit and credit accounts of the original.
  Committed postings are never modified.

## 9. UUIE.Accounting.v1 validity

A payload is valid for decoding if and only if its length is
exactly 28 bytes and its `version` field equals 1. All multi-byte
fields are big-endian. Any other length or version is rejected and
must not alter the ledger. Message-type values are carried, not
interpreted, by the codec in 0.1.
