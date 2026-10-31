/* Sovereign Circuit Fabric — UUIE-Array acceptance tests.
 * Covers spec/uuie_array.md section 10 at the engine and language
 * level. Every check is an exact assertion; any failure prints the
 * case and exits nonzero.
 */
#include "uuie.h"
#include "uuie_lang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks = 0, failures = 0;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static void u128_str(uuie_u128 v, char *buf) {
    char tmp[48]; int i = 0;
    if (v == 0) { strcpy(buf, "0"); return; }
    while (v) { tmp[i++] = (char)('0' + (int)(v % 10)); v /= 10; }
    int j = 0;
    while (i) buf[j++] = tmp[--i];
    buf[j] = 0;
}

/* ---------------- codec ---------------- */

static void test_codec(void) {
    uuie_sig in = { .version = 1, .message_type = 3,
                    .circuit_id = 0x0102030405060708ULL,
                    .account_ref = 0xA0B0C0D0E0F00102ULL,
                    .tariff_id = 0x11223344u, .sequence = 0x99u,
                    .flags = 0x8001u };
    uint8_t buf[UUIE_SIG_LEN];
    uuie_sig_encode(&in, buf);
    CHECK(buf[2] == 0x01 && buf[9] == 0x08);       /* big-endian */
    CHECK(buf[10] == 0xA0 && buf[17] == 0x02);
    uuie_sig out;
    CHECK(uuie_sig_decode(buf, UUIE_SIG_LEN, &out) == 0);
    CHECK(out.circuit_id == in.circuit_id &&
          out.account_ref == in.account_ref &&
          out.tariff_id == in.tariff_id &&
          out.sequence == in.sequence && out.flags == in.flags);
    CHECK(uuie_sig_decode(buf, UUIE_SIG_LEN - 1, &out) == -1);
    buf[0] = 2;
    CHECK(uuie_sig_decode(buf, UUIE_SIG_LEN, &out) == -1);
    printf("codec ok\n");
}

/* ---------------- engine: charges ---------------- */

/* Shared fixture: the three circuits from the semantics examples.
 * Rate 0: setup 1000, cap 3, byte 2. Rate 1: setup 2500, cap 5,
 * byte 1. Hand-computed charges: 21620001000, 4505002500, 3001003.
 */
static uint64_t B_ID[3], B_ACCT[3], B_DUR[3], B_BW[3], B_BYTES[3];
static uint32_t B_TARIFF[3];
static int64_t  R_SETUP[2] = {1000, 2500};
static uint64_t R_CAP[2]   = {3, 5};
static uint64_t R_BYTE[2]  = {2, 1};
static uint32_t R_VER[2]   = {1, 7};

static void fixture_batch(uuie_batch *b, int64_t *charges) {
    static const uint64_t bw[3]    = {2000000ULL, 500000ULL, 999999ULL};
    static const uint64_t dur[3]   = {3600000000000ULL,
                                      1800000000000ULL,
                                      1000000000ULL};
    static const uint64_t bytes[3] = {10000000ULL, 5000000ULL, 3ULL};
    static const uint32_t tar[3]   = {0, 1, 0};
    for (int i = 0; i < 3; i++) {
        B_ID[i] = 1001 + (uint64_t)i;
        B_ACCT[i] = 501 + (uint64_t)i;
        B_BW[i] = bw[i]; B_DUR[i] = dur[i]; B_BYTES[i] = bytes[i];
        B_TARIFF[i] = tar[i];
    }
    memset(b, 0, sizeof *b);
    b->n = 3; b->cap = 3;
    b->circuit_id = B_ID; b->account_id = B_ACCT;
    b->duration_ns = B_DUR; b->bandwidth_bps = B_BW;
    b->usage_bytes = B_BYTES; b->tariff_id = B_TARIFF;
    b->charge_minor = charges;
}

static void fixture_rates(uuie_rates *r) {
    memset(r, 0, sizeof *r);
    r->n = 2; r->cap = 2;
    r->setup_minor = R_SETUP; r->capacity_rate = R_CAP;
    r->byte_rate = R_BYTE; r->version = R_VER;
}

static void test_charges(void) {
    static const int64_t want[3] = {21620001000LL, 4505002500LL,
                                    3001003LL};
    int64_t charges[3] = {0, 0, 0};
    uuie_batch b; uuie_rates r;
    fixture_batch(&b, charges);
    fixture_rates(&r);
    CHECK(uuie_compute_charges(&b, &r, 1000000000ULL, 1) == UUIE_OK);
    for (int i = 0; i < 3; i++) CHECK(charges[i] == want[i]);

    /* determinism: identical input, identical output bytes */
    int64_t charges2[3] = {0, 0, 0};
    uuie_batch b2;
    fixture_batch(&b2, charges2);
    CHECK(uuie_compute_charges(&b2, &r, 1000000000ULL, 1) == UUIE_OK);
    CHECK(memcmp(charges, charges2, sizeof charges) == 0);

    /* sum is order-independent (exact integer arithmetic) */
    uuie_i128 total;
    CHECK(uuie_sum_charges(&b, &total) == UUIE_OK);
    CHECK(total == (uuie_i128)26128004503LL);
    int64_t rev[3] = {charges[2], charges[1], charges[0]};
    b.charge_minor = rev;
    uuie_i128 total2;
    CHECK(uuie_sum_charges(&b, &total2) == UUIE_OK);
    CHECK(total2 == total);
    printf("charges ok\n");
}

