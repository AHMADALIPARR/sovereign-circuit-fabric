/* Sovereign Circuit Fabric — UUIE-Array language implementation.
 * See uuie_lang.h. Storage here is allocated (this is the compiler
 * and evaluator, not the engine hot path of spec section 6).
 */
#include "uuie_lang.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------- types and columns ---------------- */

typedef enum { LT_U8, LT_U16, LT_U32, LT_U64, LT_U128, LT_I64 } lty;

static int lty_is_signed(lty t) { return t == LT_I64; }

static int lty_fits(lty t, uuie_i128 v) {
    switch (t) {
        case LT_U8:  return v >= 0 && v <= 0xff;
        case LT_U16: return v >= 0 && v <= 0xffff;
        case LT_U32: return v >= 0 && v <= 0xffffffffLL;
        case LT_U64: return v >= 0 && (uuie_u128)v <= ~(uint64_t)0;
        case LT_U128: return v >= 0;
        case LT_I64: return v >= (uuie_i128)INT64_MIN &&
                            v <= (uuie_i128)INT64_MAX;
    }
    return 0;
}

typedef struct {
    char     name[32];
    lty      ty;
    uint32_t len;          /* rows (bound dimension size) */
    uuie_i128 *v;          /* exact values, one per row   */
} lcol;

typedef struct {
    char     name[32];
    char     dim[32];
    lcol    *fields;
    uint32_t nfields;
    uint32_t capfields;
} ltable;

typedef struct {
    char     name[32];
    int      table;        /* index into tables */
    uint32_t *rows;
    uint32_t count;
} lsel;

typedef struct {
    char      name[32];
    uuie_i128 value;
    int       is_const;    /* declared constant vs reduced scalar */
} lscalar;

/* ---------------- AST ---------------- */

enum { A_INT, A_NAME, A_COLREF, A_SELCOL, A_GATHER, A_BINOP,
       A_ROUNDDIV, A_SUM };
enum { OP_ADD, OP_SUB, OP_MUL, OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE };

typedef struct {
    int kind, op;
    int l, r;              /* child node indices, -1 if none */
    char a[32], b[32], c[32];
    uuie_i128 ival;
} anode;

enum { S_SELECT, S_COLDEF, S_REDUCE, S_TRANSFER, S_CONST };
typedef struct {
    int kind;
    char target[32];
    char table[32];        /* SELECT: source table */
    char field[32];        /* SELECT: predicate field */
    int  pred_op;
    uuie_i128 pred_val;
    int  expr;             /* COLDEF / REDUCE: AST root */
    char sink[32];         /* TRANSFER: destination name */
    uuie_i128 const_val;   /* CONST */
    int  dest_ty;          /* COLDEF/REDUCE: checked type (lty) */
} lstmt;

#define MAX_VC 128
struct uuie_prog {
    ltable  *tables;  uint32_t ntables, captables;
    lcol    *cols;    uint32_t ncols, capcols;      /* derived columns */
    lsel    *sels;    uint32_t nsels, capsels;
    lscalar *scalars; uint32_t nscalars, capscalars;
    anode   *nodes;   uint32_t nnodes, capnodes;
    lstmt   *stmts;   uint32_t nstmts, capstmts;
    char     dims[16][32];
    uint32_t dimvals[16];
    uint32_t ndims;
    char     vcs[MAX_VC][128];
    uint32_t nvcs;
    int      ran;
};

/* ---------------- small helpers ---------------- */

static void *xgrow(void *p, uint32_t *cap, uint32_t need, size_t sz) {
    if (need <= *cap) return p;
    uint32_t nc = *cap ? *cap * 2 : 8;
    while (nc < need) nc *= 2;
    void *np = realloc(p, (size_t)nc * sz);
    if (np == NULL) { fprintf(stderr, "uuie_lang: out of memory\n"); exit(1); }
    *cap = nc;
    return np;
}

