/* SPDX-License-Identifier: MIT */
#include "hc_rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const hc_rule_desc RULES[] = {
    {"HC-E001","LOCK_POSIX","POSIX mutex/rwlock/cond/spin/barrier/semaphore primitive"},
    {"HC-E002","LOCK_C11_OR_OS","C11 <threads.h> lock, flock/lockf, or SYS_futex/FUTEX_LOCK in component"},
    {"HC-E003","LOCK_ATOMIC_SPIN","hidden atomic_flag / CAS-retry / exchange spinlock"},
    {"HC-E004","LOCK_ASM_OR_PAUSE","inline-asm lock/spin/pause bypass"},
    {"HC-E005","LOCK_VOLATILE_WAIT","volatile Dekker/Peterson busy-wait"},
    {"HC-E006","LOCK_HIDDEN_THREAD","unauthorized thread/process creation in component"},
    {"HC-E007","PHASE_TRANSITION","illegal beat transition (PULL invoking COMMIT, skipping BARRIER)"},
    {"HC-E008","PHASE_WRITE_PULL","write to shared/file-scope mutable state inside PULL/BARRIER"},
    {"HC-E009","PHASE_UNPUBLISHED","reading/mutating unpublished staging or hs_hs internals"},
    {"HC-E010","PHASE_COMMIT_LEAK","posting commit/control events from PULL/BARRIER"},
    {"HC-E011","PHASE_CALL_LEAK","transitive call-graph leak to shared write / COMMIT mutator"},
    {"HC-E012","PHASE_PTR_ESCAPE","phase-scoped/stack/shard pointer escaping into global"},
    {"HC-E013","ALIAS_CONST_CAST","casting away const on a readonly snapshot"},
    {"HC-E014","ALIAS_UNION_PUN","union / uintptr_t punning bypassing const immutability"},
    {"HC-E015","ALIAS_SHARD_OVERLAP","cross-shard write overlap"},
    {"HC-E016","LIVENESS_LOOP","statically unbounded loop / backward goto in phase kernel"},
    {"HC-E017","LIVENESS_RECURSION","direct/mutual recursion reachable from a phase kernel"},
    {"HC-E018","LIVENESS_BLOCKING","blocking sleep/yield/IO inside a phase kernel"},
    {"HC-E019","WATCHDOG_TAMPER","tampering with watchdog ceiling_ns/frozen"},
    {"HC-E020","DET_WALL_CLOCK","wall-clock/monotonic time read in component payload"},
    {"HC-E021","DET_CYCLE_COUNTER","raw hardware cycle counter in deterministic code"},
    {"HC-E022","DET_AMBIENT_RNG","ambient entropy / unseeded RNG"},
    {"HC-E023","DET_ORDER_LEAK","pointer/tid/arrival-order leak into output/ledger"},
    {"HC-E024","SMUGGLE_DLOPEN","dynamic-loader symbol smuggling (dlopen/dlsym/split-hex)"},
    {"HC-E025","SMUGGLE_MACRO","preprocessor ## token-pasting smuggle / #pragma suppression"},
    {"HC-E026","SMUGGLE_LINKER","linker/asm symbol redirection to a banned symbol"}
};

const hc_rule_desc *hc_rule_registry(size_t *count)
{
    *count = sizeof(RULES) / sizeof(RULES[0]);
    return RULES;
}

void hc_dump_rules(void)
{
    size_t n, i;
    const hc_rule_desc *r = hc_rule_registry(&n);
    printf("Handshake Compiler (HC) declarative rule registry — %zu rules\n", n);
    printf("%-8s  %-20s  %s\n", "CODE", "NAME", "SUMMARY");
    for (i = 0; i < n; i++) {
        printf("%-8s  %-20s  %s\n", r[i].code, r[i].name, r[i].summary);
    }
}

/* ---- analysis context ---- */
typedef struct {
    hc_unit *u;
    const hc_token_list *tl;
    const char *srcname;
    hc_profile profile;
    int count;
    /* volatile file-scope var names */
    char **vols; size_t nvols;
    /* launder union type names */
    char **lunions; size_t nlun;
} ctx;

static const hc_token *T(ctx *c, size_t i) { return &c->tl->toks[i]; }

