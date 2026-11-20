/* SPDX-License-Identifier: MIT */
#include "hc_rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const hc_rule_desc RULES[] = {
    {"HC-E001","LOCK_POSIX","POSIX mutex/rwlock/cond/spin/barrier/semaphore primitive"},
    {"HC-E002","LOCK_C11_OR_OS","C11 <threads.h> lock, flock/lockf/fcntl, or SYS_futex in component"},
    {"HC-E003","LOCK_ATOMIC_SPIN","hidden atomic_flag / CAS-retry / exchange / atomic-load spinlock"},
    {"HC-E004","LOCK_ASM_OR_PAUSE","inline-asm lock/spin/pause/raw-opcode bypass"},
    {"HC-E005","LOCK_VOLATILE_WAIT","volatile Dekker/Peterson busy-wait"},
    {"HC-E006","LOCK_HIDDEN_THREAD","unauthorized thread/process/ctor creation in component"},
    {"HC-E007","PHASE_TRANSITION","illegal beat transition (PULL invoking COMMIT, skipping BARRIER)"},
    {"HC-E008","PHASE_WRITE_PULL","write to shared/file-scope/static mutable state inside PULL/BARRIER"},
    {"HC-E009","PHASE_UNPUBLISHED","reading/mutating unpublished staging or hs_hs internals"},
    {"HC-E010","PHASE_COMMIT_LEAK","posting commit/control events from PULL/BARRIER"},
    {"HC-E011","PHASE_CALL_LEAK","transitive call-graph leak to shared write / COMMIT / extern"},
    {"HC-E012","PHASE_PTR_ESCAPE","phase-scoped/stack/shard pointer escaping into global"},
    {"HC-E013","ALIAS_CONST_CAST","casting away const on a readonly snapshot"},
    {"HC-E014","ALIAS_UNION_PUN","union / uintptr_t punning bypassing const immutability"},
    {"HC-E015","ALIAS_SHARD_OVERLAP","cross-shard write overlap"},
    {"HC-E016","LIVENESS_LOOP","statically unbounded loop / backward goto in phase kernel"},
    {"HC-E017","LIVENESS_RECURSION","direct/mutual/indirect recursion reachable from a phase kernel"},
    {"HC-E018","LIVENESS_BLOCKING","blocking sleep/yield/IO/setjmp inside a phase kernel"},
    {"HC-E019","WATCHDOG_TAMPER","tampering with watchdog ceiling_ns/frozen"},
    {"HC-E020","DET_WALL_CLOCK","wall-clock time (gettimeofday/time/CLOCK_REALTIME) in payload"},
    {"HC-E021","DET_CYCLE_COUNTER","raw hardware cycle counter / intrinsic in deterministic code"},
    {"HC-E022","DET_AMBIENT_RNG","ambient entropy / unseeded RNG"},
    {"HC-E023","DET_ORDER_LEAK","pointer/pid/tid/env/arrival-order leak into output/ledger"},
    {"HC-E024","SMUGGLE_DLOPEN","dynamic-loader symbol smuggling (dlopen/dlsym/split-hex)"},
    {"HC-E025","SMUGGLE_MACRO","preprocessor ## token-pasting / __HC_COMPILER__ / flag smuggle"},
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
    for (i = 0; i < n; i++)
        printf("%-8s  %-20s  %s\n", r[i].code, r[i].name, r[i].summary);
}