static void test_round_div(void) {
    int64_t q;
    CHECK(uuie_round_div(150, 100, &q) == UUIE_OK && q == 2);
    CHECK(uuie_round_div(149, 100, &q) == UUIE_OK && q == 1);
    CHECK(uuie_round_div(50, 100, &q) == UUIE_OK && q == 1);
    CHECK(uuie_round_div(25, 100, &q) == UUIE_OK && q == 0);
    CHECK(uuie_round_div(0, 7, &q) == UUIE_OK && q == 0);
    CHECK(uuie_round_div(10, 0, &q) == UUIE_ERR_ARG);
    CHECK(uuie_round_div(-5, 10, &q) == UUIE_ERR_OVERFLOW);
    printf("round_div ok\n");
}

static void test_overflow(void) {
    uuie_rates r; fixture_rates(&r);
    /* product exceeds the 128-bit intermediate */
    uint64_t bw = UINT64_MAX, dur = UINT64_MAX, by = 0;
    uint32_t tar = 0; int64_t ch = 0;
    uuie_batch b; memset(&b, 0, sizeof b);
    b.n = 1; b.cap = 1;
    b.circuit_id = &bw; b.account_id = &bw; b.duration_ns = &dur;
    b.bandwidth_bps = &bw; b.usage_bytes = &by; b.tariff_id = &tar;
    b.charge_minor = &ch;
    uint64_t cap2[1] = {2}, byte2[1] = {0};
    int64_t setup2[1] = {0}; uint32_t ver2[1] = {1};
    uuie_rates r2; memset(&r2, 0, sizeof r2);
    r2.n = 1; r2.cap = 1; r2.setup_minor = setup2;
    r2.capacity_rate = cap2; r2.byte_rate = byte2; r2.version = ver2;
    CHECK(uuie_compute_charges(&b, &r2, 1000000000ULL, 1)
          == UUIE_ERR_OVERFLOW);
    /* quotient exceeds i64 */
    bw = 1000000000000ULL; dur = 1000000000000ULL;
    cap2[0] = 1000000ULL;
    CHECK(uuie_compute_charges(&b, &r2, 1000000000ULL, 1)
          == UUIE_ERR_OVERFLOW);
    /* tariff index out of range */
    tar = 9;
    bw = 1; dur = 1; cap2[0] = 1;
    CHECK(uuie_compute_charges(&b, &r2, 1000000000ULL, 1)
          == UUIE_ERR_INDEX);
    (void)r;
    printf("overflow ok\n");
}

/* ---------------- events and journal ---------------- */

#define LOG_CAP 64
static uuie_u128 IDS[LOG_CAP];
static uint8_t IDS_USED[LOG_CAP], SEQ_USED[LOG_CAP];
static uint64_t SEQ_CID[LOG_CAP], SEQ_NEXT[LOG_CAP];
#define JRN_CAP 32
static uuie_posting ROWS[JRN_CAP];

static uuie_event mk_ev(uuie_u128 id, uint64_t cid, uint64_t seq,
                        uint8_t kind) {
    uuie_event e; memset(&e, 0, sizeof e);
    e.event_id = id; e.circuit_id = cid; e.account_id = 501;
    e.sequence = seq; e.kind = kind;
    return e;
}

