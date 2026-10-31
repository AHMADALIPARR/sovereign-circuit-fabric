# Binary Circuit-Frame Format

Custom frame format. Ethernet/IP are not required for internal exchange traffic.

## Layout (big-endian unless noted)

```
Offset  Size  Field                 Description
------  ----  --------------------  -----------------------------------------------
0       2     Magic                 0xSCF1 ("Sovereign Circuit Fabric 1")
2       1     Version               Currently 0x01
3       1     Flags                 bit0 = control, bit1 = data, bit2 = OAM
4       4     CircuitID             32-bit identifier within namespace
8       2     NamespaceID           Explicit namespace (0 = local)
10      1     Priority              0..7 (higher = higher priority)
11      1     QoSClass              0..15
12      4     Sequence              Monotonic per-circuit sequence number
16      2     PayloadLength         Length of payload in bytes (0..65535)
18      2     Checksum              16-bit one's complement over header+payload
20      N     Payload               Circuit payload (may be empty)
```

Total fixed header size: **20 bytes**.

## Rules

- Frames with Magic != 0xSCF1 are dropped at ingress.
- Version must equal 0x01; higher versions are rejected until negotiated.
- CircuitID = 0 is reserved (invalid).
- PayloadLength must match the actual bytes present after the header.
- Checksum covers bytes 0..(19+PayloadLength); the checksum field itself is treated as zero during calculation.
- No padding is inserted; alignment is the responsibility of the payload producer.
- Byte order of multi-byte fields is network order (big-endian).

## Control vs Data

- Flag bit0 set → control plane message (setup, teardown, accounting, OAM).
- Flag bit1 set → data plane payload belonging to an ACTIVE circuit.
- Both bits may be set only for marked control-over-data frames; normal data must have only bit1.

## Extensibility

Future versions may append optional TLVs after the fixed header. Version 0x01 contains no TLVs.
