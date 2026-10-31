/* Sovereign Circuit Fabric — UUIE-Array accounting engine (C11).
 *
 * Implements spec/uuie_array.md (Specification 0.1) under the formal
 * semantics of spec/uuie_semantics.md:
 *   - UUIE.Accounting.v1 binary signaling codec (28 bytes, big-endian)
 *   - structure-of-arrays batch engine with checked arithmetic
 *   - immutable event application (duplicate / sequence rules)
 *   - double-entry journal with a checked balance invariant
 * All batch storage is caller-provided fixed capacity; the engine
 * performs no allocation. 128-bit intermediates use the compiler's
 * __int128 (gcc/clang on 64-bit targets).
 */
#ifndef UUIE_H
#define UUIE_H

#include <stdint.h>
#include <stddef.h>

typedef unsigned __int128 uuie_u128;
typedef __int128          uuie_i128;

/* Status / error mask bits (semantics: batch error mask). */
enum {
    UUIE_OK            = 0,
    UUIE_ERR_OVERFLOW  = 1u << 0,  /* exact result does not fit destination */
    UUIE_ERR_INDEX     = 1u << 1,  /* tariff gather index out of range      */
    UUIE_ERR_DUP_EVENT = 1u << 2,  /* event_id already applied              */
    UUIE_ERR_SEQ       = 1u << 3,  /* sequence gap / stale sequence         */
    UUIE_ERR_BALANCE   = 1u << 4,  /* journal debit != credit               */
    UUIE_ERR_CODEC     = 1u << 5,  /* invalid signaling payload             */
    UUIE_ERR_CAPACITY  = 1u << 6,  /* fixed-capacity store full             */
    UUIE_ERR_ARG       = 1u << 7   /* invalid argument (e.g. scale <= 0)    */
};
typedef uint32_t uuie_status;

/* ------------------------------------------------------------------ */
/* Signaling codec: UUIE.Accounting.v1, 28 bytes, big-endian fields.  */
/* The payload is untrusted correlation data only (spec section 2).   */
/* ------------------------------------------------------------------ */

#define UUIE_SIG_LEN     28u
#define UUIE_SIG_VERSION 1u

typedef struct {
    uint8_t  version;
    uint8_t  message_type;
    uint64_t circuit_id;
    uint64_t account_ref;
    uint32_t tariff_id;
    uint32_t sequence;
    uint16_t flags;
} uuie_sig;

/* Encode always writes exactly UUIE_SIG_LEN bytes. */
void uuie_sig_encode(const uuie_sig *in, uint8_t out[UUIE_SIG_LEN]);

/* Valid iff len == 28 and version == 1 (semantics section 9).
 * Returns 0 on success, -1 on an invalid payload. */
int uuie_sig_decode(const uint8_t *buf, size_t len, uuie_sig *out);

/* ------------------------------------------------------------------ */
/* Batch engine (spec section 6 layout).                              */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t  n;              /* rows in this batch            */
    uint32_t  cap;            /* fixed capacity                */
    uint64_t *circuit_id;
    uint64_t *account_id;
    uint64_t *duration_ns;
    uint64_t *bandwidth_bps;
    uint64_t *usage_bytes;
    uint32_t *tariff_id;
    int64_t  *charge_minor;   /* output of uuie_compute_charges */
    uuie_status err;          /* batch error mask               */
    uint32_t  err_row;        /* first row that set an error    */
} uuie_batch;

typedef struct {
    uint32_t  n;
    uint32_t  cap;
    int64_t  *setup_minor;
    uint64_t *capacity_rate;  /* minor per bps per second */
    uint64_t *byte_rate;      /* minor per byte           */
    uint32_t *version;
} uuie_rates;

/* round_div(x, s) = floor((2x + s) / (2s)) on exact integers
 * (semantics section 4): round-half-up. x must be >= 0, s > 0.
 * The u128 form is the core; the i128 form rejects negatives. */
uuie_status uuie_round_div_u128(uuie_u128 x, uint64_t scale, int64_t *out);
uuie_status uuie_round_div(uuie_i128 x, uint64_t scale, int64_t *out);

/* Deterministic counter delta under u64 wraparound (semantics
 * section 8): exact when the counter wraps at most once per
 * interval. */
static inline uint64_t uuie_counter_delta(uint64_t cur, uint64_t prev) {
    return cur - prev;
}

