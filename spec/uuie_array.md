# UUIE-Array Accounting Extension — Specification 0.1

Architecture: P4_16 · Tcl 9 · Gödel · Dylan · Erlang/BEAM · TLA+ ·
WhyML/Why3 · Dafny · VHDL · UUIE-Array

Objective: a deterministic, array-oriented telecommunications
accounting engine that associates virtual-circuit signaling with
immutable usage records, tariff calculations, and double-entry
settlement.

Core principle: Signaling identifies the circuit. Hardware measures
the usage. Arrays compute the charge. The ledger establishes the
accounting record.

## 1. UUIE-Array language model

UUIE-Array is a domain-specific language with five primitive
operations:

| Operator | Meaning                        |
|----------|--------------------------------|
| `@`      | Declare typed columnar array   |
| `#`      | Select records by predicate    |
| `Σ`      | Checked integer reduction      |
| `⊙`      | Elementwise arithmetic         |
| `→`      | Transfer a validated result    |

All array operations require explicit dimensions, types, overflow
rules, and rounding semantics.

Example:

```
@Circuit[N] {
    id          : u64
    account     : u64
    bandwidth   : u64
    duration_ns : u64
    bytes       : u64
    tariff      : u32
    state       : u8
}
@Rate[T] {
    setup_minor      : i64
    capacity_rate    : u64
    byte_rate        : u64
    version          : u32
}
#Active = Circuit # (state == ACTIVE)
Σ Reserved = sum_checked(#Active.bandwidth)
```

The compiler defines `sum_checked` as a bounded integer reduction
that rejects overflow rather than wrapping.

## 2. UUIE signaling schema

A versioned application payload carries accounting correlation
information through a UUIE-capable signaling interface:

```
UUIE.Accounting.v1 {
    version       : u8
    message_type  : u8
    circuit_id    : u64be
    account_ref   : u64be
    tariff_id     : u32be
    sequence      : u32be
    flags         : u16be
}
```

The payload is 28 bytes. The Q.931 UUIE protocol discriminator and
information-element framing are handled by the signaling adapter,
not implicitly included in this 28-byte application payload.

The payload is untrusted signaling data. It is not authorization,
proof of payment, or an authoritative usage record.

## 3. Accounting event model

Immutable events:

```
@Event[N] {
    event_id       : u128
    circuit_id     : u64
    account_id     : u64
    sequence       : u64
    timestamp_ns   : u64
    byte_delta     : u64
    duration_ns    : u64
    tariff_version : u32
    kind           : u8
}
```

Event kinds: CIRCUIT_OPEN, CIRCUIT_RESERVE, CIRCUIT_ACTIVE,
USAGE_SAMPLE, TARIFF_APPLY, CIRCUIT_RELEASE, CIRCUIT_SETTLE.

Duplicate event identifiers are rejected. Sequence numbers are
monotonic per circuit; missing, delayed, or reordered events are
explicitly reconciled (policy in `uuie_semantics.md`).

Usage counters originate from trusted switch metering, not from
user-supplied UUIE fields.

## 4. Deterministic tariff algebra

Integer minor currency units. For circuit i:

```
@Setup[N] =
    Rate[tariff].setup_minor
@Capacity[N] =
    round_div(
        Circuit.bandwidth
        ⊙ Circuit.duration_ns
        ⊙ Rate[tariff].capacity_rate,
        CAPACITY_SCALE
    )
@Traffic[N] =
    round_div(
        Circuit.bytes
        ⊙ Rate[tariff].byte_rate,
        TRAFFIC_SCALE
    )
@Charge[N] =
    Setup + Capacity + Traffic
Σ Revenue =
    sum_checked(Charge)
```

`round_div`, indexing, broadcasting, and arithmetic promotion are
formally defined in `uuie_semantics.md`, as this specification
requires before implementation.

Rates have explicit units and fixed-point scales. Intermediate
integers are sufficiently wide and multiplication is checked.

## 5. Double-entry accounting

Each settled charge creates balanced postings:

```
@Posting[2*N] {
    event_id    : u128
    account_id  : u64
    debit_minor : u128
    credit_minor: u128
}
```

Example:

```
SETTLE circuit_id=1001 charge=12500 {
    DEBIT  CustomerReceivable 12500
    CREDIT CircuitRevenue     12500
}
```

The amount is 12,500 minor units, or $125.00 for a currency with
two decimal minor units.

Invariant: `Σ Posting.debit_minor == Σ Posting.credit_minor`.
A settlement batch that fails this invariant is rejected.

Accounting corrections use compensating entries rather than
modifying previously committed postings.

## 6. Array execution architecture

Structure-of-arrays memory layout:

```
circuit_id[N]       : u64
account_id[N]       : u64
duration_ns[N]      : u64
bandwidth_bps[N]    : u64
usage_bytes[N]      : u64
tariff_id[N]        : u32
charge_minor[N]     : i64
```

Execution requirements:

- Contiguous typed arrays
- Fixed-capacity processing batches
- Explicit strides and alignment
- Checked integer arithmetic
- SIMD-compatible reduction semantics
- No per-record object allocation in the hot path
- Deterministic results independent of processing order
- Explicit error masks and overflow handling
- Immutable committed accounting batches

## 7. Integration with the exchange

```
Tcl 9            Provision Circuit
Gödel + Dylan    Admit / Compile Route
BEAM             Establish Circuit
P4_16 + VHDL     Forward / Meter
UUIE Adapter     Correlate Circuit
UUIE-Array       Rate / Reduce / Post
Ledger           Immutable Accounting Ledger
```

The signaling adapter and hardware metering path are independent
inputs to the accounting engine. A circuit may be accounted for
even when its signaling transport does not support UUIE.

## 8. Formal verification obligations

- TLA+: no duplicate settlement; no settlement before valid usage
  reconciliation; recovery after interrupted posting; event
  ordering and circuit lifecycle consistency.
- WhyML/Why3: array bounds; checked reductions; fixed-point
  arithmetic; capacity and usage calculations; integer overflow
  prevention.
- Dafny: balanced journal transactions; idempotent event
  processing; tariff-version consistency; posting preconditions
  and postconditions.

Assumptions are stated explicitly. Finite-state model checking is
not described as a universal proof without additional
justification.

## 9. Compiler and execution requirements

A real parser, typed intermediate representation, evaluator, and
binary encoder/decoder for UUIE-Array, supporting: typed column
declarations; fixed-width binary records; predicate filtering;
elementwise arithmetic; checked reductions; tariff lookup; event
validation; journal generation; deterministic batch execution;
verification-condition generation.

The runtime uses the approved architecture: Dylan handles
accounting orchestration, BEAM handles event coordination, and
VHDL/P4 provide authoritative hardware metering.

## 10. Acceptance tests

- One circuit established, metered, billed, and released
- Multiple concurrent circuits with distinct tariffs
- Duplicate usage event rejected
- Missing sequence detected
- Hardware counter rollover handled
- Arithmetic overflow rejected
- Partial settlement recovered without duplicate posting
- Debit and credit totals exactly balanced
- Identical input batches producing identical accounting results
- Invalid UUIE signaling payload rejected without altering the
  ledger