static void emit(ctx *c, int code1, size_t tokidx, const char *msg)
{
    const hc_token *t = T(c, tokidx);
    const char *file = t->file ? t->file : c->srcname;
    fprintf(stderr, "%s:%d:%d: error [%s]: %s (%s)\n",
            file, t->line, t->col, RULES[code1 - 1].code, msg, RULES[code1 - 1].name);
    c->count++;
}

static int tok_is(const hc_token *t, const char *s)
{
    return (t->kind == HCT_PUNCT || t->kind == HCT_IDENT) && strcmp(t->text, s) == 0;
}
static int ispfx(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }
static int ieq(const char *s, const char *p) { return strcmp(s, p) == 0; }

/* match close for an open at index */
static size_t match_at(ctx *c, size_t open, const char *o, const char *cl)
{
    int d = 0; size_t i;
    for (i = open; i < c->tl->count; i++) {
        if (tok_is(T(c,i), o)) d++;
        else if (tok_is(T(c,i), cl)) { d--; if (d == 0) return i; }
    }
    return c->tl->count - 1;
}

/* ---------- reachability ---------- */
static void mark(ctx *c, hc_func *f, int strict, int commitish)
{
    size_t i; hc_func *g;
    if (f == NULL) return;
    if (strict && f->reach_strict && commitish && f->reach_commit) return;
    int changed = 0;
    if (strict && !f->reach_strict) { f->reach_strict = 1; changed = 1; }
    if (commitish && !f->reach_commit) { f->reach_commit = 1; changed = 1; }
    if (!changed && f->visited) return;
    f->visited = 1;
    for (i = 0; i < f->ncallees; i++) {
        g = hc_find_func(c->u, f->callees[i]);
        if (g) mark(c, g, strict, commitish);
    }
}

static int strictf(hc_func *f) { return f->phase == PH_PULL || f->phase == PH_BARRIER || f->reach_strict; }
static int commitf(hc_func *f) { return f->phase == PH_COMMIT || f->reach_commit; }
static int phase_any(hc_func *f) {
    return f->phase != PH_NONE || f->reach_strict || f->reach_commit;
}

/* per-func: does the function body directly write a file-scope var? */
static int writes_global(ctx *c, hc_func *f)
{
    size_t i;
    for (i = f->body_start + 1; i < f->body_end; i++) {
        const hc_token *t = T(c, i);
        if (t->kind != HCT_IDENT) continue;
        if (!hc_find_var(c->u, t->text)) continue;
        /* skip to end of lvalue chain: VAR ( .x | ->x | [..] )* */
        size_t j = i + 1;
        while (j < f->body_end) {
            if (tok_is(T(c,j), ".") || tok_is(T(c,j), "->")) { j += 2; continue; }
            if (tok_is(T(c,j), "[")) { j = match_at(c, j, "[", "]") + 1; continue; }
            break;
        }
        const hc_token *nx = T(c, j);
        if (tok_is(nx,"=")||tok_is(nx,"+=")||tok_is(nx,"-=")||tok_is(nx,"*=")||
            tok_is(nx,"/=")||tok_is(nx,"%=")||tok_is(nx,"|=")||tok_is(nx,"&=")||
            tok_is(nx,"^=")||tok_is(nx,"<<=")||tok_is(nx,">>=")||tok_is(nx,"++")||tok_is(nx,"--"))
            return 1;
        if (i > f->body_start + 1) {
            const hc_token *pv = T(c, i - 1);
            if (tok_is(pv,"++")||tok_is(pv,"--")) return 1;
        }
    }
    return 0;
}