/* Charge per row (spec section 4):
 *   charge = setup[tariff]
 *          + round_div(bandwidth * duration_ns * capacity_rate[tariff],
 *                      capacity_scale)
 *          + round_div(bytes * byte_rate[tariff], traffic_scale)
 * Intermediates are exact 128-bit; any row whose charge does not
 * fit i64 sets UUIE_ERR_OVERFLOW and no charge is committed for
 * any row (the batch is rejected as a whole). */
uuie_status uuie_compute_charges(uuie_batch *b, const uuie_rates *r,
                                 uint64_t capacity_scale,
                                 uint64_t traffic_scale);

/* sum_checked over charge_minor: exact i128 accumulation; sets
 * UUIE_ERR_OVERFLOW if the total does not fit i64. */
uuie_status uuie_sum_charges(const uuie_batch *b, uuie_i128 *out);

/* ------------------------------------------------------------------ */
/* Events (spec section 3) and journal (spec section 5).              */
/* ------------------------------------------------------------------ */

enum {
    UUIE_EV_OPEN = 1, UUIE_EV_RESERVE = 2, UUIE_EV_ACTIVE = 3,
    UUIE_EV_USAGE = 4, UUIE_EV_TARIFF = 5, UUIE_EV_RELEASE = 6,
    UUIE_EV_SETTLE = 7
};

typedef struct {
    uuie_u128 event_id;
    uint64_t  circuit_id;
    uint64_t  account_id;
    uint64_t  sequence;
    uint64_t  timestamp_ns;
    uint64_t  byte_delta;
    uint64_t  duration_ns;
    uint32_t  tariff_version;
    uint8_t   kind;
} uuie_event;

/* Applied-event store: fixed capacity (a power of two), open
 * addressing on event_id for duplicates and on circuit_id for
 * next sequence. */
typedef struct {
    uuie_u128 *ids;        /* applied event ids (occupied flag below) */
    uint8_t   *ids_used;
    uint64_t  *seq_cid;    /* circuit ids with an applied sequence    */
    uint64_t  *seq_next;   /* next expected sequence per circuit      */
    uint8_t   *seq_used;
    uint32_t   cap;        /* capacity of each array                  */
    uint32_t   applied;    /* number of applied events                */
} uuie_eventlog;

void uuie_eventlog_init(uuie_eventlog *log, uuie_u128 *ids,
                        uint8_t *ids_used, uint64_t *seq_cid,
                        uint64_t *seq_next, uint8_t *seq_used,
                        uint32_t cap);

/* Apply one event under semantics section 7. On any error the log
 * is unchanged. When the error is UUIE_ERR_SEQ due to a gap,
 * *expected_out (if non-NULL) receives the expected sequence. */
uuie_status uuie_event_apply(uuie_eventlog *log, const uuie_event *ev,
                             uint64_t *expected_out);

typedef struct {
    uuie_u128 event_id;
    uint64_t  account_id;
    uuie_u128 debit_minor;
    uuie_u128 credit_minor;
} uuie_posting;

typedef struct {
    uuie_posting *rows;
    uint32_t      len;
    uint32_t      cap;
    uuie_u128     total_debit;
    uuie_u128     total_credit;
} uuie_journal;

void uuie_journal_init(uuie_journal *j, uuie_posting *rows, uint32_t cap);

/* Append the balanced pair for a settlement of `amount` minor
 * units: debit customer_acct, credit revenue_acct, both under
 * event_id. The append is all-or-nothing and re-checks the
 * balance invariant (semantics sections 6 and 8). A partial
 * settlement recovered after interruption is completed by calling
 * this once with the same event_id: if the pair is already fully
 * present it is a no-op success (idempotent), so a recovery retry
 * can never duplicate the posting. */
uuie_status uuie_journal_settle(uuie_journal *j, uuie_u128 event_id,
                                uint64_t customer_acct,
                                uint64_t revenue_acct,
                                uuie_u128 amount);

/* Compensating entry: the mirror pair of an earlier settlement
 * (debit revenue, credit customer) under a new event_id. */
uuie_status uuie_journal_reverse(uuie_journal *j, uuie_u128 event_id,
                                 uint64_t customer_acct,
                                 uint64_t revenue_acct,
                                 uuie_u128 amount);

/* 1 iff total debits equal total credits across committed rows. */
int uuie_journal_balanced(const uuie_journal *j);

#endif /* UUIE_H */
