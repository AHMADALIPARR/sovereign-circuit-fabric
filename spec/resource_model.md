# Resource Model

## Units

- Bandwidth: integer bits per second (bps).
- Buffer capacity: integer bytes.
- Circuit identifiers: 32-bit unsigned within a 16-bit namespace.

## Link Capacity

Each physical or logical link L has:

- `capacity_bps` : positive integer
- `reserved_bps` : non-negative integer, initially 0
- Invariant: `reserved_bps <= capacity_bps` at all times.

Reservation of bandwidth B on link L succeeds only if

```
reserved_bps + B <= capacity_bps
```

and then updates `reserved_bps := reserved_bps + B`.

Release subtracts B and never goes negative.

## Buffer Pools

Ingress and egress queues are fixed-capacity ring buffers.

- Each queue has a hard maximum depth (bytes or packets).
- Allocation is explicit; there is no unbounded dynamic allocation on the packet path.
- Ownership of a buffer descriptor is transferred by the DMA descriptor hand-off protocol (see hardware ABI).

## Circuit Resource Record

Every non-FREE circuit owns:

```
struct CircuitResources {
  uint32_t  circuit_id;
  uint16_t  namespace_id;
  uint64_t  reserved_bps;
  uint32_t  buffer_credits;
  uint8_t   priority;
  uint8_t   qos_class;
  uint64_t  setup_timestamp_ns;
  uint64_t  bytes_sent;
  uint64_t  bytes_received;
  uint32_t  packets_sent;
  uint32_t  packets_received;
  uint16_t  tariff_id;
  // ownership metadata for rollback
};
```

## Conservation

- Sum of reserved_bps over all circuits that traverse a given link equals the link's reserved_bps.
- On failure or release, every reserved resource is returned exactly once (idempotent release).