/* ---------- ident-based checks within a function ---------- */
static void check_idents(ctx *c, hc_func *f)
{
    int runtime = (c->profile == HC_PROFILE_RUNTIME);
    int S = strictf(f), CM = commitf(f), PA = phase_any(f);
    size_t i;
    if (!PA) return;
    for (i = f->body_start + 1; i < f->body_end; i++) {
        const hc_token *t = T(c, i);
        if (t->kind != HCT_IDENT) continue;
        const char *s = t->text;
        /* E001 POSIX locks */
        if (ispfx(s,"pthread_mutex")||ispfx(s,"pthread_rwlock")||ispfx(s,"pthread_cond")||
            ispfx(s,"pthread_spin")||ispfx(s,"pthread_barrier")||ispfx(s,"pthread_once")||
            ispfx(s,"sem_")) { emit(c,1,i,"POSIX lock/semaphore primitive"); continue; }
        /* E002 C11/OS locks + SYS_futex in component */
        if (ispfx(s,"mtx_")||ispfx(s,"cnd_")||ieq(s,"call_once")||ispfx(s,"tss_")||
            ieq(s,"flock")||ieq(s,"lockf")) { emit(c,2,i,"C11/OS lock primitive"); continue; }
        if (ieq(s,"FUTEX_LOCK_PI")||ieq(s,"FUTEX_TRYLOCK_PI")) { emit(c,2,i,"futex lock op"); continue; }
        if ((ieq(s,"SYS_futex")||ieq(s,"__NR_futex")) && !runtime) { emit(c,2,i,"raw SYS_futex in component"); continue; }
        /* E003 atomic spin */
        if (ieq(s,"atomic_flag_test_and_set")||ieq(s,"atomic_flag_test_and_set_explicit")||
            ieq(s,"__sync_lock_test_and_set")||ieq(s,"__atomic_test_and_set"))
            { emit(c,3,i,"hidden atomic spinlock primitive"); continue; }
        /* E006 thread spawn */
        if (ieq(s,"pthread_create")||ieq(s,"thrd_create")||ieq(s,"clone")||ieq(s,"fork")||ieq(s,"vfork")) {
            if (!runtime) emit(c,6,i,"thread/process creation in component");
            continue;
        }
        if (ieq(s,"pthread_join")) { if (!runtime) emit(c,6,i,"thread join in component"); continue; }
        /* E023 order leak */
        if (ieq(s,"pthread_self")||ieq(s,"gettid")||ieq(s,"sched_getcpu"))
            { emit(c,23,i,"thread-identity leak into deterministic code"); continue; }
        if ((ieq(s,"atomic_fetch_add")||ieq(s,"atomic_fetch_add_explicit")||ieq(s,"__atomic_fetch_add")||ieq(s,"__c11_atomic_fetch_add")) && S)
            { emit(c,23,i,"arrival-order atomic fold in PULL/BARRIER"); continue; }
        /* E010 */
        if (ieq(s,"hs_hs_post_event") && S) { emit(c,10,i,"posting control events from PULL/BARRIER"); continue; }
        /* E018 blocking */
        if ((S||CM) && (ieq(s,"usleep")||ieq(s,"sleep")||ieq(s,"nanosleep")||ieq(s,"clock_nanosleep")||
            ieq(s,"sched_yield")||ieq(s,"poll")||ieq(s,"ppoll")||ieq(s,"select")||ieq(s,"pselect")||
            ieq(s,"epoll_wait")||ieq(s,"epoll_pwait")||ieq(s,"waitpid")||ieq(s,"wait4")))
            { emit(c,18,i,"blocking sleep/yield/IO in phase kernel"); continue; }
        /* E020 wall clock */
        if (ieq(s,"gettimeofday")||ieq(s,"timespec_get")||ieq(s,"ftime")||ieq(s,"hs_now_ns")||
            ((ieq(s,"clock_gettime")||ieq(s,"time")||ieq(s,"clock")) && !runtime))
            { emit(c,20,i,"wall-clock/monotonic time in component payload"); continue; }
        /* E021 cycle counter builtins */
        if (ieq(s,"__rdtsc")||ieq(s,"__builtin_ia32_rdtsc")||ieq(s,"__builtin_ia32_rdtscp")||
            ieq(s,"__builtin_readcyclecounter")||ieq(s,"rdtscp")||ieq(s,"rdtsc"))
            { emit(c,21,i,"hardware cycle counter in deterministic code"); continue; }
        /* E022 rng */
        if (ieq(s,"rand")||ieq(s,"srand")||ieq(s,"random")||ieq(s,"srandom")||ieq(s,"drand48")||
            ieq(s,"lrand48")||ieq(s,"mrand48")||ieq(s,"erand48")||ieq(s,"getrandom")||
            ispfx(s,"arc4random")||ieq(s,"rdrand")||ieq(s,"rdseed")||ieq(s,"rndr"))
            { emit(c,22,i,"ambient entropy / unseeded RNG"); continue; }
        /* E024 dlopen */
        if (ieq(s,"dlopen")||ieq(s,"dlsym")||ieq(s,"dlvsym"))
            { emit(c,24,i,"dynamic-loader symbol smuggling"); continue; }
        /* E009 hs internals member access */
        if (i > f->body_start+1) {
            const hc_token *pv = T(c,i-1);
            if ((tok_is(pv,"->")||tok_is(pv,".")) &&
                (ieq(s,"pub_seq")||ieq(s,"pub_phase")||ieq(s,"pub_cycle")||ieq(s,"pub_group")||
                 ieq(s,"pub_logical_seq")||ieq(s,"pub_generation")||ieq(s,"ledger_hash")||
                 ieq(s,"arrivals")||ieq(s,"expected")||ieq(s,"commit_epoch")||
                 ieq(s,"publish_word")||ieq(s,"barrier_word")||ieq(s,"monatomic")) && (S||CM))
                { emit(c,9,i,"direct access to hs_hs internal/unpublished field"); continue; }
            /* E019 watchdog */
            if ((tok_is(pv,"->")||tok_is(pv,".")) && (ieq(s,"ceiling_ns")||ieq(s,"frozen")) && (S||CM))
                { emit(c,19,i,"watchdog tamper (ceiling_ns/frozen)"); continue; }
        }
    }
}

