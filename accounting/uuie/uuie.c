/* Sovereign Circuit Fabric — UUIE-Array accounting engine (C11).
 * See uuie.h and spec/uuie_semantics.md. No allocation anywhere in
 * this file: all storage is caller-provided fixed capacity.
 */
#include "uuie.h"
#include <string.h>

/* ---------------- signaling codec ---------------- */

static void put_u64be(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) { p[i] = (uint8_t)(v & 0xffu); v >>= 8; }
}
static void put_u32be(uint8_t *p, uint32_t v) {
    for (int i = 3; i >= 0; i--) { p[i] = (uint8_t)(v & 0xffu); v >>= 8; }
}
static void put_u16be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v & 0xffu);
}
static uint64_t get_u64be(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}
static uint32_t get_u32be(const uint8_t *p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v = (v << 8) | p[i];
    return v;
}
static uint16_t get_u16be(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

void uuie_sig_encode(const uuie_sig *in, uint8_t out[UUIE_SIG_LEN]) {
    out[0] = in->version;
    out[1] = in->message_type;
    put_u64be(out + 2, in->circuit_id);
    put_u64be(out + 10, in->account_ref);
    put_u32be(out + 18, in->tariff_id);
    put_u32be(out + 22, in->sequence);
    put_u16be(out + 26, in->flags);
}

int uuie_sig_decode(const uint8_t *buf, size_t len, uuie_sig *out) {
    if (buf == NULL || out == NULL || len != UUIE_SIG_LEN) return -1;
    if (buf[0] != UUIE_SIG_VERSION) return -1;
    out->version      = buf[0];
    out->message_type = buf[1];
    out->circuit_id   = get_u64be(buf + 2);
    out->account_ref  = get_u64be(buf + 10);
    out->tariff_id    = get_u32be(buf + 18);
    out->sequence     = get_u32be(buf + 22);
    out->flags        = get_u16be(buf + 26);
    return 0;
}

/* ---------------- tariff algebra ---------------- */

uuie_status uuie_round_div_u128(uuie_u128 x, uint64_t scale,
                                int64_t *out) {
    if (scale == 0) return UUIE_ERR_ARG;
    /* floor((2x + s) / (2s)) computed as x/s with a half-up fix-up
     * on the remainder, so no intermediate exceeds 128 bits. */
    uuie_u128 s = (uuie_u128)scale;
    uuie_u128 q = x / s;
    uuie_u128 r = x - q * s;
    if (2u * r >= s) q += 1u;
    if (q > (uuie_u128)INT64_MAX) return UUIE_ERR_OVERFLOW;
    *out = (int64_t)q;
    return UUIE_OK;
}

uuie_status uuie_round_div(uuie_i128 x, uint64_t scale, int64_t *out) {
    if (x < 0) return UUIE_ERR_OVERFLOW;
    return uuie_round_div_u128((uuie_u128)x, scale, out);
}

uuie_status uuie_compute_charges(uuie_batch *b, const uuie_rates *r,
                                 uint64_t capacity_scale,
                                 uint64_t traffic_scale) {
    if (b == NULL || r == NULL || capacity_scale == 0 || traffic_scale == 0)
        return UUIE_ERR_ARG;
    b->err = UUIE_OK;
    for (uint32_t i = 0; i < b->n; i++) {
        uint32_t t = b->tariff_id[i];
        if (t >= r->n) {
            b->err = UUIE_ERR_INDEX; b->err_row = i;
            return b->err;
        }
        /* exact intermediates in 128 bits; the third multiply is
         * overflow-checked (semantics section 2) */
        uuie_u128 cap_prod = (uuie_u128)b->bandwidth_bps[i]
                           * (uuie_u128)b->duration_ns[i];
        if (r->capacity_rate[t] != 0 &&
            cap_prod > ~(uuie_u128)0 / (uuie_u128)r->capacity_rate[t]) {
            b->err = UUIE_ERR_OVERFLOW; b->err_row = i;
            return b->err;
        }
        cap_prod *= (uuie_u128)r->capacity_rate[t];
        uuie_u128 traf_prod = (uuie_u128)b->usage_bytes[i]
                            * (uuie_u128)r->byte_rate[t];
        int64_t cap_charge, traf_charge;
        uuie_status st = uuie_round_div_u128(cap_prod,
                                             capacity_scale, &cap_charge);
        if (st == UUIE_OK)
            st = uuie_round_div_u128(traf_prod,
                                     traffic_scale, &traf_charge);
        if (st != UUIE_OK) {
            b->err = st; b->err_row = i;
            return b->err;
        }
        uuie_i128 total = (uuie_i128)r->setup_minor[t]
                        + (uuie_i128)cap_charge
                        + (uuie_i128)traf_charge;
        if (total > (uuie_i128)INT64_MAX || total < (uuie_i128)INT64_MIN) {
            b->err = UUIE_ERR_OVERFLOW; b->err_row = i;
            return b->err;
        }
        b->charge_minor[i] = (int64_t)total;
    }
    return UUIE_OK;
}

uuie_status uuie_sum_charges(const uuie_batch *b, uuie_i128 *out) {
    uuie_i128 acc = 0;
    for (uint32_t i = 0; i < b->n; i++) acc += (uuie_i128)b->charge_minor[i];
    if (acc > (uuie_i128)INT64_MAX || acc < (uuie_i128)INT64_MIN)
        return UUIE_ERR_OVERFLOW;
    *out = acc;
    return UUIE_OK;
}

/* ---------------- event log ---------------- */

static uint32_t hash128(uuie_u128 v) {
    uint64_t lo = (uint64_t)v, hi = (uint64_t)(v >> 64);
    return (uint32_t)(lo * 0x9E3779B97F4A7C15ull ^ hi * 0xC2B2AE3D27D4EB4Full);
}
static uint32_t hash64(uint64_t v) {
    return (uint32_t)(v * 0x9E3779B97F4A7C15ull);
}

void uuie_eventlog_init(uuie_eventlog *log, uuie_u128 *ids,
                        uint8_t *ids_used, uint64_t *seq_cid,
                        uint64_t *seq_next, uint8_t *seq_used,
                        uint32_t cap) {
    log->ids = ids;         log->ids_used = ids_used;
    log->seq_cid = seq_cid; log->seq_next = seq_next;
    log->seq_used = seq_used;
    log->cap = cap;         log->applied = 0;
    memset(ids_used, 0, cap);
    memset(seq_used, 0, cap);
}

static int id_find(const uuie_eventlog *log, uuie_u128 id, uint32_t *slot) {
    uint32_t mask = log->cap - 1, i = hash128(id) & mask;
    uint32_t probes = 0;
    while (log->ids_used[i] && probes < log->cap) {
        if (log->ids[i] == id) { *slot = i; return 1; }
        i = (i + 1) & mask;
        probes++;
    }
    *slot = i;
    return 0;
}

static int seq_find(const uuie_eventlog *log, uint64_t cid, uint32_t *slot) {
    uint32_t mask = log->cap - 1, i = hash64(cid) & mask;
    uint32_t probes = 0;
    while (log->seq_used[i] && probes < log->cap) {
        if (log->seq_cid[i] == cid) { *slot = i; return 1; }
        i = (i + 1) & mask;
        probes++;
    }
    *slot = i;
    return 0;
}

uuie_status uuie_event_apply(uuie_eventlog *log, const uuie_event *ev,
                             uint64_t *expected_out) {
    if (log == NULL || ev == NULL) return UUIE_ERR_ARG;
    if (log->applied >= log->cap) return UUIE_ERR_CAPACITY;
    uint32_t islot;
    if (id_find(log, ev->event_id, &islot)) return UUIE_ERR_DUP_EVENT;
    uint32_t sslot;
    int have = seq_find(log, ev->circuit_id, &sslot);
    uint64_t expected = have ? log->seq_next[sslot] : 0;
    if (ev->sequence != expected) {
        if (expected_out != NULL) *expected_out = expected;
        return UUIE_ERR_SEQ;
    }
    log->ids[islot] = ev->event_id;
    log->ids_used[islot] = 1;
    if (!have) {
        log->seq_cid[sslot] = ev->circuit_id;
        log->seq_used[sslot] = 1;
    }
    log->seq_next[sslot] = expected + 1;
    log->applied++;
    return UUIE_OK;
}

/* ---------------- journal ---------------- */

void uuie_journal_init(uuie_journal *j, uuie_posting *rows, uint32_t cap) {
    j->rows = rows; j->len = 0; j->cap = cap;
    j->total_debit = 0; j->total_credit = 0;
}

int uuie_journal_balanced(const uuie_journal *j) {
    return j->total_debit == j->total_credit;
}

/* Count rows of a settlement (event_id + the two accounts). */
static void settle_scan(const uuie_journal *j, uuie_u128 event_id,
                        uint64_t customer, uint64_t revenue,
                        uuie_u128 amount,
                        int *have_debit, int *have_credit, int *mismatch) {
    *have_debit = 0; *have_credit = 0; *mismatch = 0;
    for (uint32_t i = 0; i < j->len; i++) {
        const uuie_posting *p = &j->rows[i];
        if (p->event_id != event_id) continue;
        if (p->account_id == customer && p->credit_minor == 0 &&
            p->debit_minor == amount)
            *have_debit = 1;
        else if (p->account_id == revenue && p->debit_minor == 0 &&
                 p->credit_minor == amount)
            *have_credit = 1;
        else
            *mismatch = 1;
    }
}

static uuie_status settle_pair(uuie_journal *j, uuie_u128 event_id,
                               uint64_t debit_acct, uint64_t credit_acct,
                               uuie_u128 amount) {
    int hd, hc, mm;
    settle_scan(j, event_id, debit_acct, credit_acct, amount,
                &hd, &hc, &mm);
    if (mm) return UUIE_ERR_BALANCE;
    if (hd && hc) return UUIE_OK;              /* idempotent retry */
    uint32_t need = (hd ? 0u : 1u) + (hc ? 0u : 1u);
    if (j->len + need > j->cap) return UUIE_ERR_CAPACITY;
    if (!hd) {
        uuie_posting *p = &j->rows[j->len++];
        p->event_id = event_id; p->account_id = debit_acct;
        p->debit_minor = amount; p->credit_minor = 0;
        j->total_debit += amount;
    }
    if (!hc) {
        uuie_posting *p = &j->rows[j->len++];
        p->event_id = event_id; p->account_id = credit_acct;
        p->debit_minor = 0; p->credit_minor = amount;
        j->total_credit += amount;
    }
    if (!uuie_journal_balanced(j)) return UUIE_ERR_BALANCE;
    return UUIE_OK;
}

uuie_status uuie_journal_settle(uuie_journal *j, uuie_u128 event_id,
                                uint64_t customer_acct,
                                uint64_t revenue_acct,
                                uuie_u128 amount) {
    if (j == NULL) return UUIE_ERR_ARG;
    return settle_pair(j, event_id, customer_acct, revenue_acct, amount);
}

uuie_status uuie_journal_reverse(uuie_journal *j, uuie_u128 event_id,
                                 uint64_t customer_acct,
                                 uint64_t revenue_acct,
                                 uuie_u128 amount) {
    if (j == NULL) return UUIE_ERR_ARG;
    return settle_pair(j, event_id, revenue_acct, customer_acct, amount);
}
