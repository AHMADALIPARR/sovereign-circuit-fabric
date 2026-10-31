/* Sovereign Circuit Fabric — UUIE-Array language (C11).
 *
 * Lexer, parser, typed intermediate representation, and evaluator
 * for the UUIE-Array constructs of spec/uuie_array.md, under the
 * formal semantics of spec/uuie_semantics.md. Supports: typed
 * column declarations (@Table[Dim] { field : type ... }), named
 * constants, selections (#Name = Table # (field cmp value)),
 * derived columns (@Name[Dim] = expr) with elementwise arithmetic
 * (⊙, *, +, -), round_div, tariff gathers (Rate[tariff].field),
 * selection columns (#Sel.field), checked reductions
 * (Σ Name = sum_checked(expr)), and validated transfers
 * (Source → Sink). Type checking emits verification conditions
 * (uuie_prog_vc): they are generated obligations, not discharged
 * proofs — discharge belongs to the Why3/Dafny phase.
 */
#ifndef UUIE_LANG_H
#define UUIE_LANG_H

#include "uuie.h"

typedef struct uuie_prog uuie_prog;

/* Parse and type-check. Returns 0 on success; on failure returns
 * -1 and writes a message into errbuf. */
int uuie_prog_load(const char *source, uuie_prog **out,
                   char *errbuf, size_t errcap);

/* Bind a dimension (e.g. "N") to a row count before setting data
 * or running. All tables declared with that dimension get that
 * many rows. Returns 0 on success. */
int uuie_prog_bind_dim(uuie_prog *p, const char *dim, uint32_t n);

/* Set one cell of a declared table column. The value is
 * range-checked against the field type. Returns 0 on success. */
int uuie_prog_set(uuie_prog *p, const char *table, const char *field,
                  uint32_t row, uuie_i128 v);

/* Read a cell of a declared table column. Returns 0 on success. */
int uuie_prog_get(uuie_prog *p, const char *table, const char *field,
                  uint32_t row, uuie_i128 *out);

/* Execute all statements in order. Returns UUIE_OK or the first
 * batch error (the program's computed state is then invalid). */
uuie_status uuie_prog_run(uuie_prog *p);

/* Read one element of a derived column (@Name[Dim] = ...). */
int uuie_prog_col(const uuie_prog *p, const char *name, uint32_t row,
                  uuie_i128 *out);

/* Read a reduced scalar (Σ Name = ...). */
int uuie_prog_scalar(const uuie_prog *p, const char *name,
                     uuie_i128 *out);

/* Number of elements in a derived column or selection. */
int uuie_prog_len(const uuie_prog *p, const char *name, uint32_t *out);

/* Verification conditions generated during type checking. */
uint32_t uuie_prog_vc_count(const uuie_prog *p);
const char *uuie_prog_vc(const uuie_prog *p, uint32_t i);

void uuie_prog_free(uuie_prog *p);

#endif /* UUIE_LANG_H */