/* asm scan (E004/E021) */
static void check_asm(ctx *c, hc_func *f)
{
    if (!phase_any(f)) return;
    size_t i;
    for (i = f->body_start + 1; i < f->body_end; i++) {
        const hc_token *t = T(c, i);
        if (t->kind != HCT_IDENT) continue;
        if (!(ieq(t->text,"asm")||ieq(t->text,"__asm__")||ieq(t->text,"__asm"))) continue;
        size_t j = i + 1;
        while (j < f->body_end && (tok_is(T(c,j),"volatile")||ieq(T(c,j)->text,"__volatile__")||
               ieq(T(c,j)->text,"volatile"))) j++;
        if (j >= f->body_end || !tok_is(T(c,j),"(")) continue;
        /* first string literal inside */
        size_t k = j + 1;
        while (k < f->body_end && T(c,k)->kind != HCT_STRING && !tok_is(T(c,k),")")) k++;
        if (k >= f->body_end || T(c,k)->kind != HCT_STRING) continue;
        char dec[512];
        hc_decode_string_literal(T(c,k)->text, dec, sizeof(dec));
        if (strstr(dec,"rdtsc")||strstr(dec,"cntvct")||strstr(dec,"pmccntr")||strstr(dec,"cntfrq"))
            { emit(c,21,i,"inline-asm cycle counter"); continue; }
        if (strstr(dec,"xchg")||strstr(dec,"cmpxchg")||strstr(dec,"ldaxr")||strstr(dec,"stlxr")||
            strstr(dec,"ldxr")||strstr(dec,"stxr")||strstr(dec,"casal")||strstr(dec,"swpal")||
            strstr(dec,"lock ")||strstr(dec,"bts")||strstr(dec,"pause")||strstr(dec,"wfe")||
            strstr(dec,"yield")||strstr(dec,"svc"))
            { emit(c,4,i,"inline-asm lock/spin/pause primitive"); continue; }
    }
}