/* ---- analysis context ---- */
typedef struct {
    hc_unit *u;
    const hc_token_list *tl;
    const char *srcname;
    hc_profile profile;
    int count;
    char **vols;    size_t nvols;    /* volatile file-scope var names */
    char **volf;    size_t nvolf;    /* volatile struct-field names */
    char **lunions; size_t nlun;     /* const-launder union type/typedef names */
    char **ftaken;  size_t nft;      /* function names address-taken at file scope */
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

static int is_rt_header(const hc_token *t)
{
    const char *f = t->file;
    size_t n;
    if (!f) return 0;
    n = strlen(f);
    return (n >= 8 && strcmp(f + n - 8, "hc_rt.h") == 0) ||
           (n >= 7 && strcmp(f + n - 7, "hc_rt.h") == 0);
}

static size_t match_at(ctx *c, size_t open, const char *o, const char *cl)
{
    int d = 0; size_t i;
    for (i = open; i < c->tl->count; i++) {
        if (tok_is(T(c,i), o)) d++;
        else if (tok_is(T(c,i), cl)) { d--; if (d == 0) return i; }
    }
    return c->tl->count - 1;
}

static int func_index(hc_unit *u, const char *name)
{
    size_t i;
    for (i = 0; i < u->nfuncs; i++) if (strcmp(u->funcs[i].name, name) == 0) return (int)i;
    return -1;
}

/* successor function indices of f: direct callees + address-taken fn refs */
static size_t succ_of(ctx *c, hc_func *f, int *out, size_t cap)
{
    size_t n = 0, i; int gi;
    for (i = 0; i < f->ncallees && n < cap; i++) {
        gi = func_index(c->u, f->callees[i]);
        if (gi >= 0) out[n++] = gi;
    }
    for (i = 0; i < f->nfnrefs && n < cap; i++) {
        gi = func_index(c->u, f->fnrefs[i]);
        if (gi >= 0) {
            size_t z; int dup = 0;
            for (z = 0; z < n; z++) if (out[z] == gi) { dup = 1; break; }
            if (!dup) out[n++] = gi;
        }
    }
    return n;
}

/* ---------- reachability (iterative worklist, O(V+E)) ---------- */
static void propagate_reach(ctx *c)
{
    size_t nf = c->u->nfuncs, i;
    int *stack = (int *)malloc((nf + 1) * sizeof(int));
    int *succ = (int *)malloc((nf + 1) * sizeof(int));
    if (!stack || !succ) { free(stack); free(succ); return; }

    /* strict reachability */
    for (i = 0; i < nf; i++) c->u->funcs[i].visited = 0;
    { size_t sp = 0;
      for (i = 0; i < nf; i++)
          if (c->u->funcs[i].phase == PH_PULL || c->u->funcs[i].phase == PH_BARRIER) {
              c->u->funcs[i].reach_strict = 1; c->u->funcs[i].visited = 1; stack[sp++] = (int)i;
          }
      while (sp) {
          hc_func *f = &c->u->funcs[stack[--sp]];
          size_t ns = succ_of(c, f, succ, nf), k;
          for (k = 0; k < ns; k++) {
              hc_func *g = &c->u->funcs[succ[k]];
              if (!g->reach_strict) { g->reach_strict = 1; }
              if (!g->visited)      { g->visited = 1; stack[sp++] = succ[k]; }
          }
      }
    }
    /* commit reachability */
    for (i = 0; i < nf; i++) c->u->funcs[i].visited = 0;
    { size_t sp = 0;
      for (i = 0; i < nf; i++)
          if (c->u->funcs[i].phase == PH_COMMIT) {
              c->u->funcs[i].reach_commit = 1; c->u->funcs[i].visited = 1; stack[sp++] = (int)i;
          }
      while (sp) {
          hc_func *f = &c->u->funcs[stack[--sp]];
          size_t ns = succ_of(c, f, succ, nf), k;
          for (k = 0; k < ns; k++) {
              hc_func *g = &c->u->funcs[succ[k]];
              if (!g->reach_commit) { g->reach_commit = 1; }
              if (!g->visited)      { g->visited = 1; stack[sp++] = succ[k]; }
          }
      }
    }
    free(stack); free(succ);
}

static int strictf(hc_func *f) { return f->phase == PH_PULL || f->phase == PH_BARRIER || f->reach_strict; }
static int commitf(hc_func *f) { return f->phase == PH_COMMIT || f->reach_commit; }
static int phase_any(hc_func *f) { return f->phase != PH_NONE || f->reach_strict || f->reach_commit; }

/* per-func: does the body directly write a file-scope var? (incl prefix/paren) */
static int writes_global(ctx *c, hc_func *f)
{
    size_t i;
    for (i = f->body_start + 1; i < f->body_end; i++) {
        const hc_token *t = T(c, i);
        if (t->kind != HCT_IDENT) continue;
        if (!hc_find_var(c->u, t->text)) continue;
        size_t j = i + 1;
        while (j < f->body_end) {
            if (tok_is(T(c,j), ".") || tok_is(T(c,j), "->")) { j += 2; continue; }
            if (tok_is(T(c,j), "[")) { j = match_at(c, j, "[", "]") + 1; continue; }
            break;
        }
        while (j < f->body_end && tok_is(T(c,j), ")")) j++;  /* parenthesised lvalue */
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

/* ---------- TU-wide bans (every function) ---------- */
static void check_bans(ctx *c, hc_func *f)
{
    int runtime = (c->profile == HC_PROFILE_RUNTIME);
    int comp = !runtime;
    int S = strictf(f), CM = commitf(f);
    size_t i;

    if (comp && f->is_ctor)
        emit(c, 6, f->decl_start, "constructor/destructor runs outside phase boundaries in component");

    for (i = f->body_start + 1; i < f->body_end; i++) {
        const hc_token *t = T(c, i);
        if (t->kind != HCT_IDENT) continue;
        const char *s = t->text;

        /* E001 POSIX locks + SysV semaphores */
        if (ispfx(s,"pthread_mutex")||ispfx(s,"pthread_rwlock")||ispfx(s,"pthread_cond")||
            ispfx(s,"pthread_spin")||ispfx(s,"pthread_barrier")||ispfx(s,"pthread_once")||
            ispfx(s,"sem_")) { emit(c,1,i,"POSIX lock/semaphore primitive"); continue; }
        if (ieq(s,"semop")||ieq(s,"semtimedop")||ieq(s,"semget")||ieq(s,"semctl"))
            { emit(c,1,i,"SysV semaphore primitive"); continue; }
        /* E002 C11/OS locks */
        if (ispfx(s,"mtx_")||ispfx(s,"cnd_")||ieq(s,"call_once")||ispfx(s,"tss_")||
            ieq(s,"flock")||ieq(s,"lockf")) { emit(c,2,i,"C11/OS lock primitive"); continue; }
        if (ieq(s,"FUTEX_LOCK_PI")||ieq(s,"FUTEX_TRYLOCK_PI")) { emit(c,2,i,"futex lock op"); continue; }
        if ((ieq(s,"SYS_futex")||ieq(s,"__NR_futex")) && comp) { emit(c,2,i,"raw SYS_futex in component"); continue; }
        if (ieq(s,"fcntl") && comp) { emit(c,2,i,"fcntl file-lock in component"); continue; }
        /* E003 atomic spin primitives (any phase) */
        if (ieq(s,"atomic_flag_test_and_set")||ieq(s,"atomic_flag_test_and_set_explicit")||
            ieq(s,"__sync_lock_test_and_set")||ieq(s,"__atomic_test_and_set"))
            { emit(c,3,i,"hidden atomic spinlock primitive"); continue; }
        /* E006 thread/process/ctor creation */
        if (ieq(s,"system")||ieq(s,"popen")||ispfx(s,"posix_spawn")||
            ieq(s,"execve")||ieq(s,"execv")||ieq(s,"execvp")||ieq(s,"execvpe")||
            ieq(s,"execl")||ieq(s,"execlp")||ieq(s,"execle"))
            { emit(c,6,i,"process creation"); continue; }
        if (ieq(s,"pthread_create")||ieq(s,"thrd_create")||ieq(s,"clone")||ieq(s,"fork")||ieq(s,"vfork")) {
            if (comp) emit(c,6,i,"thread/process creation in component");
            continue;
        }
        if (ieq(s,"pthread_join")||ieq(s,"pthread_detach")) { if (comp) emit(c,6,i,"thread join/detach in component"); continue; }
        /* E018 setjmp/longjmp (non-local control transfer) */
        if (ieq(s,"setjmp")||ieq(s,"_setjmp")||ieq(s,"sigsetjmp")||
            ieq(s,"longjmp")||ieq(s,"_longjmp")||ieq(s,"siglongjmp"))
            { emit(c,18,i,"setjmp/longjmp non-local control transfer"); continue; }
        /* E023 thread-identity / pid / ambient-order leaks (component). getenv /
           secure_getenv are deterministic, fixed-at-exec config reads and are
           NOT banned (real component glue like hs_battery_read uses them). */
        if (comp && (ieq(s,"pthread_self")||ieq(s,"gettid")||ieq(s,"sched_getcpu")||
            ieq(s,"getpid")||ieq(s,"getppid")||ieq(s,"getauxval")))
            { emit(c,23,i,"pid/tid/ambient-order leak into deterministic code"); continue; }
        /* E020 wall clock (always). `time` is guarded by call-syntax so a
           parameter/variable/member named "time" (e.g. peanut-gb's
           `const struct tm *time`) is not a false positive. */
        if (ieq(s,"gettimeofday")||ieq(s,"__gettimeofday")||ieq(s,"timespec_get")||
            ieq(s,"ftime")||(ieq(s,"time") && i+1<f->body_end && tok_is(T(c,i+1),"(")))
            { emit(c,20,i,"wall-clock time in deterministic payload"); continue; }
        if (comp && ((ieq(s,"clock") && i+1<f->body_end && tok_is(T(c,i+1),"("))||
            (ieq(s,"times") && i+1<f->body_end && tok_is(T(c,i+1),"("))||
            ieq(s,"clock_getres")||ieq(s,"timer_create")||ieq(s,"timer_settime")))
            { emit(c,20,i,"OS clock/timer in component payload"); continue; }
        /* clock_gettime: REALTIME banned always; any clock banned in component */
        if (ieq(s,"clock_gettime")) {
            size_t a = i + 1; const char *clk = NULL;
            if (a < f->body_end && tok_is(T(c,a),"(")) {
                size_t b = a + 1;
                if (b < f->body_end && T(c,b)->kind == HCT_IDENT) clk = T(c,b)->text;
            }
            if (clk && (ispfx(clk,"CLOCK_REALTIME")||ieq(clk,"CLOCK_TAI")||ieq(clk,"CLOCK_BOOTTIME")))
                { emit(c,20,i,"CLOCK_REALTIME/TAI/BOOTTIME wall clock"); continue; }
            if (comp) { emit(c,20,i,"clock_gettime in component payload"); continue; }
            continue; /* runtime + CLOCK_MONOTONIC: allowed */
        }
        /* E021 cycle counters + intrinsics (component / !runtime) */
        if ((ieq(s,"__rdtsc")||ieq(s,"__rdtscp")||ieq(s,"_rdtsc")||ieq(s,"rdtsc")||ieq(s,"rdtscp")||
             ieq(s,"__builtin_ia32_rdtsc")||ieq(s,"__builtin_ia32_rdtscp")||
             ieq(s,"__builtin_readcyclecounter")||ieq(s,"__rdpmc")||ieq(s,"__builtin_ia32_rdpmc")||
             ieq(s,"__arm_rsr")||ieq(s,"__arm_rsr64")||ieq(s,"__arm_rsrp")||ieq(s,"__arm_mrs")||
             ieq(s,"__arm_rsrf")||ieq(s,"__arm_rsrf64")||
             ieq(s,"__builtin_arm_rsr")||ieq(s,"__builtin_arm_rsr64")||
             ieq(s,"__builtin_aarch64_rsr")||ieq(s,"__builtin_aarch64_rsr64")||
             ieq(s,"__builtin_aarch64_rsrp")||ieq(s,"__builtin_aarch64_rsrf")||
             ieq(s,"__builtin_aarch64_rsrf64")||ieq(s,"__builtin_aarch64_get_cntvct")||
             ieq(s,"hc_cycle_counter")||ieq(s,"hc_cycle_freq")) && (S||CM))
            { emit(c,21,i,"hardware cycle counter / intrinsic in deterministic code"); continue; }
        /* E022 ambient RNG + intrinsics (always) */
        if (ieq(s,"rand")||ieq(s,"srand")||ieq(s,"random")||ieq(s,"srandom")||ieq(s,"drand48")||
            ieq(s,"lrand48")||ieq(s,"mrand48")||ieq(s,"erand48")||ieq(s,"getrandom")||
            ieq(s,"getentropy")||ispfx(s,"arc4random")||ieq(s,"rdrand")||ieq(s,"rdseed")||ieq(s,"rndr")||
            ispfx(s,"__builtin_ia32_rdrand")||ispfx(s,"__builtin_ia32_rdseed"))
            { emit(c,22,i,"ambient entropy / unseeded RNG"); continue; }
        /* E024 dlopen */
        if (ieq(s,"dlopen")||ieq(s,"dlsym")||ieq(s,"dlvsym")||ieq(s,"dlmopen"))
            { emit(c,24,i,"dynamic-loader symbol smuggling"); continue; }
        /* thrd/sched sleep+yield (component) */
        if (comp && (ieq(s,"thrd_sleep")||ieq(s,"thrd_yield")||ieq(s,"sched_yield")))
            { emit(c,18,i,"thread sleep/yield in component"); continue; }
        /* async signals (component) */
        if (comp && (ieq(s,"alarm")||ieq(s,"raise")||ieq(s,"signal")||ieq(s,"sigaction")||
            ieq(s,"setitimer")||ieq(s,"kill")))
            { emit(c,18,i,"async signal/timer in component"); continue; }

        /* E018 blocking sleep / IO — SCOPED to phase kernels (strict/commit) only,
         * so non-phase component drivers (gb_emu/pngenc) keep legitimate fopen/fread. */
        if ((S||CM) && (ieq(s,"usleep")||ieq(s,"sleep")||ieq(s,"nanosleep")||ieq(s,"clock_nanosleep")||
            ieq(s,"poll")||ieq(s,"ppoll")||ieq(s,"select")||ieq(s,"pselect")||
            ieq(s,"epoll_wait")||ieq(s,"epoll_pwait")||ieq(s,"waitpid")||ieq(s,"wait4")||
            ieq(s,"read")||ieq(s,"pread")||ieq(s,"readv")||ieq(s,"write")||ieq(s,"pwrite")||
            ieq(s,"recv")||ieq(s,"recvfrom")||ieq(s,"recvmsg")||ieq(s,"send")||ieq(s,"sendto")||
            ieq(s,"accept")||ieq(s,"accept4")||ieq(s,"connect")||
            ieq(s,"open")||ieq(s,"openat")||ieq(s,"fopen")||ieq(s,"fdopen")||ieq(s,"freopen")||
            ieq(s,"fread")||ieq(s,"fgets")||ieq(s,"getline")||ieq(s,"ioctl")))
            { emit(c,18,i,"blocking sleep/IO in phase kernel"); continue; }

        /* E010 posting control events from PULL/BARRIER */
        if (ieq(s,"hs_hs_post_event") && S) { emit(c,10,i,"posting control events from PULL/BARRIER"); continue; }
        /* E023 arrival-order atomic fold in PULL/BARRIER */
        if ((ieq(s,"atomic_fetch_add")||ieq(s,"atomic_fetch_add_explicit")||ieq(s,"__atomic_fetch_add")||
             ieq(s,"__c11_atomic_fetch_add")) && S)
            { emit(c,23,i,"arrival-order atomic fold in PULL/BARRIER"); continue; }
        /* E009/E019 hs internals + watchdog (member access, strict/commit) */
        if (i > f->body_start+1) {
            const hc_token *pv = T(c,i-1);
            if ((tok_is(pv,"->")||tok_is(pv,".")) &&
                (ieq(s,"pub_seq")||ieq(s,"pub_phase")||ieq(s,"pub_cycle")||ieq(s,"pub_group")||
                 ieq(s,"pub_logical_seq")||ieq(s,"pub_generation")||ieq(s,"ledger_hash")||
                 ieq(s,"arrivals")||ieq(s,"expected")||ieq(s,"commit_epoch")||
                 ieq(s,"publish_word")||ieq(s,"barrier_word")||ieq(s,"monatomic")) && (S||CM))
                { emit(c,9,i,"direct access to hs_hs internal/unpublished field"); continue; }
            if ((tok_is(pv,"->")||tok_is(pv,".")) && (ieq(s,"ceiling_ns")||ieq(s,"frozen")) && (S||CM))
                { emit(c,19,i,"watchdog tamper (ceiling_ns/frozen)"); continue; }
        }
    }
}

/* ---------- inline-asm scan (E004/E021) ---------- */
static void lc_strip(char *s) { size_t i; for (i=0;s[i];i++) s[i]=(char)tolower((unsigned char)s[i]); }
static void check_asm(ctx *c, hc_func *f)
{
    int runtime = (c->profile == HC_PROFILE_RUNTIME);
    size_t i;
    for (i = f->body_start + 1; i < f->body_end; i++) {
        const hc_token *t = T(c, i);
        if (t->kind != HCT_IDENT) continue;
        if (!(ieq(t->text,"asm")||ieq(t->text,"__asm__")||ieq(t->text,"__asm"))) continue;
        if (is_rt_header(t)) continue;  /* sanctioned runtime header */
        size_t j = i + 1;
        while (j < f->body_end && (tok_is(T(c,j),"volatile")||ieq(T(c,j)->text,"__volatile__")||
               ieq(T(c,j)->text,"goto"))) j++;
        if (j >= f->body_end || !tok_is(T(c,j),"(")) continue;
        /* concatenate ALL adjacent string literals of the whole asm group */
        size_t close = match_at(c, j, "(", ")");
        char acc[1024]; size_t ai = 0; size_t k;
        for (k = j + 1; k < close && ai < sizeof(acc) - 1; k++) {
            if (T(c,k)->kind == HCT_STRING) {
                char dec[512]; size_t dl = hc_decode_string_literal(T(c,k)->text, dec, sizeof(dec));
                size_t m; for (m = 0; m < dl && ai < sizeof(acc)-1; m++) acc[ai++] = dec[m];
            }
        }
        acc[ai] = '\0';
        lc_strip(acc);
        int cyc = (strstr(acc,"rdtsc")||strstr(acc,"rdtscp")||strstr(acc,"rdpmc")||
                   strstr(acc,"cntvct")||strstr(acc,"cntpct")||strstr(acc,"cntfrq")||
                   strstr(acc,"pmccntr")||strstr(acc,"s3_3_c14"));
        int rawop = (strstr(acc,".byte")||strstr(acc,".inst")||strstr(acc,".word")||
                     strstr(acc,".long")||strstr(acc,".short")||strstr(acc,".quad")||
                     strstr(acc,".hword")||strstr(acc,".2byte")||strstr(acc,".4byte")||
                     strstr(acc,".8byte")||strstr(acc,".incbin")||strstr(acc,".ascii"));
        int lock = (strstr(acc,"xchg")||strstr(acc,"cmpxchg")||strstr(acc,"xadd")||
                    strstr(acc,"ldaxr")||strstr(acc,"stlxr")||strstr(acc,"ldxr")||strstr(acc,"stxr")||
                    strstr(acc,"ldaxp")||strstr(acc,"stlxp")||strstr(acc,"casal")||strstr(acc,"swpal")||
                    strstr(acc,"ldadd")||strstr(acc,"lock")||strstr(acc,"bts")||strstr(acc,"pause")||
                    strstr(acc,"wfe")||strstr(acc,"wfi")||strstr(acc,"sev")||strstr(acc,"yield"));
        if (cyc) { emit(c,21,i,"inline-asm cycle counter"); continue; }
        if (!runtime) { emit(c,4,i,"raw inline asm not permitted in component (route via hc_rt.h)"); continue; }
        if (rawop) { emit(c,4,i,"raw machine-code opcode directive in inline asm"); continue; }
        if (lock)  { emit(c,4,i,"inline-asm lock/spin/pause primitive"); continue; }
    }
}

/* ---------- TU-wide file-scope (top-level) inline asm ----------
   check_asm only scans inside parsed function bodies; a top-level `asm`/
   `__asm__` block (w03/w20/w21, Cortana b04/c01/c02) injects raw machine code,
   .init_array constructors, syscalls or cycle counters into the TU without ever
   entering a function. Scan every user-provenance token OUTSIDE any function
   body (and outside system headers / hc_rt.h) for statement-form asm. The
   symbol-redirect declarator form (`extern int x __asm__("sym")`) is handled by
   E026 and is skipped here (it is preceded by an identifier or ')'/']'). */
static void check_toplevel_asm(ctx *c)
{
    int runtime = (c->profile == HC_PROFILE_RUNTIME);
    size_t i;
    for (i = 0; i < c->tl->count; i++) {
        const hc_token *t = T(c,i);
        if (t->kind != HCT_IDENT) continue;
        if (!(ieq(t->text,"asm")||ieq(t->text,"__asm__")||ieq(t->text,"__asm"))) continue;
        if (t->system) continue;          /* system header */
        if (is_rt_header(t)) continue;     /* sanctioned runtime header */
        { size_t z; int inbody=0;
          for (z=0; z<c->u->nfuncs; z++)
              if (i>c->u->funcs[z].body_start && i<c->u->funcs[z].body_end) { inbody=1; break; }
          if (inbody) continue; }          /* handled by check_asm */
        /* statement form only (file-scope asm declaration): prev token is a
           declaration boundary. Declarator symbol-redirect is left to E026. */
        { int stmt_form = (i==0);
          if (!stmt_form) { const hc_token *pv=T(c,i-1); if (tok_is(pv,";")||tok_is(pv,"}")) stmt_form=1; }
          if (!stmt_form) continue; }
        size_t j = i + 1;
        while (j < c->tl->count && (tok_is(T(c,j),"volatile")||ieq(T(c,j)->text,"__volatile__")||
               ieq(T(c,j)->text,"goto"))) j++;
        if (j >= c->tl->count || !tok_is(T(c,j),"(")) continue;
        size_t close = match_at(c, j, "(", ")");
        char acc[2048]; size_t ai = 0; size_t k;
        for (k = j + 1; k < close && ai < sizeof(acc) - 1; k++) {
            if (T(c,k)->kind == HCT_STRING) {
                char dec[1024]; size_t dl = hc_decode_string_literal(T(c,k)->text, dec, sizeof(dec));
                size_t m; for (m = 0; m < dl && ai < sizeof(acc)-1; m++) acc[ai++] = dec[m];
            }
        }
        acc[ai] = '\0';
        lc_strip(acc);
        if (!runtime) {
            emit(c,4,i,"top-level file-scope inline asm not permitted in component (route via hc_rt.h)");
            continue;
        }
        /* RUNTIME: even the audited core may not emit constructors, raw opcodes,
           syscalls, cycle counters or lock/spin primitives from top-level asm. */
        int ctor = (strstr(acc,".init_array")||strstr(acc,".preinit_array")||strstr(acc,".fini_array")||
                    strstr(acc,".ctors")||strstr(acc,".dtors")||strstr(acc,".section")||
                    strstr(acc,".pushsection")||strstr(acc,".subsection"));
        int rawop = (strstr(acc,".byte")||strstr(acc,".inst")||strstr(acc,".word")||
                     strstr(acc,".long")||strstr(acc,".short")||strstr(acc,".quad")||
                     strstr(acc,".hword")||strstr(acc,".2byte")||strstr(acc,".4byte")||
                     strstr(acc,".8byte")||strstr(acc,".incbin")||strstr(acc,".ascii"));
        int sysc = (strstr(acc,"syscall")||strstr(acc,"\n svc")||strstr(acc,"\tsvc")||strstr(acc," svc")||
                    strstr(acc,"int $0x80")||strstr(acc,"sysenter"));
        int cyc  = (strstr(acc,"rdtsc")||strstr(acc,"rdpmc")||strstr(acc,"cntvct")||
                    strstr(acc,"cntpct")||strstr(acc,"pmccntr")||strstr(acc,"s3_3_c14"));
        int lock = (strstr(acc,"xchg")||strstr(acc,"cmpxchg")||strstr(acc,"xadd")||
                    strstr(acc,"ldaxr")||strstr(acc,"stlxr")||strstr(acc,"ldxr")||strstr(acc,"stxr")||
                    strstr(acc,"lock")||strstr(acc,"pause")||strstr(acc,"wfe")||strstr(acc,"wfi")||
                    strstr(acc,"sev")||strstr(acc,"yield"));
        if (cyc)       emit(c,21,i,"top-level inline-asm cycle counter");
        else if (ctor) emit(c,4,i,"top-level inline asm emitting a constructor/section directive");
        else if (rawop)emit(c,4,i,"top-level inline asm raw machine-code opcode directive");
        else if (sysc) emit(c,4,i,"top-level inline asm raw syscall");
        else if (lock) emit(c,4,i,"top-level inline-asm lock/spin/pause primitive");
    }
}

/* ---------- loops: E016 unbounded, E003 CAS/atomic-load spin, E005 volatile ---------- */
static int is_vol(ctx *c, const char *name) { size_t i; for(i=0;i<c->nvols;i++) if(ieq(c->vols[i],name)) return 1; return 0; }
static int is_volf(ctx *c, const char *name){ size_t i; for(i=0;i<c->nvolf;i++) if(ieq(c->volf[i],name)) return 1; return 0; }

static void scan_loop_range(ctx *c, hc_func *f, size_t i, size_t lo, size_t hi)
{
    size_t k; int cas=0, aload=0, vol=0;
    (void)f;
    for (k = lo; k < hi; k++) {
        const hc_token *ct = T(c,k);
        if (ct->kind != HCT_IDENT) continue;
        if (ispfx(ct->text,"atomic_compare_exchange")||ispfx(ct->text,"__atomic_compare_exchange")||
            ispfx(ct->text,"__c11_atomic_compare_exchange")||ispfx(ct->text,"__sync_bool_compare")||
            ispfx(ct->text,"__sync_val_compare")||ieq(ct->text,"atomic_exchange")||
            ieq(ct->text,"atomic_exchange_explicit")||ieq(ct->text,"__atomic_exchange")) cas=1;
        if (ispfx(ct->text,"atomic_load")||ispfx(ct->text,"__atomic_load")||ispfx(ct->text,"__c11_atomic_load")) aload=1;
        if (is_vol(c,ct->text)||is_volf(c,ct->text)) vol=1;
    }
    if (cas)        emit(c,3,i,"CAS/exchange retry spinlock loop");
    else if (aload) emit(c,3,i,"atomic-load busy-wait lock loop");
    else if (vol)   emit(c,5,i,"volatile busy-wait (Dekker/Peterson) loop");
}

static int zero_literal(const hc_token *t){
    if (t->kind!=HCT_NUMBER) return 0;
    const char *p=t->text;
    if (p[0]=='0' && p[1]=='x') { p+=2; while(*p=='0')p++; }
    else { while(*p=='0')p++; }
    while(*p=='u'||*p=='U'||*p=='l'||*p=='L') p++;
    return *p=='\0';
}
/* Does token range [lo,hi) advance one of the induction vars cv[] by a
   non-zero step (++/--, += / -= / *= ... with non-zero rhs, or V = V +/- nz)? */
static int range_advances(ctx *c, size_t lo, size_t hi, char **cv, size_t ncv){
    size_t k, z;
    for (k=lo;k<hi;k++){
        const hc_token *t=T(c,k);
        if (t->kind!=HCT_IDENT) continue;
        int isc=0; for(z=0;z<ncv;z++) if(ieq(cv[z],t->text)){isc=1;break;}
        if (!isc) continue;
        if (k+1<hi && (tok_is(T(c,k+1),"++")||tok_is(T(c,k+1),"--"))) return 1;
        if (k>lo   && (tok_is(T(c,k-1),"++")||tok_is(T(c,k-1),"--"))) return 1;
        if (k+1<hi && (tok_is(T(c,k+1),"+=")||tok_is(T(c,k+1),"-=")||tok_is(T(c,k+1),"*=")||
                       tok_is(T(c,k+1),"/=")||tok_is(T(c,k+1),"<<=")||tok_is(T(c,k+1),">>=")||
                       tok_is(T(c,k+1),"|=")||tok_is(T(c,k+1),"&=")||tok_is(T(c,k+1),"^="))){
            size_t j=k+2; int nonzero=0, sawtok=0;
            while (j<hi && !tok_is(T(c,j),";") && !tok_is(T(c,j),",")){
                sawtok=1; if(!zero_literal(T(c,j))) nonzero=1; j++;
            }
            if (sawtok && nonzero) return 1;    /* i += 0u is NOT advancing */
        }
        if (k+1<hi && tok_is(T(c,k+1),"=")){
            size_t j=k+2; int refself=0, hasop=0, nonzero=0;
            while (j<hi && !tok_is(T(c,j),";") && !tok_is(T(c,j),",")){
                const hc_token *rt=T(c,j);
                if (rt->kind==HCT_IDENT && ieq(rt->text,t->text)) refself=1;
                if (tok_is(rt,"+")||tok_is(rt,"-")) hasop=1;
                if (!zero_literal(rt)) nonzero=1;
                j++;
            }
            if (refself && hasop && nonzero) return 1;
        }
    }
    return 0;
}

static void check_loops(ctx *c, hc_func *f)
{
    int S = strictf(f), CM = commitf(f);
    if (!(S||CM)) return;
    size_t i;
    for (i = f->body_start + 1; i < f->body_end; i++) {
        const hc_token *t = T(c, i);
        if (t->kind == HCT_IDENT && (ieq(t->text,"while")) && i+1<f->body_end && tok_is(T(c,i+1),"(")) {
            size_t cp = match_at(c, i+1, "(", ")");
            /* unbounded: condition has no identifier and is a truthy constant */
            size_t k; int has_ident=0, nonzero=0, only_zero=1;
            for (k=i+2;k<cp;k++){ const hc_token*ct=T(c,k);
                if (ct->kind==HCT_IDENT){ has_ident=1; if(ieq(ct->text,"true")) nonzero=1; }
                if (ct->kind==HCT_NUMBER){ if(!ieq(ct->text,"0")){ nonzero=1; only_zero=0; } } }
            if (!has_ident && (nonzero || (cp>i+2 && !only_zero)))
                emit(c,16,i,"statically unbounded while loop in phase kernel");
            else if (!has_ident && nonzero)
                emit(c,16,i,"statically unbounded while loop in phase kernel");
            scan_loop_range(c, f, i, i+2, cp);
        }
        if (t->kind == HCT_IDENT && ieq(t->text,"for") && i+1<f->body_end && tok_is(T(c,i+1),"(")) {
            size_t cp = match_at(c, i+1, "(", ")");
            /* split the three clauses at depth-0 ';' */
            size_t k; int d=0; size_t s1=0,s2=0;
            for (k=i+2;k<cp;k++){ const hc_token*ct=T(c,k);
                if (tok_is(ct,"(")) d++; else if (tok_is(ct,")")) d--;
                else if (tok_is(ct,";") && d==0){ if(!s1) s1=k; else if(!s2){ s2=k; } } }
            if (s1 && s2) {
                if (s1+1==s2) emit(c,16,i,"for(;;) empty-condition unbounded loop");
                /* empty increment clause (s2..cp) with no advance in body -> unbounded */
                if (s2+1==cp) {
                    /* does the loop body advance anything? look for ++/--/+=/-= */
                    size_t bo = cp+1; int adv=0;
                    if (bo<f->body_end && tok_is(T(c,bo),"{")) {
                        size_t bc=match_at(c,bo,"{","}"), z;
                        for (z=bo+1;z<bc;z++){ const hc_token*bt=T(c,z);
                            if (tok_is(bt,"++")||tok_is(bt,"--")||tok_is(bt,"+=")||tok_is(bt,"-=")) adv=1; }
                    }
                    if (!adv) emit(c,16,i,"for-loop with no induction advance (unbounded)");
                }
                /* non-empty cond AND non-empty step: the step (or body) must
                   advance a condition induction variable by a non-zero amount,
                   else the loop is non-advancing/infinite (w09 i+=0u, w16 j++
                   while testing i). */
                if (s1+1 != s2 && s2+1 != cp) {
                    char *cv[16]; size_t ncv=0, k2;
                    for (k2=s1+1;k2<s2 && ncv<16;k2++){
                        const hc_token *ct=T(c,k2);
                        if (ct->kind==HCT_IDENT && !ieq(ct->text,"true") && !ieq(ct->text,"false")){
                            size_t z; int dup=0; for(z=0;z<ncv;z++) if(ieq(cv[z],ct->text)){dup=1;break;}
                            if(!dup) cv[ncv++]=ct->text;
                        }
                    }
                    if (ncv>0) {
                        int adv = range_advances(c, s2+1, cp, cv, ncv);
                        if (!adv) {
                            size_t bo=cp+1;
                            if (bo<f->body_end && tok_is(T(c,bo),"{")) {
                                size_t bc=match_at(c,bo,"{","}");
                                adv = range_advances(c, bo+1, bc, cv, ncv);
                            } else {
                                size_t be=bo; while(be<f->body_end && !tok_is(T(c,be),";")) be++;
                                adv = range_advances(c, bo, be, cv, ncv);
                            }
                        }
                        if (!adv)
                            emit(c,16,i,"for-loop step never advances a condition induction variable (non-advancing/infinite)");
                    }
                }
                scan_loop_range(c, f, i, i+2, s1);   /* init usually benign, but cheap */
                scan_loop_range(c, f, i, s1, s2);    /* condition */
            }
        }
        if (t->kind == HCT_IDENT && ieq(t->text,"goto") && i+1<f->body_end && T(c,i+1)->kind==HCT_IDENT) {
            const char *lbl = T(c,i+1)->text; size_t k;
            for (k=f->body_start+1;k<i;k++)
                if (T(c,k)->kind==HCT_IDENT && ieq(T(c,k)->text,lbl) &&
                    k+1<f->body_end && tok_is(T(c,k+1),":")) { emit(c,16,i,"backward goto loop in phase kernel"); break; }
        }
    }
}

/* ---------- strict memory (E008/E012/E013/E014/E015) ---------- */
static int is_ro_param(hc_func *f, const char *name){ size_t i; for(i=0;i<f->nro;i++) if(ieq(f->ro_params[i],name)) return 1; return 0; }
static int is_param(hc_func *f, const char *name){ size_t i; for(i=0;i<f->nparams;i++) if(ieq(f->params[i],name)) return 1; return 0; }
static int is_ptr_param(hc_func *f, const char *name){ size_t i; for(i=0;i<f->nparams;i++) if(ieq(f->params[i],name)) return f->param_ptr[i]; return 0; }

static int tok_is_binop(const hc_token *t){
    return tok_is(t,"^")||tok_is(t,"+")||tok_is(t,"-")||tok_is(t,"|")||tok_is(t,"&")||
           tok_is(t,"<<")||tok_is(t,">>")||tok_is(t,"*")||tok_is(t,"/")||tok_is(t,"%");
}
/* Is local `id` ever assigned from an expression that both uses a binary
   operator AND references a parameter? (a shard-id transform like my_shard^1) */
static int local_from_param_arith(ctx *c, hc_func *f, const char *id){
    size_t k;
    for(k=f->body_start+1;k<f->body_end;k++){
        const hc_token *t=T(c,k);
        if(t->kind!=HCT_IDENT || !ieq(t->text,id) || k+1>=f->body_end) continue;
        const hc_token *n=T(c,k+1);
        int compound = tok_is(n,"^=")||tok_is(n,"+=")||tok_is(n,"-=")||tok_is(n,"|=")||tok_is(n,"&=");
        if(!(tok_is(n,"=")||compound)) continue;
        { size_t j=k+2; int hasop=compound, refp=0;
          while(j<f->body_end && !tok_is(T(c,j),";")){
              if(tok_is_binop(T(c,j))) hasop=1;
              if(T(c,j)->kind==HCT_IDENT && is_param(f,T(c,j)->text)) refp=1;
              j++;
          }
          if(hasop && refp) return 1; }
    }
    return 0;
}
/* Does the subscript expr in [lo,hi) address a SIBLING shard (param-derived)? */
static int index_is_sibling(ctx *c, hc_func *f, size_t lo, size_t hi){
    size_t k; int hasop=0, refp=0;
    for(k=lo;k<hi;k++){
        const hc_token *t=T(c,k);
        if(tok_is_binop(t)) hasop=1;
        if(t->kind==HCT_IDENT && is_param(f,t->text)) refp=1;
    }
    if(hasop && refp) return 1;                 /* e.g. all_slots[my_shard ^ 1] */
    for(k=lo;k<hi;k++){
        const hc_token *t=T(c,k);
        if(t->kind==HCT_IDENT && !is_param(f,t->text) && local_from_param_arith(c,f,t->text))
            return 1;                           /* victim = my_shard ^ 1; all[victim] */
    }
    return 0;
}
/* A pointer-parameter element index/offset that is NOT the kernel's own slot:
   either a sibling-shard transform (param-derived arithmetic) or a hard-coded
   constant literal index (writing to a fixed sibling slot regardless of shard).
   A loop-counter / shard-id index (bare local ident) is allowed. */
static int index_bad(ctx *c, hc_func *f, size_t lo, size_t hi){
    size_t k; int hasid=0, hasnum=0;
    if (index_is_sibling(c,f,lo,hi)) return 1;
    for(k=lo;k<hi;k++){ const hc_token *t=T(c,k);
        if(t->kind==HCT_IDENT) hasid=1;
        if(t->kind==HCT_NUMBER) hasnum=1; }
    if(!hasid && hasnum) return 1;             /* e.g. all_slots[0] / (base+2) */
    return 0;
}
static int is_lunion(ctx *c, const char *name){ size_t i; for(i=0;i<c->nlun;i++) if(ieq(c->lunions[i],name)) return 1; return 0; }

static void check_strict_mem(ctx *c, hc_func *f)
{
    if (!strictf(f)) return;
    size_t i;
    for (i = f->body_start + 1; i < f->body_end; i++) {
        const hc_token *t = T(c, i);

        /* E008: static (non-const) local mutable state in a phase/pure kernel */
        if (t->kind==HCT_IDENT && ieq(t->text,"static")) {
            size_t k=i+1; int sawconst=0;
            while (k<f->body_end && !tok_is(T(c,k),";") && !tok_is(T(c,k),"=") && !tok_is(T(c,k),"{")) {
                if (T(c,k)->kind==HCT_IDENT && ieq(T(c,k)->text,"const")) sawconst=1;
                k++;
            }
            if (!sawconst) emit(c,8,i,"static mutable local in PULL/BARRIER kernel (hidden shared state)");
        }

        /* E008/E012: write to file-scope var (direct, incl prefix/paren) */
        if (t->kind == HCT_IDENT) {
            hc_var *v = hc_find_var(c->u, t->text);
            if (v) {
                size_t j = i + 1; int chain=0;
                while (j < f->body_end) {
                    if (tok_is(T(c,j),".")||tok_is(T(c,j),"->")) { j += 2; chain=1; continue; }
                    if (tok_is(T(c,j),"[")) { j = match_at(c,j,"[","]")+1; chain=1; continue; }
                    break;
                }
                while (j < f->body_end && tok_is(T(c,j),")")) j++;
                const hc_token *nx = T(c,j);
                int isw = tok_is(nx,"=")||tok_is(nx,"+=")||tok_is(nx,"-=")||tok_is(nx,"*=")||
                    tok_is(nx,"/=")||tok_is(nx,"%=")||tok_is(nx,"|=")||tok_is(nx,"&=")||
                    tok_is(nx,"^=")||tok_is(nx,"<<=")||tok_is(nx,">>=")||tok_is(nx,"++")||tok_is(nx,"--");
                int pfxw = 0;
                if (i > f->body_start+1) { const hc_token *pv=T(c,i-1); if (tok_is(pv,"++")||tok_is(pv,"--")) pfxw=1; }
                if (isw || pfxw) {
                    if (v->is_pointer && !chain) emit(c,12,i,"pointer-typed global assigned (phase pointer escape)");
                    else if (!v->is_const) emit(c,8,i,"write to shared/file-scope mutable state in PULL/BARRIER");
                }
            }
        }

        /* E008: aliasing &g_var of a non-const file-scope var -> writable alias */
        if (tok_is(t,"&") && i+1<f->body_end && T(c,i+1)->kind==HCT_IDENT) {
            hc_var *v = hc_find_var(c->u, T(c,i+1)->text);
            if (v && !v->is_const) emit(c,8,i,"address of shared/file-scope var taken in PULL/BARRIER (writable alias)");
        }

        /* E008: capturing a writable pointer alias of a MUTABLE shared global.
           Covers mutable global array decay (`uint64_t *p = g_table;`) and a
           const-POINTER-to-mutable (`uint64_t *q = g_const_ptr;` where the
           global is `T * const`). `static const` LUTs (const pointee) are NOT
           flagged, so read-only tables stay accepted. */
        if (t->kind==HCT_IDENT) {
            hc_var *gv = hc_find_var(c->u, t->text);
            if (gv && (gv->is_array || gv->is_pointer) && !gv->is_const_pointee &&
                i>f->body_start+1 && tok_is(T(c,i-1),"=") &&
                i+1<f->body_end && tok_is(T(c,i+1),";")) {
                /* require the LHS of this initializer/assignment to be a pointer */
                size_t s=i-1; int star=0;
                while (s>f->body_start && !tok_is(T(c,s),";") &&
                       !tok_is(T(c,s),"{") && !tok_is(T(c,s),"}")) {
                    if (tok_is(T(c,s),"*")) star=1;
                    s--;
                }
                if (star) emit(c,8,i,"writable pointer alias of mutable shared global in PULL/BARRIER");
            }
        }

        /* E015 cross-shard: WRITE to a pointer-parameter element that is not the
           kernel's own slot. Three syntactic forms, all gated by index_bad()
           (sibling-shard transform OR hard-coded constant index) so a plain
           loop counter / own-shard index stays allowed:
             (1) subscript        all_slots[IDX].note = v
             (2) arrow arith      (all_slots + IDX)->note = v
             (3) deref arith      *(all_slots + IDX) = v                      */
        if (t->kind == HCT_IDENT && is_ptr_param(f, t->text) && i+1<f->body_end && tok_is(T(c,i+1),"[")) {
            size_t close = match_at(c,i+1,"[","]");
            size_t j=close+1; while(j<f->body_end){ if(tok_is(T(c,j),".")||tok_is(T(c,j),"->")){j+=2;continue;} break; }
            int iswrite = tok_is(T(c,j),"=")||tok_is(T(c,j),"+=")||tok_is(T(c,j),"^=")||tok_is(T(c,j),"|=")||tok_is(T(c,j),"-=");
            if (iswrite && index_bad(c,f,i+2,close))
                emit(c,15,i,"cross-shard write overlap (sibling/fixed shard slot)");
        }
        /* pointer-arith forms: ptr_param used inside a parenthesised +/- expr */
        if (t->kind == HCT_IDENT && is_ptr_param(f, t->text) && i>f->body_start+1 &&
            tok_is(T(c,i-1),"(") && i+1<f->body_end &&
            (tok_is(T(c,i+1),"+")||tok_is(T(c,i+1),"-"))) {
            size_t open=i-1, close=match_at(c,open,"(",")");
            if (close<f->body_end) {
                size_t j=close+1;
                int arrow=0;
                while(j<f->body_end){ if(tok_is(T(c,j),".")||tok_is(T(c,j),"->")){arrow=1;j+=2;continue;} break; }
                int iswrite = tok_is(T(c,j),"=")||tok_is(T(c,j),"+=")||tok_is(T(c,j),"^=")||tok_is(T(c,j),"|=")||tok_is(T(c,j),"-=");
                /* *(p + IDX) = v : outer paren preceded by a deref '*' */
                int deref = (open>f->body_start+1 && tok_is(T(c,open-1),"*"));
                if ((arrow || deref) && iswrite && index_bad(c,f,i+2,close))
                    emit(c,15,i,"cross-shard write overlap via pointer arithmetic (sibling/fixed shard slot)");
            }
        }

        /* E013/E014 const launder: a cast whose OPERAND is a readonly param used
           as a pointer. Forward scan unwraps arbitrary parentheses, address-of,
           deref, subscript and +/- pointer arithmetic in the operand so forms
           like (T *)(snap + 0) and (T *)&snap[0] are caught, while an integer
           read of a member such as (uint64_t)(snap->seed + i) is NOT. */
        if (tok_is(t,"(")) {
            size_t close = match_at(c,i,"(",")");
            if (close>i && close<f->body_end) {
                int star=0,cst=0,typ=0,intty=0; size_t z;
                for(z=i+1;z<close;z++){ const hc_token*zt=T(c,z);
                    if(tok_is(zt,"*"))star=1;
                    if(ieq(zt->text,"const"))cst=1;
                    if(zt->kind==HCT_IDENT) typ=1;
                    if(ieq(zt->text,"uintptr_t")||ieq(zt->text,"intptr_t")||ieq(zt->text,"size_t")||
                       ieq(zt->text,"long")||ieq(zt->text,"void")) intty=1; }
                int ptr_cast = (star && typ && !cst);   /* strips const on a pointer */
                int int_cast = intty;                   /* integer/void launder     */
                if (ptr_cast || int_cast) {
                    /* unwrap the operand expression following the cast */
                    size_t p=close+1; int prefixptr=0;
                    while (p<f->body_end &&
                           (tok_is(T(c,p),"&")||tok_is(T(c,p),"*")||tok_is(T(c,p),"("))) {
                        if (tok_is(T(c,p),"&")||tok_is(T(c,p),"*")) prefixptr=1;
                        p++;
                    }
                    if (p<f->body_end && T(c,p)->kind==HCT_IDENT && is_ro_param(f,T(c,p)->text)) {
                        const hc_token *after = (p+1<f->body_end)?T(c,p+1):NULL;
                        int member = after && (tok_is(after,"->")||tok_is(after,"."));
                        /* a bare member VALUE read (no address-of / deref prefix) is not a ptr use */
                        if (prefixptr || !member) {
                            if (ptr_cast) emit(c,13,i,"casting away const on readonly snapshot");
                            else          emit(c,14,i,"readonly snapshot laundered via integer/void cast");
                        }
                    }
                }
            }
        }

        /* E014 union pun: use of a launder union / typedef name */
        if (t->kind == HCT_IDENT && ieq(t->text,"union") && i+1<f->body_end &&
            T(c,i+1)->kind==HCT_IDENT && is_lunion(c, T(c,i+1)->text))
            emit(c,14,i,"union type-punning to bypass const immutability");
        if (t->kind == HCT_IDENT && is_lunion(c, t->text) &&
            !(i>f->body_start+1 && (tok_is(T(c,i-1),".")||tok_is(T(c,i-1),"->"))))
            emit(c,14,i,"const-launder union type used in PULL/BARRIER");
    }
}

/* ---------- call graph (E007 direct COMMIT, E011 leaks, F13 extern) ---------- */
static int body_has_indirect_call(ctx *c, hc_func *f)
{
    size_t i;
    for (i = f->body_start + 1; i < f->body_end; i++) {
        if (!tok_is(T(c,i),"(")) continue;
        if (i==0) continue;
        const hc_token *pv = T(c,i-1);
        if (tok_is(pv,"]")) return 1;                 /* table[...](...) */
        if (pv->kind==HCT_IDENT && i>=2 &&
            (tok_is(T(c,i-2),".")||tok_is(T(c,i-2),"->"))) return 1;  /* obj.fn(...) */
    }
    return 0;
}

static int in_allowlist(const char *s)
{
    return ispfx(s,"hc_")||ispfx(s,"__")||ieq(s,"memcpy")||ieq(s,"memmove")||
           ieq(s,"memset")||ieq(s,"memcmp")||ieq(s,"sizeof");
}

static void check_callgraph(ctx *c, hc_func *f, const int *leak)
{
    size_t i;
    if (!(f->phase==PH_PULL||f->phase==PH_BARRIER||f->reach_strict||f->is_pure)) return;
    int kernel = (f->phase==PH_PULL||f->phase==PH_BARRIER);

    /* F13: extern/unverified callee from a strict/pure function */
    for (i = 0; i < f->ncallees; i++) {
        const char *cn = f->callees[i];
        if (func_index(c->u, cn) >= 0) continue;       /* defined & verified in TU */
        if (in_allowlist(cn)) continue;
        emit(c, 11, f->body_start, "calls an unverified extern (not defined in TU / not HS_PURE)");
        break;
    }

    if (!kernel) return;

    /* E007 direct COMMIT call */
    for (i = 0; i < f->ncallees; i++) {
        int gi = func_index(c->u, f->callees[i]);
        if (gi >= 0 && c->u->funcs[gi].phase == PH_COMMIT)
            emit(c, 7, f->body_start, "PULL/BARRIER directly invokes a COMMIT phase function (skips BARRIER)");
    }
    /* E011 transitive leak via any successor */
    {
        int succ[4096]; size_t ns = succ_of(c, f, succ, 4096), k;
        for (k = 0; k < ns; k++)
            if (leak[succ[k]]) { emit(c,11,f->body_start,"transitive call-graph leak: PULL reaches a shared write / COMMIT mutator"); break; }
    }
    /* E011 indirect dispatch through a file-scope table holding a COMMIT/mutator */
    if (body_has_indirect_call(c, f)) {
        size_t z;
        for (z = 0; z < c->nft; z++) {
            int gi = func_index(c->u, c->ftaken[z]);
            if (gi >= 0 && (c->u->funcs[gi].phase == PH_COMMIT || writes_global(c, &c->u->funcs[gi]))) {
                emit(c,11,f->body_start,"indirect dispatch to a COMMIT/mutator via file-scope function table");
                break;
            }
        }
    }
    /* E011 bare fn-ref to a COMMIT/writer used as a value inside the kernel */
    for (i = f->body_start + 1; i < f->body_end; i++) {
        const hc_token *t = T(c, i);
        if (t->kind != HCT_IDENT) continue;
        if (i+1 < f->body_end && tok_is(T(c,i+1),"(")) continue;
        if (i>f->body_start+1 && (tok_is(T(c,i-1),".")||tok_is(T(c,i-1),"->"))) continue;
        int gi = func_index(c->u, t->text);
        if (gi >= 0 && &c->u->funcs[gi] != f &&
            (c->u->funcs[gi].phase == PH_COMMIT || writes_global(c, &c->u->funcs[gi])))
            emit(c, 11, i, "function-pointer escape into COMMIT/mutator from PULL");
    }
}

/* ---------- recursion cycle (E017) iterative 3-color DFS, O(V+E) ---------- */
static void check_recursion(ctx *c)
{
    size_t nf = c->u->nfuncs, r;
    if (nf == 0) return;
    int *color = (int *)calloc(nf, sizeof(int));          /* 0 white 1 grey 2 black */
    int *stkn  = (int *)malloc((nf + 1) * sizeof(int));   /* node stack */
    size_t *stki = (size_t *)malloc((nf + 1) * sizeof(size_t)); /* successor cursor */
    int *succ  = (int *)malloc((nf + 1) * sizeof(int));
    if (!color||!stkn||!stki||!succ) { free(color);free(stkn);free(stki);free(succ); return; }

    for (r = 0; r < nf; r++) {
        if (!phase_any(&c->u->funcs[r])) continue;
        if (color[r] == 2) continue;
        size_t sp = 0;
        stkn[sp] = (int)r; stki[sp] = 0; color[r] = 1; sp++;
        int found = 0;
        while (sp) {
            int ci = stkn[sp-1];
            hc_func *f = &c->u->funcs[ci];
            size_t ns = succ_of(c, f, succ, nf);
            if (stki[sp-1] < ns) {
                int gi = succ[stki[sp-1]];
                stki[sp-1]++;
                if (color[gi] == 1) { found = 1; break; }         /* back edge -> cycle */
                if (color[gi] == 0) { color[gi]=1; stkn[sp]=gi; stki[sp]=0; sp++; }
            } else {
                color[ci] = 2; sp--;
            }
        }
        if (found) {
            emit(c, 17, c->u->funcs[r].body_start, "recursion cycle reachable from a phase kernel");
            /* leave greys as-is; continue to next root (colors persist for O(V+E)) */
        }
    }
    free(color); free(stkn); free(stki); free(succ);
}

/* ---------- global collection: volatile vars/fields, launder unions, ftaken ---------- */
static void addname(char ***arr, size_t *n, const char *s)
{
    *arr = (char **)realloc(*arr, (*n+1)*sizeof(char*));
    (*arr)[(*n)++] = (char *)s;
}
static void collect_globals(ctx *c)
{
    const hc_token_list *tl = c->tl;
    size_t i; int depth = 0;
    for (i = 0; i + 1 < tl->count; i++) {
        const hc_token *t = &tl->toks[i];
        if (t->system) continue;
        if (tok_is(t,"{")) depth++;
        else if (tok_is(t,"}")) { if (depth) depth--; }

        /* volatile file-scope vars (depth 0) */
        if (depth == 0 && t->kind==HCT_IDENT && ieq(t->text,"volatile")) {
            size_t j=i+1;
            while (j<tl->count && !tok_is(&tl->toks[j],";") && !tok_is(&tl->toks[j],"{")) {
                if (tl->toks[j].kind==HCT_IDENT && j+1<tl->count &&
                    (tok_is(&tl->toks[j+1],";")||tok_is(&tl->toks[j+1],"[")||tok_is(&tl->toks[j+1],"=")||tok_is(&tl->toks[j+1],",")))
                    addname(&c->vols,&c->nvols,tl->toks[j].text);
                j++;
            }
        }
        /* volatile struct/union FIELD names (depth > 0) */
        if (depth > 0 && t->kind==HCT_IDENT && ieq(t->text,"volatile")) {
            size_t j=i+1;
            while (j<tl->count && !tok_is(&tl->toks[j],";") && !tok_is(&tl->toks[j],"{") && !tok_is(&tl->toks[j],"}")) {
                if (tl->toks[j].kind==HCT_IDENT && j+1<tl->count &&
                    (tok_is(&tl->toks[j+1],";")||tok_is(&tl->toks[j+1],"[")||tok_is(&tl->toks[j+1],",")))
                    addname(&c->volf,&c->nvolf,tl->toks[j].text);
                j++;
            }
        }
        /* named launder union: union NAME { ... } with ptr + (const|uintptr) */
        if (t->kind==HCT_IDENT && ieq(t->text,"union") && i+1<tl->count &&
            tl->toks[i+1].kind==HCT_IDENT && i+2<tl->count && tok_is(&tl->toks[i+2],"{")) {
            const char *nm = tl->toks[i+1].text;
            size_t open=i+2, close=match_at(c,open,"{","}"); int cst=0,star=0,upt=0; size_t z;
            for (z=open+1;z<close;z++){ if(ieq(tl->toks[z].text,"const"))cst=1; if(tok_is(&tl->toks[z],"*"))star=1;
                if(ieq(tl->toks[z].text,"uintptr_t")||ieq(tl->toks[z].text,"intptr_t"))upt=1; }
            if (star && (cst||upt)) addname(&c->lunions,&c->nlun,(char*)nm);
        }
        /* anonymous typedef union: typedef union { ... } NAME ; */
        if (t->kind==HCT_IDENT && ieq(t->text,"typedef") && i+1<tl->count &&
            tl->toks[i+1].kind==HCT_IDENT && ieq(tl->toks[i+1].text,"union") &&
            i+2<tl->count && tok_is(&tl->toks[i+2],"{")) {
            size_t open=i+2, close=match_at(c,open,"{","}"); int cst=0,star=0,upt=0; size_t z;
            for (z=open+1;z<close;z++){ if(ieq(tl->toks[z].text,"const"))cst=1; if(tok_is(&tl->toks[z],"*"))star=1;
                if(ieq(tl->toks[z].text,"uintptr_t")||ieq(tl->toks[z].text,"intptr_t"))upt=1; }
            if (star && (cst||upt) && close+1<tl->count && tl->toks[close+1].kind==HCT_IDENT)
                addname(&c->lunions,&c->nlun,(char*)tl->toks[close+1].text);
        }
    }
    /* File-scope address-taken functions: any ident that names a function,
       appearing OUTSIDE every function body (so declarations AND initializer
       lists like `static const op_fn_t g_ops[]={ fn };` are covered — F4).
       Using "outside any body" instead of brace depth is robust to the braces
       of file-scope aggregate initializers. */
    for (i = 0; i + 1 < tl->count; i++) {
        const hc_token *t = &tl->toks[i];
        size_t fi; int inbody = 0;
        if (t->system) continue;
        for (fi = 0; fi < c->u->nfuncs; fi++)
            if (i >= c->u->funcs[fi].body_start && i <= c->u->funcs[fi].body_end) { inbody = 1; break; }
        if (inbody) continue;
        if (t->kind==HCT_IDENT && func_index(c->u, t->text) >= 0 &&
            !(i+1<tl->count && tok_is(&tl->toks[i+1],"(")) &&
            !(i>0 && (tok_is(&tl->toks[i-1],".")||tok_is(&tl->toks[i-1],"->"))))
            addname(&c->ftaken,&c->nft,t->text);
    }
}

/* ---------- comment-stripped raw copy (preserve strings) ---------- */
static char *strip_comments_dup(const char *raw)
{
    size_t n = strlen(raw), i;
    char *o = (char *)malloc(n + 1);
    if (!o) return NULL;
    int st = 0; /* 0 normal, 1 line, 2 block, 3 string, 4 char */
    for (i = 0; i < n; i++) {
        char ch = raw[i];
        if (st==0) {
            if (ch=='/' && raw[i+1]=='/') { o[i]=' '; st=1; continue; }
            if (ch=='/' && raw[i+1]=='*') { o[i]=' '; st=2; continue; }
            if (ch=='"')  { o[i]=ch; st=3; continue; }
            if (ch=='\'') { o[i]=ch; st=4; continue; }
            o[i]=ch;
        } else if (st==1) { if (ch=='\n'){ o[i]=ch; st=0; } else o[i]=' '; }
        else if (st==2) { if (ch=='*'&&raw[i+1]=='/'){ o[i]=' '; o[i+1]=' '; i++; st=0; } else o[i]=(ch=='\n'?'\n':' '); }
        else if (st==3) { o[i]=ch; if (ch=='\\'&&raw[i+1]){ o[i+1]=raw[i+1]; i++; } else if (ch=='"') st=0; }
        else if (st==4) { o[i]=ch; if (ch=='\\'&&raw[i+1]){ o[i+1]=raw[i+1]; i++; } else if (ch=='\'') st=0; }
    }
    o[n]='\0';
    return o;
}

static int rc(const char *hay, const char *n){ return strstr(hay,n)!=NULL; }

static void raw_pass(ctx *c, const char *raw)
{
    if (raw == NULL) return;
    char *s = strip_comments_dup(raw);
    const char *scan = s ? s : raw;
    size_t firsttok = 0, z;
    for (z=0; z<c->tl->count; z++){ if(!c->tl->toks[z].system){ firsttok=z; break; } }

    /* F1/F2: any user reference to the compiler kill-switch macro */
    if (rc(scan,"__HC_COMPILER__"))
        emit(c,25,firsttok,"user source references __HC_COMPILER__ (verifier/backend split-brain)");

    /* banned includes (comment-stripped, so comments never trigger) */
    if (rc(scan,"<threads.h>")) emit(c,2,firsttok,"banned include <threads.h>");
    if (rc(scan,"<semaphore.h>")||rc(scan,"<sys/sem.h>")) emit(c,1,firsttok,"banned include <semaphore.h>/<sys/sem.h>");
    if (rc(scan,"<dlfcn.h>")) emit(c,24,firsttok,"banned include <dlfcn.h>");
    if (rc(scan,"<omp.h>")) emit(c,6,firsttok,"banned include <omp.h>");

    /* V1 + OpenMP: scan raw directive lines. The raw user source must NEVER
       contain a GNU linemarker (`# <n> "file" [flags]`) — gcc -E would honour a
       hand-written one and stamp the rest of the TU as system-header flag 3,
       slipping past provenance filtering. Also reject `#line` in component and
       `#pragma omp` (pulls in the GOMP lock runtime). */
    {
        int comp = (c->profile == HC_PROFILE_COMPONENT);
        const char *p = scan; int lm_done = 0;
        while (p && *p) {
            const char *q = p;
            while (*q==' '||*q=='\t') q++;
            if (*q=='#') {
                const char *d = q+1;
                while (*d==' '||*d=='\t') d++;
                if (!lm_done && isdigit((unsigned char)*d)) {
                    emit(c,25,firsttok,"hand-written GNU linemarker forges source provenance (system-header flag)");
                    lm_done = 1;
                } else if (comp && !strncmp(d,"line",4) &&
                           (d[4]==' '||d[4]=='\t'||d[4]=='\0'||d[4]=='\n')) {
                    emit(c,25,firsttok,"#line provenance override not permitted in component profile");
                } else if (!strncmp(d,"line",4)) {
                    /* runtime: reject #line pointing at a system path */
                    const char *nl=strchr(d,'\n'); const char *dq=strchr(d,'"');
                    if (dq && (!nl||dq<nl)) {
                        const char *sys1=strstr(dq,"/usr/"), *sys2=strstr(dq,"<");
                        if ((sys1 && (!nl||sys1<nl))||(sys2 && (!nl||sys2<nl)))
                            emit(c,25,firsttok,"#line points at a system path (forged provenance)");
                    }
                } else if (!strncmp(d,"pragma",6)) {
                    const char *w=d+6;
                    while (*w==' '||*w=='\t') w++;
                    if (!strncmp(w,"omp",3) &&
                        (w[3]==' '||w[3]=='\t'||w[3]=='\n'||w[3]=='\0'||w[3]=='\r'))
                        emit(c,1,firsttok,"#pragma omp introduces a hidden GOMP lock runtime");
                }
            }
            { const char *nl=strchr(p,'\n'); if(!nl) break; p=nl+1; }
        }
    }

    /* E025: ## token paste smuggling a banned symbol not present in raw text */
    if (rc(scan,"##")) {
        size_t i;
        for (i=0;i<c->tl->count;i++){
            const hc_token *t=&c->tl->toks[i];
            if (t->system || t->kind!=HCT_IDENT) continue;
            if (ispfx(t->text,"pthread_mutex")||ispfx(t->text,"pthread_rwlock")||
                ispfx(t->text,"pthread_cond")||ispfx(t->text,"pthread_spin")) {
                if (!rc(scan, t->text)) { emit(c,25,i,"preprocessor ## token-paste smuggling a banned symbol"); break; }
            }
        }
    }

    /* E026: __asm__("<banned symbol>") symbol redirection on a declaration */
    {
        const char *keys[] = {"__asm__","asm","__attribute__"}; size_t ki;
        for (ki=0; ki<3; ki++) {
            const char *q = scan;
            while ((q = strstr(q, keys[ki])) != NULL) {
                const char *paren = strchr(q, '(');
                const char *stop = strchr(q, ';');
                if (paren && (!stop || paren < stop)) {
                    const char *dq = strchr(paren, '"');
                    if (dq && (!stop || dq < stop + 80)) {
                        char sym[160]; size_t si=0; const char *r=dq+1;
                        while (*r && *r!='"' && si<sizeof(sym)-1) {
                            if (*r=='\\' && (r[1]=='x')) { /* hex escape -> decode one byte */
                                int v=0,nd=0; r+=2;
                                while (isxdigit((unsigned char)*r)&&nd<2){ char h=*r; int d=(h<='9')?h-'0':(tolower(h)-'a'+10); v=v*16+d; r++; nd++; }
                                if (si<sizeof(sym)-1) sym[si++]=(char)v;
                            } else sym[si++]=*r++;
                        }
                        sym[si]='\0';
                        if (ispfx(sym,"pthread_mutex")||ispfx(sym,"pthread_rwlock")||
                            ispfx(sym,"pthread_cond")||ispfx(sym,"pthread_spin")||
                            ispfx(sym,"sem_")||ispfx(sym,"mtx_"))
                            emit(c,26,firsttok,"linker/asm symbol redirection to a banned symbol");
                    }
                }
                q += strlen(keys[ki]);
            }
        }
    }

    /* E024: split/hex adjacent string literal concatenation -> banned symbol */
    {
        size_t i;
        for (i=0;i<c->tl->count;i++){
            const hc_token *t=&c->tl->toks[i];
            if (t->system || t->kind!=HCT_STRING) continue;
            char acc[256]; size_t ai=0; size_t j=i;
            while (j<c->tl->count && c->tl->toks[j].kind==HCT_STRING) {
                char dec[256]; size_t dl=hc_decode_string_literal(c->tl->toks[j].text,dec,sizeof(dec));
                size_t m; for(m=0;m<dl && ai<sizeof(acc)-1;m++) acc[ai++]=dec[m];
                j++;
            }
            acc[ai]='\0';
            if (ispfx(acc,"pthread_mutex")||ispfx(acc,"pthread_rwlock")||ispfx(acc,"pthread_cond")||
                ispfx(acc,"pthread_spin")||ieq(acc,"pthread_create"))
                emit(c,24,i,"string-constructed banned symbol (split/hex smuggle)");
            i=j-1;
        }
    }
    free(s);
}

int hc_verify(hc_unit *u, const hc_token_list *tl,
              const char *rawsrc, const char *srcname, hc_profile profile)
{
    ctx c; size_t k, nf;
    memset(&c, 0, sizeof(c));
    c.u = u; c.tl = tl; c.srcname = srcname; c.profile = profile;

    propagate_reach(&c);
    collect_globals(&c);

    /* leak[] : can this function reach a shared-write / COMMIT / PUBLISH node?  (O(V+E)) */
    nf = u->nfuncs;
    int *leak = NULL;
    if (nf) {
        leak = (int *)calloc(nf, sizeof(int));
        int *qq = (int *)malloc((nf+1)*sizeof(int));
        int *succ = (int *)malloc((nf+1)*sizeof(int));
        /* reverse adjacency */
        size_t *radj_off = (size_t *)calloc(nf+1, sizeof(size_t));
        if (leak && qq && succ && radj_off) {
            size_t i; size_t tote=0;
            for (i=0;i<nf;i++){ size_t ns=succ_of(&c,&u->funcs[i],succ,nf); tote+=ns; radj_off[i]=ns; }
            /* build CSR reverse graph */
            size_t *cnt = (size_t*)calloc(nf,sizeof(size_t));
            int *redges = (int*)malloc((tote+1)*sizeof(int));
            size_t *roff = (size_t*)calloc(nf+1,sizeof(size_t));
            if (cnt && redges && roff) {
                for (i=0;i<nf;i++){ size_t ns=succ_of(&c,&u->funcs[i],succ,nf),z; for(z=0;z<ns;z++) cnt[succ[z]]++; }
                for (i=0;i<nf;i++) roff[i+1]=roff[i]+cnt[i];
                size_t *cur=(size_t*)malloc(nf*sizeof(size_t)); for(i=0;i<nf;i++) cur[i]=roff[i];
                for (i=0;i<nf;i++){ size_t ns=succ_of(&c,&u->funcs[i],succ,nf),z; for(z=0;z<ns;z++){ int g=succ[z]; redges[cur[g]++]=(int)i; } }
                /* seed */
                size_t qh=0,qt=0;
                for (i=0;i<nf;i++){ if (u->funcs[i].phase==PH_COMMIT||u->funcs[i].phase==PH_PUBLISH||writes_global(&c,&u->funcs[i])) { leak[i]=1; qq[qt++]=(int)i; } }
                while (qh<qt){ int n=qq[qh++]; size_t e; for(e=roff[n];e<roff[n+1];e++){ int p=redges[e]; if(!leak[p]){ leak[p]=1; qq[qt++]=p; } } }
                free(cur);
            }
            free(cnt); free(redges); free(roff);
        }
        free(qq); free(succ); free(radj_off);
    }

    for (k = 0; k < nf; k++) {
        hc_func *f = &u->funcs[k];
        check_bans(&c, f);
        check_asm(&c, f);
        check_loops(&c, f);
        check_strict_mem(&c, f);
        if (leak) check_callgraph(&c, f, leak);
    }
    check_recursion(&c);
    check_toplevel_asm(&c);
    raw_pass(&c, rawsrc);

    free(leak);
    free(c.vols); free(c.volf); free(c.lunions); free(c.ftaken);
    return c.count;
}