static void namecpy(char *dst, const char *src) {
    size_t i = 0;
    for (; i < 31 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static ltable *find_table(uuie_prog *p, const char *name) {
    for (uint32_t i = 0; i < p->ntables; i++)
        if (strcmp(p->tables[i].name, name) == 0) return &p->tables[i];
    return NULL;
}
static lcol *table_field(ltable *t, const char *field) {
    if (t == NULL) return NULL;
    for (uint32_t i = 0; i < t->nfields; i++)
        if (strcmp(t->fields[i].name, field) == 0) return &t->fields[i];
    return NULL;
}
static lcol *find_col(uuie_prog *p, const char *name) {
    for (uint32_t i = 0; i < p->ncols; i++)
        if (strcmp(p->cols[i].name, name) == 0) return &p->cols[i];
    return NULL;
}
static lsel *find_sel(uuie_prog *p, const char *name) {
    for (uint32_t i = 0; i < p->nsels; i++)
        if (strcmp(p->sels[i].name, name) == 0) return &p->sels[i];
    return NULL;
}
static lscalar *find_scalar(uuie_prog *p, const char *name) {
    for (uint32_t i = 0; i < p->nscalars; i++)
        if (strcmp(p->scalars[i].name, name) == 0) return &p->scalars[i];
    return NULL;
}

static const struct { const char *name; int code; } STATE_NAMES[] = {
    {"FREE", 0}, {"REQUESTED", 1}, {"ADMITTED", 2}, {"RESERVED", 3},
    {"INSTALLED", 4}, {"ACTIVE", 5}, {"DRAINING", 6}, {"RELEASED", 7},
    {"FAILED", 8}
};
static int state_code(const char *name, int *out) {
    for (size_t i = 0; i < sizeof(STATE_NAMES)/sizeof(STATE_NAMES[0]); i++)
        if (strcmp(name, STATE_NAMES[i].name) == 0) {
            *out = STATE_NAMES[i].code; return 1;
        }
    return 0;
}

static void add_vc(uuie_prog *p, const char *kind, const char *detail) {
    if (p->nvcs >= MAX_VC) return;
    snprintf(p->vcs[p->nvcs], 128, "%s: %s", kind, detail);
    p->nvcs++;
}

/* ---------------- lexer ---------------- */

enum { T_EOF, T_AT, T_HASH, T_SIGMA, T_ODOT, T_ARROW,
       T_IDENT, T_INT, T_LB, T_RB, T_LC, T_RC, T_LP, T_RP,
       T_COLON, T_ASSIGN, T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE,
       T_PLUS, T_MINUS, T_STAR, T_COMMA, T_DOT };

typedef struct {
    const char *s;
    size_t pos;
    int tok;
    char ident[32];
    uuie_i128 ival;
    char err[160];
} lexer;

static void lex_next(lexer *lx) {
    const char *s = lx->s;
    while (s[lx->pos] == ' ' || s[lx->pos] == '\t' ||
           s[lx->pos] == '\n' || s[lx->pos] == '\r') lx->pos++;
    unsigned char c = (unsigned char)s[lx->pos];
    if (c == 0) { lx->tok = T_EOF; return; }
    /* multi-byte operators: Σ CE A3, ⊙ E2 8A 99, → E2 86 92 */
    if (c == 0xCE && (unsigned char)s[lx->pos+1] == 0xA3) {
        lx->pos += 2; lx->tok = T_SIGMA; return;
    }
    if (c == 0xE2 && (unsigned char)s[lx->pos+1] == 0x8A &&
        (unsigned char)s[lx->pos+2] == 0x99) {
        lx->pos += 3; lx->tok = T_ODOT; return;
    }
    if (c == 0xE2 && (unsigned char)s[lx->pos+1] == 0x86 &&
        (unsigned char)s[lx->pos+2] == 0x92) {
        lx->pos += 3; lx->tok = T_ARROW; return;
    }
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
        size_t i = 0;
        while ((s[lx->pos] >= 'A' && s[lx->pos] <= 'Z') ||
               (s[lx->pos] >= 'a' && s[lx->pos] <= 'z') ||
               (s[lx->pos] >= '0' && s[lx->pos] <= '9') ||
               s[lx->pos] == '_') {
            if (i < 31) lx->ident[i++] = s[lx->pos];
            lx->pos++;
        }
        lx->ident[i] = 0;
        lx->tok = T_IDENT;
        return;
    }
    if (c >= '0' && c <= '9') {
        uuie_u128 v = 0;
        if (c == '0' && (s[lx->pos+1] == 'x' || s[lx->pos+1] == 'X')) {
            lx->pos += 2;
            for (;;) {
                char h = s[lx->pos];
                int d;
                if (h >= '0' && h <= '9') d = h - '0';
                else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
                else break;
                v = v * 16u + (unsigned)d;
                lx->pos++;
            }
        } else {
            while (s[lx->pos] >= '0' && s[lx->pos] <= '9') {
                v = v * 10u + (unsigned)(s[lx->pos] - '0');
                lx->pos++;
            }
        }
        lx->ival = (uuie_i128)v;
        lx->tok = T_INT;
        return;
    }
    lx->pos++;
    switch (c) {
        case '@': lx->tok = T_AT; return;
        case '#': lx->tok = T_HASH; return;
        case '[': lx->tok = T_LB; return;
        case ']': lx->tok = T_RB; return;
        case '{': lx->tok = T_LC; return;
        case '}': lx->tok = T_RC; return;
        case '(': lx->tok = T_LP; return;
        case ')': lx->tok = T_RP; return;
        case ':': lx->tok = T_COLON; return;
        case ',': lx->tok = T_COMMA; return;
        case '.': lx->tok = T_DOT; return;
        case '+': lx->tok = T_PLUS; return;
        case '-': lx->tok = T_MINUS; return;
        case '*': lx->tok = T_STAR; return;
        case '=':
            if (s[lx->pos] == '=') { lx->pos++; lx->tok = T_EQ; }
            else lx->tok = T_ASSIGN;
            return;
        case '!':
            if (s[lx->pos] == '=') { lx->pos++; lx->tok = T_NE; return; }
            break;
        case '<':
            if (s[lx->pos] == '=') { lx->pos++; lx->tok = T_LE; }
            else lx->tok = T_LT;
            return;
        case '>':
            if (s[lx->pos] == '=') { lx->pos++; lx->tok = T_GE; }
            else lx->tok = T_GT;
            return;
    }
    snprintf(lx->err, sizeof lx->err,
             "lexer: unexpected character 0x%02x", c);
    lx->tok = T_EOF;
}

/* ---------------- parser ---------------- */

typedef struct {
    lexer lx;
    uuie_prog *p;
    char err[200];
} parser;

static int pfail(parser *ps, const char *msg) {
    if (ps->err[0] == 0) snprintf(ps->err, sizeof ps->err, "%s", msg);
    return -1;
}
static int expect(parser *ps, int tok, const char *what) {
    if (ps->lx.tok != tok) {
        char buf[96];
        snprintf(buf, sizeof buf, "parser: expected %s", what);
        return pfail(ps, buf);
    }
    lex_next(&ps->lx);
    return 0;
}
static int new_node(parser *ps, anode nd) {
    uuie_prog *p = ps->p;
    p->nodes = xgrow(p->nodes, &p->capnodes, p->nnodes + 1, sizeof(anode));
    p->nodes[p->nnodes] = nd;
    return (int)p->nnodes++;
}
static anode mknode(int kind) {
    anode n; memset(&n, 0, sizeof n);
    n.kind = kind; n.l = -1; n.r = -1;
    return n;
}

static int parse_expr(parser *ps);

static int parse_primary(parser *ps) {
    lexer *lx = &ps->lx;
    if (lx->tok == T_INT) {
        anode n = mknode(A_INT); n.ival = lx->ival;
        lex_next(lx);
        return new_node(ps, n);
    }
    if (lx->tok == T_LP) {
        lex_next(lx);
        int e = parse_expr(ps);
        if (e < 0) return -1;
        if (expect(ps, T_RP, "')'") < 0) return -1;
        return e;
    }
    if (lx->tok == T_HASH) {           /* #Sel.field */
        lex_next(lx);
        if (lx->tok != T_IDENT) return pfail(ps, "selection name");
        anode n = mknode(A_SELCOL);
        namecpy(n.a, lx->ident);
        lex_next(lx);
        if (expect(ps, T_DOT, "'.'") < 0) return -1;
        if (lx->tok != T_IDENT) return pfail(ps, "field name");
        namecpy(n.b, lx->ident);
        lex_next(lx);
        return new_node(ps, n);
    }
    if (lx->tok == T_IDENT) {
        char name[32];
        namecpy(name, lx->ident);
        if (strcmp(name, "sum_checked") == 0) {
            lex_next(lx);
            if (expect(ps, T_LP, "'('") < 0) return -1;
            int e = parse_expr(ps);
            if (e < 0) return -1;
            if (expect(ps, T_RP, "')'") < 0) return -1;
            anode n = mknode(A_SUM); n.l = e;
            return new_node(ps, n);
        }
        if (strcmp(name, "round_div") == 0) {
            lex_next(lx);
            if (expect(ps, T_LP, "'('") < 0) return -1;
            int x = parse_expr(ps);
            if (x < 0) return -1;
            if (expect(ps, T_COMMA, "','") < 0) return -1;
            int s = parse_expr(ps);
            if (s < 0) return -1;
            if (expect(ps, T_RP, "')'") < 0) return -1;
            anode n = mknode(A_ROUNDDIV); n.l = x; n.r = s;
            return new_node(ps, n);
        }
        lex_next(lx);
        if (lx->tok == T_DOT) {        /* Table.field */
            lex_next(lx);
            if (lx->tok != T_IDENT) return pfail(ps, "field name");
            anode n = mknode(A_COLREF);
            namecpy(n.a, name); namecpy(n.b, lx->ident);
            lex_next(lx);
            return new_node(ps, n);
        }
        if (lx->tok == T_LB) {         /* Table[idx].field */
            lex_next(lx);
            if (lx->tok != T_IDENT) return pfail(ps, "index column");
            anode n = mknode(A_GATHER);
            namecpy(n.a, name); namecpy(n.b, lx->ident);
            lex_next(lx);
            if (expect(ps, T_RB, "']'") < 0) return -1;
            if (expect(ps, T_DOT, "'.'") < 0) return -1;
            if (lx->tok != T_IDENT) return pfail(ps, "field name");
            namecpy(n.c, lx->ident);
            lex_next(lx);
            return new_node(ps, n);
        }
        anode n = mknode(A_NAME);      /* derived column / const / state */
        namecpy(n.a, name);
        return new_node(ps, n);
    }
    return pfail(ps, "parser: expected expression");
}

static int parse_term(parser *ps) {
    int l = parse_primary(ps);
    if (l < 0) return -1;
    while (ps->lx.tok == T_ODOT || ps->lx.tok == T_STAR) {
        lex_next(&ps->lx);
        int r = parse_primary(ps);
        if (r < 0) return -1;
        anode n = mknode(A_BINOP); n.op = OP_MUL; n.l = l; n.r = r;
        l = new_node(ps, n);
    }
    return l;
}

static int parse_expr(parser *ps) {
    int l = parse_term(ps);
    if (l < 0) return -1;
    while (ps->lx.tok == T_PLUS || ps->lx.tok == T_MINUS) {
        int op = (ps->lx.tok == T_PLUS) ? OP_ADD : OP_SUB;
        lex_next(&ps->lx);
        int r = parse_term(ps);
        if (r < 0) return -1;
        anode n = mknode(A_BINOP); n.op = op; n.l = l; n.r = r;
        l = new_node(ps, n);
    }
    return l;
}

static int parse_type(parser *ps, lty *out) {
    if (ps->lx.tok != T_IDENT) return pfail(ps, "type name");
    const char *t = ps->lx.ident;
    if (strcmp(t, "u8") == 0) *out = LT_U8;
    else if (strcmp(t, "u16") == 0) *out = LT_U16;
    else if (strcmp(t, "u32") == 0) *out = LT_U32;
    else if (strcmp(t, "u64") == 0) *out = LT_U64;
    else if (strcmp(t, "u128") == 0) *out = LT_U128;
    else if (strcmp(t, "i64") == 0) *out = LT_I64;
    else return pfail(ps, "unknown type");
    lex_next(&ps->lx);
    return 0;
}

static void add_stmt(uuie_prog *p, lstmt st) {
    p->stmts = xgrow(p->stmts, &p->capstmts, p->nstmts + 1, sizeof(lstmt));
    p->stmts[p->nstmts++] = st;
}

static int parse_program(parser *ps) {
    uuie_prog *p = ps->p;
    lexer *lx = &ps->lx;
    while (lx->tok != T_EOF) {
        if (lx->err[0]) return pfail(ps, lx->err);
        if (lx->tok == T_AT) {
            lex_next(lx);
            if (lx->tok != T_IDENT) return pfail(ps, "table/column name");
            char name[32]; namecpy(name, lx->ident);
            lex_next(lx);
            if (expect(ps, T_LB, "'['") < 0) return -1;
            if (lx->tok != T_IDENT) return pfail(ps, "dimension name");
            char dim[32]; namecpy(dim, lx->ident);
            lex_next(lx);
            if (expect(ps, T_RB, "']'") < 0) return -1;
            if (lx->tok == T_LC) {                 /* table declaration */
                lex_next(lx);
                p->tables = xgrow(p->tables, &p->captables,
                                  p->ntables + 1, sizeof(ltable));
                ltable *t = &p->tables[p->ntables++];
                memset(t, 0, sizeof *t);
                namecpy(t->name, name); namecpy(t->dim, dim);
                while (lx->tok != T_RC) {
                    if (lx->tok != T_IDENT)
                        return pfail(ps, "field name in declaration");
                    char fname[32]; namecpy(fname, lx->ident);
                    lex_next(lx);
                    if (expect(ps, T_COLON, "':'") < 0) return -1;
                    lty ty;
                    if (parse_type(ps, &ty) < 0) return -1;
                    t->fields = xgrow(t->fields, &t->capfields,
                                      t->nfields + 1, sizeof(lcol));
                    lcol *f = &t->fields[t->nfields++];
                    memset(f, 0, sizeof *f);
                    namecpy(f->name, fname); f->ty = ty;
                }
                lex_next(lx);                      /* consume '}' */
            } else if (lx->tok == T_ASSIGN) {      /* derived column */
                lex_next(lx);
                int e = parse_expr(ps);
                if (e < 0) return -1;
                lstmt st; memset(&st, 0, sizeof st);
                st.kind = S_COLDEF; st.expr = e;
                namecpy(st.target, name);
                namecpy(st.table, dim);            /* dim kept in table */
                add_stmt(p, st);
            } else {
                return pfail(ps, "expected '{' or '=' after @Name[Dim]");
            }
        } else if (lx->tok == T_HASH) {            /* selection def */
            lex_next(lx);
            if (lx->tok != T_IDENT) return pfail(ps, "selection name");
            lstmt st; memset(&st, 0, sizeof st);
            st.kind = S_SELECT;
            namecpy(st.target, lx->ident);
            lex_next(lx);
            if (expect(ps, T_ASSIGN, "'='") < 0) return -1;
            if (lx->tok != T_IDENT) return pfail(ps, "table name");
            namecpy(st.table, lx->ident);
            lex_next(lx);
            if (expect(ps, T_HASH, "'#'") < 0) return -1;
            if (expect(ps, T_LP, "'('") < 0) return -1;
            if (lx->tok != T_IDENT) return pfail(ps, "predicate field");
            namecpy(st.field, lx->ident);
            lex_next(lx);
            switch (lx->tok) {
                case T_EQ: st.pred_op = OP_EQ; break;
                case T_NE: st.pred_op = OP_NE; break;
                case T_LT: st.pred_op = OP_LT; break;
                case T_GT: st.pred_op = OP_GT; break;
                case T_LE: st.pred_op = OP_LE; break;
                case T_GE: st.pred_op = OP_GE; break;
                default: return pfail(ps, "comparison operator");
            }
            lex_next(lx);
            if (lx->tok == T_INT) { st.pred_val = lx->ival; lex_next(lx); }
            else if (lx->tok == T_IDENT) {
                int code;
                if (!state_code(lx->ident, &code))
                    return pfail(ps, "predicate value must be an "
                                     "integer or a state name");
                st.pred_val = code;
                lex_next(lx);
            } else return pfail(ps, "predicate value");
            if (expect(ps, T_RP, "')'") < 0) return -1;
            add_stmt(p, st);
        } else if (lx->tok == T_SIGMA) {           /* reduction */
            lex_next(lx);
            if (lx->tok != T_IDENT) return pfail(ps, "reduction name");
            lstmt st; memset(&st, 0, sizeof st);
            st.kind = S_REDUCE;
            namecpy(st.target, lx->ident);
            lex_next(lx);
            if (expect(ps, T_ASSIGN, "'='") < 0) return -1;
            int e = parse_expr(ps);
            if (e < 0) return -1;
            st.expr = e;
            add_stmt(p, st);
        } else if (lx->tok == T_IDENT) {           /* const or transfer */
            char name[32]; namecpy(name, lx->ident);
            lex_next(lx);
            if (lx->tok == T_ASSIGN) {
                lex_next(lx);
                if (lx->tok != T_INT)
                    return pfail(ps, "constant value must be an integer");
                lstmt st; memset(&st, 0, sizeof st);
                st.kind = S_CONST;
                namecpy(st.target, name);
                st.const_val = lx->ival;
                lex_next(lx);
                add_stmt(p, st);
            } else if (lx->tok == T_ARROW) {
                lex_next(lx);
                if (lx->tok != T_IDENT) return pfail(ps, "transfer sink");
                lstmt st; memset(&st, 0, sizeof st);
                st.kind = S_TRANSFER;
                namecpy(st.target, name);
                namecpy(st.sink, lx->ident);
                lex_next(lx);
                add_stmt(p, st);
            } else {
                return pfail(ps, "expected '=' or '->' after name");
            }
        } else {
            return pfail(ps, "parser: unexpected token at top level");
        }
    }
    return 0;
}

/* ---------------- type checking + VC generation ---------------- */

typedef struct {
    lty ty;
    int is_scalar;
} etype;

static ltable *table_with_dim(uuie_prog *p, const char *dim) {
    if (dim == NULL) return NULL;
    for (uint32_t i = 0; i < p->ntables; i++)
        if (strcmp(p->tables[i].dim, dim) == 0) return &p->tables[i];
    return NULL;
}

/* Find the statement-defined names an expression may reference. */
static int name_kind(uuie_prog *p, const char *name,
                     lcol **col_out, lscalar **scalar_out) {
    lcol *c = find_col(p, name);
    if (c != NULL) { *col_out = c; return 1; }
    for (uint32_t i = 0; i < p->nstmts; i++) {
        if (p->stmts[i].kind == S_COLDEF &&
            strcmp(p->stmts[i].target, name) == 0) return 2; /* later */
        if (p->stmts[i].kind == S_CONST &&
            strcmp(p->stmts[i].target, name) == 0) {
            *scalar_out = NULL; return 3;
        }
        if (p->stmts[i].kind == S_REDUCE &&
            strcmp(p->stmts[i].target, name) == 0) return 4;
    }
    return 0;
}

static int check_expr(uuie_prog *p, int ni, const char *ctx_dim,
                      int allow_sum, etype *out, char *err,
                      size_t errcap);

static int check_expr_inner(uuie_prog *p, int ni, const char *ctx_dim,
                            int allow_sum, etype *out, char *err,
                            size_t errcap) {
    anode *n = &p->nodes[ni];
    switch (n->kind) {
        case A_INT:
            out->ty = LT_U64; out->is_scalar = 1; return 0;
        case A_NAME: {
            lcol *c = NULL; lscalar *s = NULL;
            int k = name_kind(p, n->a, &c, &s);
            if (k == 1) { out->ty = c->ty; out->is_scalar = 0; return 0; }
            if (k == 3) { out->ty = LT_U64; out->is_scalar = 1; return 0; }
            if (k == 2 || k == 4) {
                /* forward reference to a coldef/reduce target: find
                 * its statement and check it on demand */
                for (uint32_t i = 0; i < p->nstmts; i++) {
                    lstmt *st = &p->stmts[i];
                    if ((st->kind == S_COLDEF || st->kind == S_REDUCE) &&
                        strcmp(st->target, n->a) == 0) {
                        etype sub;
                        const char *cd = (st->kind == S_COLDEF)
                                       ? st->table : ctx_dim;
                        if (check_expr(p, st->expr, cd,
                                       st->kind == S_REDUCE,
                                       &sub, err, errcap) < 0)
                            return -1;
                        out->ty = sub.ty;
                        out->is_scalar = (st->kind == S_REDUCE);
                        return 0;
                    }
                }
            }
            snprintf(err, errcap, "unknown name '%s'", n->a);
            return -1;
        }
        case A_COLREF: {
            ltable *t = find_table(p, n->a);
            lcol *f = table_field(t, n->b);
            if (f == NULL) {
                snprintf(err, errcap, "unknown column '%s.%s'",
                         n->a, n->b);
                return -1;
            }
            out->ty = f->ty; out->is_scalar = 0; return 0;
        }
        case A_SELCOL: {
            lsel *s = NULL;
            for (uint32_t i = 0; i < p->nstmts; i++)
                if (p->stmts[i].kind == S_SELECT &&
                    strcmp(p->stmts[i].target, n->a) == 0) {
                    ltable *t = find_table(p, p->stmts[i].table);
                    lcol *f = table_field(t, n->b);
                    if (f == NULL) {
                        snprintf(err, errcap,
                                 "unknown column '#%s.%s'", n->a, n->b);
                        return -1;
                    }
                    out->ty = f->ty; out->is_scalar = 0; return 0;
                }
            (void)s;
            snprintf(err, errcap, "unknown selection '#%s'", n->a);
            return -1;
        }
        case A_GATHER: {
            ltable *t = find_table(p, n->a);
            lcol *f = table_field(t, n->c);
            if (f == NULL) {
                snprintf(err, errcap, "unknown column '%s[].%s'",
                         n->a, n->c);
                return -1;
            }
            ltable *ctx = table_with_dim(p, ctx_dim);
            if (ctx == NULL || table_field(ctx, n->b) == NULL) {
                snprintf(err, errcap,
                         "gather index '%s' is not a column of the "
                         "context table", n->b);
                return -1;
            }
            char d[128];
            snprintf(d, sizeof d, "every %s index < %s rows (gather %s)",
                     n->b, n->a, n->c);
            add_vc(p, "bounds", d);
            out->ty = f->ty; out->is_scalar = 0; return 0;
        }
        case A_BINOP: {
            etype lt, rt;
            if (check_expr(p, n->l, ctx_dim, allow_sum, &lt,
                           err, errcap) < 0) return -1;
            if (check_expr(p, n->r, ctx_dim, allow_sum, &rt,
                           err, errcap) < 0) return -1;
            out->is_scalar = lt.is_scalar && rt.is_scalar;
            out->ty = (lty_is_signed(lt.ty) || lty_is_signed(rt.ty))
                      ? LT_I64 : LT_U64;
            add_vc(p, "overflow",
                   "elementwise result exact in the 128-bit "
                   "intermediate; destination fit is a separate "
                   "obligation");
            return 0;
        }
        case A_ROUNDDIV: {
            etype xt, st2;
            if (check_expr(p, n->l, ctx_dim, allow_sum, &xt,
                           err, errcap) < 0) return -1;
            if (check_expr(p, n->r, ctx_dim, allow_sum, &st2,
                           err, errcap) < 0) return -1;
            if (!st2.is_scalar) {
                snprintf(err, errcap,
                         "round_div scale must be a scalar");
                return -1;
            }
            add_vc(p, "range", "round_div result fits i64");
            out->ty = LT_I64; out->is_scalar = xt.is_scalar; return 0;
        }
        case A_SUM: {
            if (!allow_sum) {
                snprintf(err, errcap,
                         "sum_checked is only allowed in a Σ "
                         "reduction");
                return -1;
            }
            etype xt;
            if (check_expr(p, n->l, ctx_dim, 0, &xt, err, errcap) < 0)
                return -1;
            if (xt.is_scalar) {
                snprintf(err, errcap,
                         "sum_checked needs a column operand");
                return -1;
            }
            add_vc(p, "range",
                   "sum_checked total fits the element type");
            out->ty = xt.ty; out->is_scalar = 1; return 0;
        }
    }
    snprintf(err, errcap, "internal: bad node kind");
    return -1;
}

static int check_depth = 0;
static int check_expr(uuie_prog *p, int ni, const char *ctx_dim,
                      int allow_sum, etype *out, char *err,
                      size_t errcap) {
    if (++check_depth > 64) {
        check_depth--;
        snprintf(err, errcap, "definition cycle or nesting too deep");
        return -1;
    }
    int rc = check_expr_inner(p, ni, ctx_dim, allow_sum, out, err, errcap);
    check_depth--;
    return rc;
}

static int typecheck(uuie_prog *p, char *err, size_t errcap) {
    for (uint32_t i = 0; i < p->nstmts; i++) {
        lstmt *st = &p->stmts[i];
        etype t;
        switch (st->kind) {
            case S_SELECT: {
                ltable *tb = find_table(p, st->table);
                if (tb == NULL || table_field(tb, st->field) == NULL) {
                    snprintf(err, errcap,
                             "selection '%s': unknown table/field",
                             st->target);
                    return -1;
                }
                break;
            }
            case S_COLDEF:
                if (check_expr(p, st->expr, st->table, 0, &t,
                               err, errcap) < 0) return -1;
                st->dest_ty = (int)t.ty;
                {
                    char d[128];
                    snprintf(d, sizeof d,
                             "every element of %s fits its type",
                             st->target);
                    add_vc(p, "range", d);
                }
                break;
            case S_REDUCE:
                if (check_expr(p, st->expr, NULL, 1, &t,
                               err, errcap) < 0) return -1;
                st->dest_ty = (int)t.ty;
                break;
            case S_TRANSFER: {
                lcol *c = NULL; lscalar *s = NULL;
                if (name_kind(p, st->target, &c, &s) == 0) {
                    snprintf(err, errcap,
                             "transfer source '%s' is unknown",
                             st->target);
                    return -1;
                }
                {
                    char d[128];
                    snprintf(d, sizeof d,
                             "batch error mask empty at transfer "
                             "of %s to %s", st->target, st->sink);
                    add_vc(p, "validated", d);
                }
                break;
            }
            case S_CONST:
                break;
        }
    }
    return 0;
}

/* ---------------- evaluation ---------------- */

typedef struct {
    int        is_col;
    lty        ty;
    uint32_t   len;
    uuie_i128 *v;        /* owned when is_col */
    uuie_i128  scalar;
} evalval;

static void ev_free(evalval *e) { if (e->is_col) free(e->v); }

static uuie_status eval_expr(uuie_prog *p, int ni, ltable *ctx,
                             evalval *out);
static uuie_status eval_u128_expr(uuie_prog *p, int ni, ltable *ctx,
                                  uint32_t len, uuie_u128 **out);

static lcol *resolve_name_col(uuie_prog *p, const char *name) {
    return find_col(p, name);
}

static uuie_status ev_column_values(lcol *f, evalval *out) {
    out->is_col = 1; out->ty = f->ty; out->len = f->len;
    out->v = malloc(sizeof(uuie_i128) * (f->len ? f->len : 1));
    if (out->v == NULL) return UUIE_ERR_CAPACITY;
    for (uint32_t i = 0; i < f->len; i++) out->v[i] = f->v[i];
    return UUIE_OK;
}

/* Exact unsigned evaluation for round_div numerators: column refs,
 * selection columns, gathers, derived columns, integer literals,
 * and + / ⊙ over them. Anything else is UUIE_ERR_ARG. */
static uuie_status eval_u128_expr(uuie_prog *p, int ni, ltable *ctx,
                                  uint32_t len, uuie_u128 **out) {
    anode *n = &p->nodes[ni];
    uuie_u128 *buf = malloc(sizeof(uuie_u128) * (len ? len : 1));
    if (buf == NULL) return UUIE_ERR_CAPACITY;
    switch (n->kind) {
        case A_INT:
            for (uint32_t i = 0; i < len; i++)
                buf[i] = (uuie_u128)n->ival;
            break;
        case A_COLREF: {
            lcol *f = table_field(find_table(p, n->a), n->b);
            if (f == NULL || f->len != len) { free(buf); return UUIE_ERR_ARG; }
            for (uint32_t i = 0; i < len; i++) {
                if (f->v[i] < 0) { free(buf); return UUIE_ERR_OVERFLOW; }
                buf[i] = (uuie_u128)f->v[i];
            }
            break;
        }
        case A_NAME: {
            lcol *f = resolve_name_col(p, n->a);
            if (f == NULL || f->len != len) { free(buf); return UUIE_ERR_ARG; }
            for (uint32_t i = 0; i < len; i++) {
                if (f->v[i] < 0) { free(buf); return UUIE_ERR_OVERFLOW; }
                buf[i] = (uuie_u128)f->v[i];
            }
            break;
        }
        case A_GATHER: {
            ltable *t = find_table(p, n->a);
            lcol *f = table_field(t, n->c);
            lcol *idx = table_field(ctx, n->b);
            if (f == NULL || idx == NULL || idx->len != len) {
                free(buf); return UUIE_ERR_ARG;
            }
            for (uint32_t i = 0; i < len; i++) {
                uuie_i128 iv = idx->v[i];
                if (iv < 0 || iv >= (uuie_i128)f->len) {
                    free(buf); return UUIE_ERR_INDEX;
                }
                if (f->v[(uint32_t)iv] < 0) {
                    free(buf); return UUIE_ERR_OVERFLOW;
                }
                buf[i] = (uuie_u128)f->v[(uint32_t)iv];
            }
            break;
        }
        case A_BINOP: {
            uuie_u128 *lb = NULL, *rb = NULL;
            uuie_status st = eval_u128_expr(p, n->l, ctx, len, &lb);
            if (st == UUIE_OK) st = eval_u128_expr(p, n->r, ctx, len, &rb);
            if (st != UUIE_OK) { free(buf); free(lb); free(rb); return st; }
            for (uint32_t i = 0; i < len; i++) {
                if (n->op == OP_MUL) {
                    if (lb[i] != 0 && rb[i] > ~(uuie_u128)0 / lb[i]) {
                        free(buf); free(lb); free(rb);
                        return UUIE_ERR_OVERFLOW;
                    }
                    buf[i] = lb[i] * rb[i];
                } else if (n->op == OP_ADD) {
                    if (rb[i] > ~(uuie_u128)0 - lb[i]) {
                        free(buf); free(lb); free(rb);
                        return UUIE_ERR_OVERFLOW;
                    }
                    buf[i] = lb[i] + rb[i];
                } else {
                    free(buf); free(lb); free(rb);
                    return UUIE_ERR_ARG;
                }
            }
            free(lb); free(rb);
            break;
        }
        default:
            free(buf);
            return UUIE_ERR_ARG;
    }
    *out = buf;
    return UUIE_OK;
}

static uuie_status eval_expr(uuie_prog *p, int ni, ltable *ctx,
                             evalval *out) {
    anode *n = &p->nodes[ni];
    memset(out, 0, sizeof *out);
    switch (n->kind) {
        case A_INT:
            out->is_col = 0; out->ty = LT_U64; out->scalar = n->ival;
            return UUIE_OK;
        case A_NAME: {
            lscalar *s = find_scalar(p, n->a);
            if (s != NULL) {
                out->is_col = 0; out->ty = LT_I64;
                out->scalar = s->value;
                return UUIE_OK;
            }
            lcol *f = resolve_name_col(p, n->a);
            if (f == NULL) return UUIE_ERR_ARG;
            return ev_column_values(f, out);
        }
        case A_COLREF: {
            lcol *f = table_field(find_table(p, n->a), n->b);
            if (f == NULL) return UUIE_ERR_ARG;
            return ev_column_values(f, out);
        }
        case A_SELCOL: {
            lsel *s = find_sel(p, n->a);
            if (s == NULL) return UUIE_ERR_ARG;
            lcol *f = table_field(&p->tables[s->table], n->b);
            if (f == NULL) return UUIE_ERR_ARG;
            out->is_col = 1; out->ty = f->ty; out->len = s->count;
            out->v = malloc(sizeof(uuie_i128) * (s->count ? s->count : 1));
            if (out->v == NULL) return UUIE_ERR_CAPACITY;
            for (uint32_t i = 0; i < s->count; i++)
                out->v[i] = f->v[s->rows[i]];
            return UUIE_OK;
        }
        case A_GATHER: {
            ltable *t = find_table(p, n->a);
            lcol *f = table_field(t, n->c);
            lcol *idx = table_field(ctx, n->b);
            if (f == NULL || idx == NULL) return UUIE_ERR_ARG;
            out->is_col = 1; out->ty = f->ty; out->len = idx->len;
            out->v = malloc(sizeof(uuie_i128) * (idx->len ? idx->len : 1));
            if (out->v == NULL) return UUIE_ERR_CAPACITY;
            for (uint32_t i = 0; i < idx->len; i++) {
                uuie_i128 iv = idx->v[i];
                if (iv < 0 || iv >= (uuie_i128)f->len) {
                    ev_free(out);
                    return UUIE_ERR_INDEX;
                }
                out->v[i] = f->v[(uint32_t)iv];
            }
            return UUIE_OK;
        }
        case A_BINOP: {
            evalval L, R;
            uuie_status st = eval_expr(p, n->l, ctx, &L);
            if (st != UUIE_OK) return st;
            st = eval_expr(p, n->r, ctx, &R);
            if (st != UUIE_OK) { ev_free(&L); return st; }
            int sgn = lty_is_signed(L.ty) || lty_is_signed(R.ty);
            out->ty = sgn ? LT_I64 : LT_U64;
            if (!L.is_col && !R.is_col) {
                uuie_i128 a = L.scalar, b = R.scalar, r = 0;
                int ovf = 0;
                if (n->op == OP_ADD) ovf = __builtin_add_overflow(a, b, &r);
                else if (n->op == OP_SUB) ovf = __builtin_sub_overflow(a, b, &r);
                else if (n->op == OP_MUL) ovf = __builtin_mul_overflow(a, b, &r);
                else { ev_free(&L); ev_free(&R); return UUIE_ERR_ARG; }
                out->is_col = 0; out->scalar = r;
                ev_free(&L); ev_free(&R);
                return ovf ? UUIE_ERR_OVERFLOW : UUIE_OK;
            }
            uint32_t len = L.is_col ? L.len : R.len;
            if (L.is_col && R.is_col && L.len != R.len) {
                ev_free(&L); ev_free(&R);
                return UUIE_ERR_ARG;
            }
            out->is_col = 1; out->len = len;
            out->v = malloc(sizeof(uuie_i128) * (len ? len : 1));
            if (out->v == NULL) { ev_free(&L); ev_free(&R); return UUIE_ERR_CAPACITY; }
            for (uint32_t i = 0; i < len; i++) {
                uuie_i128 a = L.is_col ? L.v[i] : L.scalar;
                uuie_i128 b = R.is_col ? R.v[i] : R.scalar;
                uuie_i128 r = 0;
                int ovf = 0;
                if (n->op == OP_ADD) ovf = __builtin_add_overflow(a, b, &r);
                else if (n->op == OP_SUB) ovf = __builtin_sub_overflow(a, b, &r);
                else if (n->op == OP_MUL) {
                    if (!sgn && a >= 0 && b >= 0) {
                        uuie_u128 prod = (uuie_u128)a * (uuie_u128)b;
                        const uuie_u128 I128_MAX =
                            (uuie_u128)(((uuie_u128)1 << 127) - 1);
                        if (prod > I128_MAX) ovf = 1;
                        else r = (uuie_i128)prod;
                    } else ovf = __builtin_mul_overflow(a, b, &r);
                }
                else { ev_free(&L); ev_free(&R); ev_free(out); return UUIE_ERR_ARG; }
                if (ovf) { ev_free(&L); ev_free(&R); ev_free(out); return UUIE_ERR_OVERFLOW; }
                out->v[i] = r;
            }
            ev_free(&L); ev_free(&R);
            return UUIE_OK;
        }
        case A_ROUNDDIV: {
            evalval S;
            uuie_status st = eval_expr(p, n->r, ctx, &S);
            if (st != UUIE_OK) return st;
            if (S.is_col || S.scalar <= 0 ||
                S.scalar > (uuie_i128)UINT64_MAX) {
                ev_free(&S);
                return UUIE_ERR_ARG;
            }
            uint64_t scale = (uint64_t)S.scalar;
            ev_free(&S);
            /* determine the numerator length from its shape */
            evalval probe;
            anode *x = &p->nodes[n->l];
            uint32_t len;
            if (x->kind == A_COLREF) {
                lcol *f = table_field(find_table(p, x->a), x->b);
                if (f == NULL) return UUIE_ERR_ARG;
                len = f->len;
            } else if (x->kind == A_NAME) {
                lcol *f = resolve_name_col(p, x->a);
                if (f == NULL) return UUIE_ERR_ARG;
                len = f->len;
            } else if (ctx != NULL && ctx->nfields > 0) {
                len = ctx->fields[0].len;
            } else return UUIE_ERR_ARG;
            (void)probe;
            uuie_u128 *nums = NULL;
            st = eval_u128_expr(p, n->l, ctx, len, &nums);
            if (st != UUIE_OK) return st;
            out->is_col = 1; out->ty = LT_I64; out->len = len;
            out->v = malloc(sizeof(uuie_i128) * (len ? len : 1));
            if (out->v == NULL) { free(nums); return UUIE_ERR_CAPACITY; }
            for (uint32_t i = 0; i < len; i++) {
                int64_t q;
                st = uuie_round_div_u128(nums[i], scale, &q);
                if (st != UUIE_OK) {
                    free(nums); ev_free(out);
                    return st;
                }
                out->v[i] = q;
            }
            free(nums);
            return UUIE_OK;
        }
        case A_SUM: {
            evalval X;
            uuie_status st = eval_expr(p, n->l, ctx, &X);
            if (st != UUIE_OK) return st;
            if (!X.is_col) { ev_free(&X); return UUIE_ERR_ARG; }
            uuie_i128 acc = 0;
            for (uint32_t i = 0; i < X.len; i++) acc += X.v[i];
            lty ety = X.ty;
            ev_free(&X);
            if (!lty_fits(ety, acc)) return UUIE_ERR_OVERFLOW;
            out->is_col = 0; out->ty = ety; out->scalar = acc;
            return UUIE_OK;
        }
    }
    return UUIE_ERR_ARG;
}

static int cmp_pred(int op, uuie_i128 a, uuie_i128 b) {
    switch (op) {
        case OP_EQ: return a == b;
        case OP_NE: return a != b;
        case OP_LT: return a < b;
        case OP_GT: return a > b;
        case OP_LE: return a <= b;
        case OP_GE: return a >= b;
    }
    return 0;
}

/* ---------------- public API ---------------- */

int uuie_prog_load(const char *source, uuie_prog **out,
                   char *errbuf, size_t errcap) {
    uuie_prog *p = calloc(1, sizeof *p);
    if (p == NULL) return -1;
    parser ps; memset(&ps, 0, sizeof ps);
    ps.p = p;
    ps.lx.s = source; ps.lx.pos = 0; ps.lx.tok = T_EOF;
    lex_next(&ps.lx);
    if (parse_program(&ps) < 0) {
        snprintf(errbuf, errcap, "%s", ps.err[0] ? ps.err : "parse error");
        uuie_prog_free(p);
        return -1;
    }
    if (typecheck(p, errbuf, errcap) < 0) {
        uuie_prog_free(p);
        return -1;
    }
    *out = p;
    return 0;
}

int uuie_prog_bind_dim(uuie_prog *p, const char *dim, uint32_t n) {
    int found = 0;
    for (uint32_t i = 0; i < p->ndims; i++)
        if (strcmp(p->dims[i], dim) == 0) { p->dimvals[i] = n; found = 1; }
    if (!found) {
        if (p->ndims >= 16) return -1;
        namecpy(p->dims[p->ndims], dim);
        p->dimvals[p->ndims] = n;
        p->ndims++;
    }
    for (uint32_t i = 0; i < p->ntables; i++) {
        ltable *t = &p->tables[i];
        if (strcmp(t->dim, dim) != 0) continue;
        for (uint32_t j = 0; j < t->nfields; j++) {
            lcol *f = &t->fields[j];
            free(f->v);
            f->v = calloc(n ? n : 1, sizeof(uuie_i128));
            if (f->v == NULL) return -1;
            f->len = n;
        }
    }
    return 0;
}

int uuie_prog_set(uuie_prog *p, const char *table, const char *field,
                  uint32_t row, uuie_i128 v) {
    lcol *f = table_field(find_table(p, table), field);
    if (f == NULL || row >= f->len) return -1;
    if (!lty_fits(f->ty, v)) return -1;
    f->v[row] = v;
    return 0;
}

int uuie_prog_get(uuie_prog *p, const char *table, const char *field,
                  uint32_t row, uuie_i128 *out) {
    lcol *f = table_field(find_table(p, table), field);
    if (f == NULL || row >= f->len) return -1;
    *out = f->v[row];
    return 0;
}

uuie_status uuie_prog_run(uuie_prog *p) {
    for (uint32_t i = 0; i < p->nstmts; i++) {
        lstmt *st = &p->stmts[i];
        switch (st->kind) {
            case S_CONST: {
                p->scalars = xgrow(p->scalars, &p->capscalars,
                                   p->nscalars + 1, sizeof(lscalar));
                lscalar *s = &p->scalars[p->nscalars++];
                memset(s, 0, sizeof *s);
                namecpy(s->name, st->target);
                s->value = st->const_val;
                s->is_const = 1;
                break;
            }
            case S_SELECT: {
                ltable *t = find_table(p, st->table);
                lcol *f = table_field(t, st->field);
                if (f == NULL) return UUIE_ERR_ARG;
                p->sels = xgrow(p->sels, &p->capsels,
                                p->nsels + 1, sizeof(lsel));
                lsel *s = &p->sels[p->nsels++];
                memset(s, 0, sizeof *s);
                namecpy(s->name, st->target);
                s->table = (int)(t - p->tables);
                s->rows = malloc(sizeof(uint32_t) * (f->len ? f->len : 1));
                if (s->rows == NULL) return UUIE_ERR_CAPACITY;
                s->count = 0;
                for (uint32_t r = 0; r < f->len; r++)
                    if (cmp_pred(st->pred_op, f->v[r], st->pred_val))
                        s->rows[s->count++] = r;
                break;
            }
            case S_COLDEF: {
                ltable *ctx = table_with_dim(p, st->table);
                evalval v;
                uuie_status rc = eval_expr(p, st->expr, ctx, &v);
                if (rc != UUIE_OK) return rc;
                if (!v.is_col) { ev_free(&v); return UUIE_ERR_ARG; }
                /* destination type was fixed at type-check time */
                lty dest = (lty)st->dest_ty;
                for (uint32_t r = 0; r < v.len; r++)
                    if (!lty_fits(dest, v.v[r])) {
                        ev_free(&v);
                        return UUIE_ERR_OVERFLOW;
                    }
                p->cols = xgrow(p->cols, &p->capcols,
                                p->ncols + 1, sizeof(lcol));
                lcol *c = &p->cols[p->ncols++];
                memset(c, 0, sizeof *c);
                namecpy(c->name, st->target);
                c->ty = dest; c->len = v.len; c->v = v.v;
                break;
            }
            case S_REDUCE: {
                evalval v;
                uuie_status rc = eval_expr(p, st->expr, NULL, &v);
                if (rc != UUIE_OK) return rc;
                if (v.is_col) { ev_free(&v); return UUIE_ERR_ARG; }
                p->scalars = xgrow(p->scalars, &p->capscalars,
                                   p->nscalars + 1, sizeof(lscalar));
                lscalar *s = &p->scalars[p->nscalars++];
                memset(s, 0, sizeof *s);
                namecpy(s->name, st->target);
                s->value = v.scalar;
                s->is_const = 0;
                break;
            }
            case S_TRANSFER: {
                if (find_col(p, st->target) == NULL &&
                    find_scalar(p, st->target) == NULL)
                    return UUIE_ERR_ARG;
                break;  /* validated: run reached here error-free */
            }
        }
    }
    p->ran = 1;
    return UUIE_OK;
}

int uuie_prog_col(const uuie_prog *p, const char *name, uint32_t row,
                  uuie_i128 *out) {
    lcol *c = find_col((uuie_prog *)p, name);
    if (c == NULL || row >= c->len) return -1;
    *out = c->v[row];
    return 0;
}

int uuie_prog_scalar(const uuie_prog *p, const char *name,
                     uuie_i128 *out) {
    lscalar *s = find_scalar((uuie_prog *)p, name);
    if (s == NULL) return -1;
    *out = s->value;
    return 0;
}

int uuie_prog_len(const uuie_prog *p, const char *name, uint32_t *out) {
    lcol *c = find_col((uuie_prog *)p, name);
    if (c != NULL) { *out = c->len; return 0; }
    lsel *s = find_sel((uuie_prog *)p, name);
    if (s != NULL) { *out = s->count; return 0; }
    return -1;
}

uint32_t uuie_prog_vc_count(const uuie_prog *p) { return p->nvcs; }
const char *uuie_prog_vc(const uuie_prog *p, uint32_t i) {
    return i < p->nvcs ? p->vcs[i] : NULL;
}

void uuie_prog_free(uuie_prog *p) {
    if (p == NULL) return;
    for (uint32_t i = 0; i < p->ntables; i++) {
        for (uint32_t j = 0; j < p->tables[i].nfields; j++)
            free(p->tables[i].fields[j].v);
        free(p->tables[i].fields);
    }
    free(p->tables);
    for (uint32_t i = 0; i < p->ncols; i++) free(p->cols[i].v);
    free(p->cols);
    for (uint32_t i = 0; i < p->nsels; i++) free(p->sels[i].rows);
    free(p->sels);
    free(p->scalars);
    free(p->nodes);
    free(p->stmts);
    free(p);
}