/* loops: E003 CAS-spin, E005 volatile wait, E016 unbounded, backward goto */
static int is_vol(ctx *c, const char *name) {
    size_t i; for (i=0;i<c->nvols;i++) if (ieq(c->vols[i],name)) return 1; return 0;
}
static void check_loops(ctx *c, hc_func *f)
{
    int S = strictf(f), CM = commitf(f);
    if (!(S||CM)) return;
    size_t i;
    for (i = f->body_start + 1; i < f->body_end; i++) {
        const hc_token *t = T(c, i);
        if (t->kind == HCT_IDENT && ieq(t->text,"while") && i+1<f->body_end && tok_is(T(c,i+1),"(")) {
            size_t cp = match_at(c, i+1, "(", ")");
            /* unbounded: while(1) / while(true) */
            if (cp == i+3 && (ieq(T(c,i+2)->text,"1")||ieq(T(c,i+2)->text,"true")))
                emit(c,16,i,"while(1) unbounded loop in phase kernel");
            /* CAS/exchange spin, or volatile wait, in condition */
            size_t k;
            int vol = 0, cas = 0;
            for (k=i+2;k<cp;k++) {
                const hc_token *ct = T(c,k);
                if (ct->kind==HCT_IDENT) {
                    if (ispfx(ct->text,"atomic_compare_exchange")||ispfx(ct->text,"__atomic_compare_exchange")||ispfx(ct->text,"__c11_atomic_compare_exchange")||ieq(ct->text,"atomic_exchange")||ieq(ct->text,"atomic_exchange_explicit")||ieq(ct->text,"__atomic_exchange")) cas=1;
                    if (is_vol(c, ct->text)) vol=1;
                }
            }
            if (cas) emit(c,3,i,"CAS/exchange retry spinlock loop");
            else if (vol) emit(c,5,i,"volatile busy-wait (Dekker/Peterson) loop");
        }
        if (t->kind == HCT_IDENT && ieq(t->text,"for") && i+1<f->body_end && tok_is(T(c,i+1),"(")) {
            size_t cp = match_at(c, i+1, "(", ")");
            /* find first ';' at depth1 (end of init), check if next is ';' (empty cond) */
            size_t k; int d=0; size_t firstsemi=0;
            for (k=i+2;k<cp;k++){ const hc_token*ct=T(c,k);
                if (tok_is(ct,"(")) d++; else if (tok_is(ct,")")) d--;
                else if (tok_is(ct,";") && d==0) { firstsemi=k; break; } }
            if (firstsemi && firstsemi+1<cp && tok_is(T(c,firstsemi+1),";"))
                emit(c,16,i,"for(;;) empty-condition unbounded loop");
        }
        if (t->kind == HCT_IDENT && ieq(t->text,"goto") && i+1<f->body_end && T(c,i+1)->kind==HCT_IDENT) {
            const char *lbl = T(c,i+1)->text;
            /* backward: label defined earlier in body */
            size_t k;
            for (k=f->body_start+1;k<i;k++){
                if (T(c,k)->kind==HCT_IDENT && ieq(T(c,k)->text,lbl) &&
                    k+1<f->body_end && tok_is(T(c,k+1),":")) { emit(c,16,i,"backward goto loop in phase kernel"); break; }
            }
        }
    }
}

/* strict-only: global writes (E008/E012), cross-shard (E015), const-cast (E013),
 * union pun (E014), post/transition handled elsewhere */
static int is_ro_param(hc_func *f, const char *name)
{
    size_t i; for (i=0;i<f->nro;i++) if (ieq(f->ro_params[i],name)) return 1; return 0;
}
static int is_lunion(ctx *c, const char *name)
{
    size_t i; for (i=0;i<c->nlun;i++) if (ieq(c->lunions[i],name)) return 1; return 0;
}
static void check_strict_mem(ctx *c, hc_func *f)
{
    if (!strictf(f)) return;
    size_t i;
    /* collect xor-vars: local IDENT = ... ^ ... ; */
    char *xorv[64]; size_t nxor=0;
    for (i = f->body_start + 1; i < f->body_end; i++) {
        const hc_token *t = T(c, i);
        /* global write */
        if (t->kind == HCT_IDENT) {
            hc_var *v = hc_find_var(c->u, t->text);
            if (v) {
                size_t j = i + 1; int chain=0;
                while (j < f->body_end) {
                    if (tok_is(T(c,j),".")||tok_is(T(c,j),"->")) { j += 2; chain=1; continue; }
                    if (tok_is(T(c,j),"[")) { j = match_at(c,j,"[","]")+1; chain=1; continue; }
                    break;
                }
                const hc_token *nx = T(c,j);
                int isw = tok_is(nx,"=")||tok_is(nx,"+=")||tok_is(nx,"-=")||tok_is(nx,"*=")||
                    tok_is(nx,"/=")||tok_is(nx,"%=")||tok_is(nx,"|=")||tok_is(nx,"&=")||
                    tok_is(nx,"^=")||tok_is(nx,"<<=")||tok_is(nx,">>=")||tok_is(nx,"++")||tok_is(nx,"--");
                if (isw) {
                    if (v->is_pointer && !chain) emit(c,12,i,"pointer-typed global assigned (phase pointer escape)");
                    else emit(c,8,i,"write to shared/file-scope mutable state in PULL/BARRIER");
                }
            }
        }
        /* xor-var tracking: IDENT = <expr with ^> ; (local) */
        if (t->kind == HCT_IDENT && i+1<f->body_end && tok_is(T(c,i+1),"=")) {
            size_t k=i+2; int sawxor=0;
            while (k<f->body_end && !tok_is(T(c,k),";")) { if (tok_is(T(c,k),"^")) sawxor=1; k++; }
            if (sawxor && nxor<64) xorv[nxor++]=t->text;
        }
        /* E015 cross-shard: base [ idx ] where idx contains ^ or is an xor-var, then write */
        if (t->kind == HCT_IDENT && i+1<f->body_end && tok_is(T(c,i+1),"[")) {
            size_t close = match_at(c,i+1,"[","]");
            int xoridx=0; size_t k;
            for (k=i+2;k<close;k++){ if (tok_is(T(c,k),"^")) xoridx=1;
                if (T(c,k)->kind==HCT_IDENT){ size_t z; for(z=0;z<nxor;z++) if(ieq(xorv[z],T(c,k)->text)) xoridx=1; } }
            if (xoridx) {
                size_t j=close+1; while(j<f->body_end){ if(tok_is(T(c,j),".")||tok_is(T(c,j),"->")){j+=2;continue;} break; }
                if (tok_is(T(c,j),"=")||tok_is(T(c,j),"+=")) emit(c,15,i,"cross-shard write overlap (sibling shard slot)");
            }
        }
        /* E013 const-cast: ( type * ) ROPARAM */
        if (t->kind == HCT_IDENT && is_ro_param(f, t->text) && i>f->body_start+1 && tok_is(T(c,i-1),")")) {
            /* find matching ( */
            int d=0; size_t k=i-1; size_t open=0;
            for (;;){ if(tok_is(T(c,k),")"))d++; else if(tok_is(T(c,k),"(")){d--; if(d==0){open=k;break;}} if(k==f->body_start)break; k--; }
            if (open){ int star=0,cst=0,typ=0; size_t z; for(z=open+1;z<i-1;z++){ if(tok_is(T(c,z),"*"))star=1; if(ieq(T(c,z)->text,"const"))cst=1; if(T(c,z)->kind==HCT_IDENT)typ=1; }
                if (star&&typ&&!cst) emit(c,13,i,"casting away const on readonly snapshot"); }
        }
        /* E014 union pun: local union LUNION use */
        if (t->kind == HCT_IDENT && ieq(t->text,"union") && i+1<f->body_end &&
            T(c,i+1)->kind==HCT_IDENT && is_lunion(c, T(c,i+1)->text))
            emit(c,14,i,"union type-punning to bypass const immutability");
    }
}