static void test_events_journal(void) {
    uuie_eventlog log;
    uuie_eventlog_init(&log, IDS, IDS_USED, SEQ_CID, SEQ_NEXT,
                       SEQ_USED, LOG_CAP);
    uuie_journal j;
    uuie_journal_init(&j, ROWS, JRN_CAP);

    /* one circuit: open, reserve, active, usage, release, settle */
    const uint8_t kinds[6] = {UUIE_EV_OPEN, UUIE_EV_RESERVE,
                              UUIE_EV_ACTIVE, UUIE_EV_USAGE,
                              UUIE_EV_RELEASE, UUIE_EV_SETTLE};
    for (int i = 0; i < 6; i++) {
        uuie_event e = mk_ev((uuie_u128)(100 + i), 1001,
                             (uint64_t)i, kinds[i]);
        CHECK(uuie_event_apply(&log, &e, NULL) == UUIE_OK);
    }
    CHECK(log.applied == 6);

    /* duplicate event id rejected, log unchanged */
    uuie_event dup = mk_ev((uuie_u128)102, 1001, 6, UUIE_EV_USAGE);
    CHECK(uuie_event_apply(&log, &dup, NULL) == UUIE_ERR_DUP_EVENT);
    CHECK(log.applied == 6);

    /* missing sequence detected, then recovered by replay */
    uuie_event gap = mk_ev((uuie_u128)200, 2002, 1, UUIE_EV_OPEN);
    uint64_t expected = 999;
    CHECK(uuie_event_apply(&log, &gap, &expected) == UUIE_ERR_SEQ);
    CHECK(expected == 0);
    uuie_event first = mk_ev((uuie_u128)199, 2002, 0, UUIE_EV_OPEN);
    CHECK(uuie_event_apply(&log, &first, NULL) == UUIE_OK);
    CHECK(uuie_event_apply(&log, &gap, NULL) == UUIE_OK);
    /* stale sequence rejected */
    uuie_event stale = mk_ev((uuie_u128)201, 2002, 0, UUIE_EV_USAGE);
    CHECK(uuie_event_apply(&log, &stale, NULL) == UUIE_ERR_SEQ);

    /* settlement of the first circuit's charge: balanced pair */
    CHECK(uuie_journal_settle(&j, (uuie_u128)105, 501, 900,
                              (uuie_u128)21620001000ULL) == UUIE_OK);
    CHECK(j.len == 2);
    CHECK(uuie_journal_balanced(&j));
    char buf[48];
    u128_str(j.total_debit, buf);
    CHECK(strcmp(buf, "21620001000") == 0);
    CHECK(j.total_debit == j.total_credit);

    /* idempotent retry: no duplicate posting */
    CHECK(uuie_journal_settle(&j, (uuie_u128)105, 501, 900,
                              (uuie_u128)21620001000ULL) == UUIE_OK);
    CHECK(j.len == 2);

    /* partial settlement recovery: only the debit half survived an
     * interruption; a retry completes the pair, exactly once */
    uuie_journal j2;
    uuie_journal_init(&j2, ROWS + 8, JRN_CAP - 8);
    j2.rows[0].event_id = (uuie_u128)777;
    j2.rows[0].account_id = 501;
    j2.rows[0].debit_minor = (uuie_u128)12500;
    j2.rows[0].credit_minor = 0;
    j2.len = 1;
    j2.total_debit = (uuie_u128)12500;
    CHECK(!uuie_journal_balanced(&j2));
    CHECK(uuie_journal_settle(&j2, (uuie_u128)777, 501, 900,
                              (uuie_u128)12500) == UUIE_OK);
    CHECK(j2.len == 2);
    CHECK(uuie_journal_balanced(&j2));

    /* compensating entry reverses a settlement */
    CHECK(uuie_journal_reverse(&j2, (uuie_u128)778, 501, 900,
                               (uuie_u128)12500) == UUIE_OK);
    CHECK(j2.len == 4);
    CHECK(uuie_journal_balanced(&j2));
    CHECK(j2.total_debit == (uuie_u128)25000);
    printf("events+journal ok\n");
}

static void test_rollover(void) {
    uint64_t prev = UINT64_MAX - 4, cur = 7;
    CHECK(uuie_counter_delta(cur, prev) == 12);
    CHECK(uuie_counter_delta(100, 40) == 60);
    printf("rollover ok\n");
}

/* ---------------- language ---------------- */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf); fclose(f); return NULL;
    }
    buf[n] = 0;
    fclose(f);
    return buf;
}

static void set_row(uuie_prog *p, int row, const char *field,
                    uuie_i128 v) {
    CHECK(uuie_prog_set(p, "Circuit", field, (uint32_t)row, v) == 0);
}