/* call-graph: E007 (direct COMMIT call), E011 (transitive leak / fnref) */
static void dfs_leak(ctx *c, hc_func *f, hc_func *root, int depth, int *hit, size_t rooti)
{
    size_t i;
    if (f->visited) return;
    f->visited = 1;
    if (depth >= 1) {
        if (writes_global(c, f) || f->phase == PH_COMMIT || f->phase == PH_PUBLISH) { *hit = 1; }
    }
    for (i = 0; i < f->ncallees; i++) {
        hc_func *g = hc_find_func(c->u, f->callees[i]);
        if (g && g != root) dfs_leak(c, g, root, depth + 1, hit, rooti);
    }
}
static void check_callgraph(ctx *c, hc_func *f)
{
    size_t i, k;
    if (!(f->phase==PH_PULL||f->phase==PH_BARRIER)) return;
    /* E007: direct callee that is COMMIT */
    for (i = 0; i < f->ncallees; i++) {
        hc_func *g = hc_find_func(c->u, f->callees[i]);
        if (g && g->phase == PH_COMMIT)
            emit(c, 7, f->body_start, "PULL/BARRIER directly invokes a COMMIT phase function (skips BARRIER)");
    }
    /* E011: transitive write/commit leak */
    for (k = 0; k < c->u->nfuncs; k++) c->u->funcs[k].visited = 0;
    int hit = 0;
    dfs_leak(c, f, f, 0, &hit, 0);
    if (hit) emit(c, 11, f->body_start, "transitive call-graph leak: PULL reaches a shared write / COMMIT mutator");
    /* E011 fnref: bare reference to a COMMIT or writes_global function used as a value */
    for (i = f->body_start + 1; i < f->body_end; i++) {
        const hc_token *t = T(c, i);
        if (t->kind != HCT_IDENT) continue;
        if (i+1 < f->body_end && tok_is(T(c,i+1),"(")) continue; /* that's a call */
        hc_func *g = hc_find_func(c->u, t->text);
        if (g && g != f && (g->phase == PH_COMMIT || writes_global(c, g)))
            emit(c, 11, i, "function-pointer escape into COMMIT/mutator from PULL");
    }
}