static void test_language(void) {
    char *src = read_file("programs/tariff.uuie");
    CHECK(src != NULL);
    if (src == NULL) return;
    uuie_prog *p = NULL;
    char err[200] = {0};
    CHECK(uuie_prog_load(src, &p, err, sizeof err) == 0);
    if (p == NULL) { printf("load error: %s\n", err); free(src); return; }
    CHECK(uuie_prog_vc_count(p) >= 5);
    int have_bounds_vc = 0;
    for (uint32_t i = 0; i < uuie_prog_vc_count(p); i++)
        if (strncmp(uuie_prog_vc(p, i), "bounds", 6) == 0)
            have_bounds_vc = 1;
    CHECK(have_bounds_vc);

    CHECK(uuie_prog_bind_dim(p, "N", 3) == 0);
    CHECK(uuie_prog_bind_dim(p, "T", 2) == 0);
    /* rates */
    CHECK(uuie_prog_set(p, "Rate", "setup_minor", 0, 1000) == 0);
    CHECK(uuie_prog_set(p, "Rate", "capacity_rate", 0, 3) == 0);
    CHECK(uuie_prog_set(p, "Rate", "byte_rate", 0, 2) == 0);
    CHECK(uuie_prog_set(p, "Rate", "version", 0, 1) == 0);
    CHECK(uuie_prog_set(p, "Rate", "setup_minor", 1, 2500) == 0);
    CHECK(uuie_prog_set(p, "Rate", "capacity_rate", 1, 5) == 0);
    CHECK(uuie_prog_set(p, "Rate", "byte_rate", 1, 1) == 0);
    CHECK(uuie_prog_set(p, "Rate", "version", 1, 7) == 0);
    /* circuits */
    static const int64_t bw[3] = {2000000, 500000, 999999};
    static const int64_t dur[3] = {3600000000000LL, 1800000000000LL,
                                   1000000000LL};
    static const int64_t by[3] = {10000000, 5000000, 3};
    static const int64_t tar[3] = {0, 1, 0};
    static const int64_t st_[3] = {5, 5, 6};   /* ACTIVE, ACTIVE, DRAINING */
    for (int i = 0; i < 3; i++) {
        set_row(p, i, "id", 1001 + i);
        set_row(p, i, "account", 501 + i);
        set_row(p, i, "bandwidth", bw[i]);
        set_row(p, i, "duration_ns", dur[i]);
        set_row(p, i, "bytes", by[i]);
        set_row(p, i, "tariff", tar[i]);
        set_row(p, i, "state", st_[i]);
    }
    CHECK(uuie_prog_run(p) == UUIE_OK);
    static const int64_t want[3] = {21620001000LL, 4505002500LL,
                                    3001003LL};
    for (int i = 0; i < 3; i++) {
        uuie_i128 v;
        CHECK(uuie_prog_col(p, "Charge", (uint32_t)i, &v) == 0);
        CHECK(v == (uuie_i128)want[i]);
    }
    uuie_i128 rev, res;
    CHECK(uuie_prog_scalar(p, "Revenue", &rev) == 0);
    CHECK(rev == (uuie_i128)26128004503LL);
    CHECK(uuie_prog_scalar(p, "Reserved", &res) == 0);
    CHECK(res == (uuie_i128)2500000);
    uint32_t alen;
    CHECK(uuie_prog_len(p, "Active", &alen) == 0 && alen == 2);
    uuie_prog_free(p);
    free(src);
    printf("language ok\n");
}

static void test_language_rejections(void) {
    char err[200] = {0};
    uuie_prog *p = NULL;
    /* unknown field */
    CHECK(uuie_prog_load("@C[N] { a : u64 }\n@X[N] = C.nope\n",
                         &p, err, sizeof err) == -1);
    /* cyclic definition */
    CHECK(uuie_prog_load("@C[N] { a : u64 }\n@X[N] = Y + C.a\n"
                         "@Y[N] = X + C.a\n",
                         &p, err, sizeof err) == -1);
    /* sum_checked overflow at run time */
    const char *ovf =
        "@C[N] { a : u64 }\nΣ T = sum_checked(C.a)\n";
    CHECK(uuie_prog_load(ovf, &p, err, sizeof err) == 0);
    if (p != NULL) {
        CHECK(uuie_prog_bind_dim(p, "N", 2) == 0);
        CHECK(uuie_prog_set(p, "C", "a", 0,
                            (uuie_i128)UINT64_MAX) == 0);
        CHECK(uuie_prog_set(p, "C", "a", 1,
                            (uuie_i128)UINT64_MAX) == 0);
        CHECK(uuie_prog_run(p) == UUIE_ERR_OVERFLOW);
        uuie_prog_free(p);
        p = NULL;
    }
    /* gather index out of range at run time */
    const char *gather =
        "K = 1\n"
        "@C[N] { t : u32 }\n"
        "@R[T] { s : i64 }\n"
        "@X[N] = R[t].s\n";
    CHECK(uuie_prog_load(gather, &p, err, sizeof err) == 0);
    if (p != NULL) {
        CHECK(uuie_prog_bind_dim(p, "N", 1) == 0);
        CHECK(uuie_prog_bind_dim(p, "T", 1) == 0);
        CHECK(uuie_prog_set(p, "R", "s", 0, 5) == 0);
        CHECK(uuie_prog_set(p, "C", "t", 0, 7) == 0);
        CHECK(uuie_prog_run(p) == UUIE_ERR_INDEX);
        uuie_prog_free(p);
        p = NULL;
    }
    printf("language rejections ok\n");
}

int main(void) {
    test_codec();
    test_round_div();
    test_charges();
    test_overflow();
    test_events_journal();
    test_rollover();
    test_language();
    test_language_rejections();
    printf("UUIE TESTS: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