/* recursion cycle reachable from any phase kernel (E017) */
static int dfs_cycle(ctx *c, hc_func *f, char **stack, size_t *sp)
{
    size_t i, z;
    for (z = 0; z < *sp; z++) if (ieq(stack[z], f->name)) return 1;
    stack[(*sp)++] = f->name;
    for (i = 0; i < f->ncallees; i++) {
        hc_func *g = hc_find_func(c->u, f->callees[i]);
        if (g && dfs_cycle(c, g, stack, sp)) { (*sp)--; return 1; }
    }
    (*sp)--;
    return 0;
}
static void check_recursion(ctx *c)
{
    size_t k;
    for (k = 0; k < c->u->nfuncs; k++) {
        hc_func *f = &c->u->funcs[k];
        if (!phase_any(f)) continue;
        char *stack[256]; size_t sp = 0;
        if (dfs_cycle(c, f, stack, &sp))
            emit(c, 17, f->body_start, "recursion cycle reachable from a phase kernel");
    }
}

/* collect volatile file-scope vars + launder unions from the token stream */
static void collect_globals(ctx *c)
{
    size_t i; int depth = 0;
    const hc_token_list *tl = c->tl;
    for (i = 0; i + 1 < tl->count; i++) {
        const hc_token *t = &tl->toks[i];
        if (t->system) continue;
        if (tok_is(t,"{")) depth++;
        else if (tok_is(t,"}")) { if (depth) depth--; }
        if (depth == 0 && t->kind==HCT_IDENT && ieq(t->text,"volatile")) {
            size_t j=i+1;
            while (j<tl->count && !tok_is(&tl->toks[j],";") && !tok_is(&tl->toks[j],"{")) {
                if (tl->toks[j].kind==HCT_IDENT && j+1<tl->count &&
                    (tok_is(&tl->toks[j+1],";")||tok_is(&tl->toks[j+1],"[")||tok_is(&tl->toks[j+1],"=")||tok_is(&tl->toks[j+1],","))) {
                    c->vols = realloc(c->vols,(c->nvols+1)*sizeof(char*));
                    c->vols[c->nvols++] = tl->toks[j].text;
                }
                j++;
            }
        }
        /* launder union: union NAME { ... const ... * ... ; ... * ... ; } with uintptr_t */
        if (t->kind==HCT_IDENT && ieq(t->text,"union") && i+1<tl->count &&
            tl->toks[i+1].kind==HCT_IDENT && i+2<tl->count && tok_is(&tl->toks[i+2],"{")) {
            const char *nm = tl->toks[i+1].text;
            size_t open=i+2; size_t close=match_at(c,open,"{","}");
            int cst=0,star=0,upt=0; size_t z;
            for (z=open+1;z<close;z++){ if(ieq(tl->toks[z].text,"const"))cst=1; if(tok_is(&tl->toks[z],"*"))star=1;
                if(ieq(tl->toks[z].text,"uintptr_t"))upt=1; }
            if (star && (cst||upt)) {
                c->lunions = realloc(c->lunions,(c->nlun+1)*sizeof(char*));
                c->lunions[c->nlun++] = (char*)nm;
            }
        }
    }
}

/* ---------- raw-source pass (E024 split-hex, E025 ##, E026 asm redirect, banned includes) ---------- */
static int raw_contains(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

static void raw_pass(ctx *c, const char *raw)
{
    if (raw == NULL) return;
    size_t firsttok = 0; /* use first user token for location */
    size_t z; for (z=0; z<c->tl->count; z++){ if(!c->tl->toks[z].system){ firsttok=z; break; } }
    /* banned includes */
    if (raw_contains(raw,"<threads.h>")) emit(c,2,firsttok,"banned include <threads.h>");
    if (raw_contains(raw,"<semaphore.h>")||raw_contains(raw,"<sys/sem.h>")) emit(c,1,firsttok,"banned include <semaphore.h>/<sys/sem.h>");
    if (raw_contains(raw,"<dlfcn.h>")) emit(c,24,firsttok,"banned include <dlfcn.h>");
    if (raw_contains(raw,"<omp.h>")) emit(c,6,firsttok,"banned include <omp.h>");
    /* E025: ## token paste that smuggles a banned symbol */
    if (raw_contains(raw,"##")) {
        /* preprocessed token stream contains a banned symbol absent from raw text */
        size_t i;
        for (i=0;i<c->tl->count;i++){
            const hc_token *t=&c->tl->toks[i];
            if (t->system || t->kind!=HCT_IDENT) continue;
            if (ispfx(t->text,"pthread_mutex")||ispfx(t->text,"pthread_rwlock")||
                ispfx(t->text,"pthread_cond")||ispfx(t->text,"pthread_spin")) {
                if (!raw_contains(raw, t->text)) { emit(c,25,i,"preprocessor ## token-paste smuggling a banned symbol"); break; }
            }
        }
    }
    /* E026: __asm__("<banned symbol>") redirection on a declaration */
    {
        const char *p = raw;
        const char *keys[] = {"__asm__","asm","__attribute__"};
        size_t ki;
        for (ki=0; ki<3; ki++) {
            const char *q = raw;
            while ((q = strstr(q, keys[ki])) != NULL) {
                const char *paren = strchr(q, '(');
                const char *stop = strchr(q, ';');
                if (paren && (!stop || paren < stop)) {
                    /* look for a quoted bare symbol within the next ~80 chars */
                    const char *dq = strchr(paren, '"');
                    if (dq && (!stop || dq < stop + 80)) {
                        char sym[128]; size_t si=0; const char *r=dq+1;
                        while (*r && *r!='"' && si<sizeof(sym)-1) sym[si++]=*r++;
                        sym[si]='\0';
                        if (ispfx(sym,"pthread_mutex")||ispfx(sym,"pthread_rwlock")||
                            ispfx(sym,"pthread_cond")||ispfx(sym,"pthread_spin")||
                            ispfx(sym,"sem_")||ispfx(sym,"mtx_")) {
                            emit(c,26,firsttok,"linker/asm symbol redirection to a banned symbol");
                        }
                    }
                }
                q += strlen(keys[ki]);
            }
        }
        (void)p;
    }
    /* E024: split/hex string literal concatenation decoding to a banned symbol, in phase funcs */
    {
        size_t i;
        for (i=0;i+0<c->tl->count;i++){
            const hc_token *t=&c->tl->toks[i];
            if (t->system || t->kind!=HCT_STRING) continue;
            /* concatenate run of adjacent string literals */
            char acc[256]; size_t ai=0; size_t j=i;
            while (j<c->tl->count && c->tl->toks[j].kind==HCT_STRING) {
                char dec[256]; size_t dl=hc_decode_string_literal(c->tl->toks[j].text,dec,sizeof(dec));
                size_t m; for(m=0;m<dl && ai<sizeof(acc)-1;m++) acc[ai++]=dec[m];
                j++;
            }
            acc[ai]='\0';
            if (ispfx(acc,"pthread_mutex")||ispfx(acc,"pthread_rwlock")||ispfx(acc,"pthread_cond")||
                ispfx(acc,"pthread_spin")||ieq(acc,"pthread_create")) {
                emit(c,24,i,"string-constructed banned symbol (split/hex smuggle)");
            }
            i=j-1;
        }
    }
}

int hc_verify(hc_unit *u, const hc_token_list *tl,
              const char *rawsrc, const char *srcname, hc_profile profile)
{
    ctx c; size_t k;
    memset(&c, 0, sizeof(c));
    c.u = u; c.tl = tl; c.srcname = srcname; c.profile = profile;

    /* reachability from PULL/BARRIER (strict) and COMMIT */
    for (k = 0; k < u->nfuncs; k++) u->funcs[k].visited = 0;
    for (k = 0; k < u->nfuncs; k++) {
        hc_func *f = &u->funcs[k];
        if (f->phase == PH_PULL || f->phase == PH_BARRIER) { for(size_t z=0;z<u->nfuncs;z++) u->funcs[z].visited=0; mark(&c, f, 1, 0); }
    }
    for (k = 0; k < u->nfuncs; k++) {
        hc_func *f = &u->funcs[k];
        if (f->phase == PH_COMMIT) { for(size_t z=0;z<u->nfuncs;z++) u->funcs[z].visited=0; mark(&c, f, 0, 1); }
    }

    collect_globals(&c);

    for (k = 0; k < u->nfuncs; k++) {
        hc_func *f = &u->funcs[k];
        check_idents(&c, f);
        check_asm(&c, f);
        check_loops(&c, f);
        check_strict_mem(&c, f);
        check_callgraph(&c, f);
    }
    check_recursion(&c);
    raw_pass(&c, rawsrc);

    free(c.vols);
    free(c.lunions);
    return c.count;
}
