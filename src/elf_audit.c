/* SPDX-License-Identifier: MIT */
#include "hc_elf_audit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint16_t rd16(const unsigned char *p){ return (uint16_t)(p[0]|(p[1]<<8)); }
static uint32_t rd32(const unsigned char *p){ return (uint32_t)(p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24)); }
static uint64_t rd64(const unsigned char *p){ return (uint64_t)rd32(p) | ((uint64_t)rd32(p+4)<<32); }

/* exact-match against a name whose length is nlen (version suffix stripped) */
static int nmatch(const char *s, size_t nlen, const char *lit)
{
    return strlen(lit) == nlen && strncmp(s, lit, nlen) == 0;
}
static int npfx(const char *s, size_t nlen, const char *pfx)
{
    size_t pl = strlen(pfx);
    return nlen >= pl && strncmp(s, pfx, pl) == 0;
}

/* Always banned in BOTH profiles (zero exceptions). */
static int banned_always(const char *s, size_t nlen)
{
    return npfx(s,nlen,"pthread_mutex")||npfx(s,nlen,"pthread_rwlock")||
           npfx(s,nlen,"pthread_cond")||npfx(s,nlen,"pthread_spin")||
           npfx(s,nlen,"pthread_barrier")||npfx(s,nlen,"pthread_once")||
           npfx(s,nlen,"sem_")||npfx(s,nlen,"mtx_")||npfx(s,nlen,"cnd_")||
           nmatch(s,nlen,"call_once")||npfx(s,nlen,"tss_")||
           nmatch(s,nlen,"flock")||nmatch(s,nlen,"lockf")||
           nmatch(s,nlen,"dlopen")||nmatch(s,nlen,"dlsym")||nmatch(s,nlen,"dlvsym")||
           npfx(s,nlen,"arc4random")||nmatch(s,nlen,"rand")||nmatch(s,nlen,"srand")||
           nmatch(s,nlen,"random")||nmatch(s,nlen,"srandom")||nmatch(s,nlen,"drand48")||
           nmatch(s,nlen,"lrand48")||nmatch(s,nlen,"mrand48")||nmatch(s,nlen,"getrandom")||
           nmatch(s,nlen,"gettimeofday")||nmatch(s,nlen,"__gettimeofday")||
           nmatch(s,nlen,"omp_set_lock")||nmatch(s,nlen,"omp_set_nest_lock")||
           npfx(s,nlen,"GOMP_")||npfx(s,nlen,"__kmpc_")||
           /* F8/F9: process spawning & entropy & 16-byte lock-backed atomics */
           nmatch(s,nlen,"system")||nmatch(s,nlen,"popen")||nmatch(s,nlen,"pclose")||
           nmatch(s,nlen,"getentropy")||nmatch(s,nlen,"getauxval")||
           nmatch(s,nlen,"posix_spawn")||nmatch(s,nlen,"posix_spawnp")||
           nmatch(s,nlen,"__atomic_compare_exchange_16")||nmatch(s,nlen,"__atomic_exchange_16")||
           nmatch(s,nlen,"__atomic_load_16")||nmatch(s,nlen,"__atomic_store_16")||
           nmatch(s,nlen,"__atomic_fetch_add_16")||nmatch(s,nlen,"time")||nmatch(s,nlen,"ftime");
}

/* Additionally banned in COMPONENT profile (allowed only in RUNTIME). */
static int banned_component(const char *s, size_t nlen)
{
    return nmatch(s,nlen,"pthread_create")||nmatch(s,nlen,"pthread_join")||
           nmatch(s,nlen,"pthread_detach")||nmatch(s,nlen,"thrd_create")||
           nmatch(s,nlen,"thrd_join")||nmatch(s,nlen,"clone")||nmatch(s,nlen,"fork")||
           nmatch(s,nlen,"vfork")||nmatch(s,nlen,"syscall")||
           nmatch(s,nlen,"clock_gettime")||nmatch(s,nlen,"timespec_get")||
           nmatch(s,nlen,"clock")||
           nmatch(s,nlen,"sleep")||nmatch(s,nlen,"usleep")||nmatch(s,nlen,"nanosleep")||
           nmatch(s,nlen,"clock_nanosleep")||nmatch(s,nlen,"sched_yield")||
           nmatch(s,nlen,"sched_getcpu")||nmatch(s,nlen,"gettid")||
           nmatch(s,nlen,"pthread_self")||nmatch(s,nlen,"select")||nmatch(s,nlen,"pselect")||
           nmatch(s,nlen,"poll")||nmatch(s,nlen,"ppoll")||nmatch(s,nlen,"epoll_wait")||
           nmatch(s,nlen,"epoll_pwait")||nmatch(s,nlen,"waitpid")||nmatch(s,nlen,"wait4")||
           /* F8/F9: timers, non-local jumps, process identity/env, SysV semaphores */
           nmatch(s,nlen,"thrd_sleep")||nmatch(s,nlen,"thrd_yield")||
           nmatch(s,nlen,"clock_getres")||nmatch(s,nlen,"timer_create")||
           nmatch(s,nlen,"timer_settime")||nmatch(s,nlen,"times")||
           nmatch(s,nlen,"setitimer")||nmatch(s,nlen,"alarm")||
           nmatch(s,nlen,"setjmp")||nmatch(s,nlen,"_setjmp")||nmatch(s,nlen,"__sigsetjmp")||
           nmatch(s,nlen,"sigsetjmp")||nmatch(s,nlen,"longjmp")||nmatch(s,nlen,"siglongjmp")||
           nmatch(s,nlen,"_longjmp")||nmatch(s,nlen,"__longjmp_chk")||
           nmatch(s,nlen,"getpid")||nmatch(s,nlen,"getppid")||
           nmatch(s,nlen,"semop")||nmatch(s,nlen,"semget")||nmatch(s,nlen,"semctl")||
           nmatch(s,nlen,"semtimedop");
}

int hc_elf_audit(const char *path, hc_profile profile)
{
    FILE *fp = fopen(path, "rb");
    unsigned char *buf = NULL;
    long szl;
    uint64_t sz;
    int offenders = 0;
    uint64_t e_shoff;
    uint16_t e_shentsize, e_shnum, i;

    if (fp == NULL) { fprintf(stderr, "hc: audit: cannot open %s\n", path); return -1; }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    szl = ftell(fp);
    if (szl < 0) { fclose(fp); return -1; }
    rewind(fp);
    sz = (uint64_t)szl;
    /* fail closed on anything too small to be a valid ELF64 header */
    if (sz < 64) { fprintf(stderr, "hc: audit: %s too small / not ELF64\n", path); fclose(fp); return -1; }
    buf = (unsigned char *)malloc((size_t)sz);
    if (buf == NULL) { fclose(fp); return -1; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) { free(buf); fclose(fp); return -1; }
    fclose(fp);

    /* fail closed: must be ELF, 64-bit, little-endian */
    if (memcmp(buf, "\x7f""ELF", 4) != 0 || buf[4] != 2 /*ELFCLASS64*/ || buf[5] != 1 /*ELFDATA2LSB*/) {
        fprintf(stderr, "hc: audit: %s is not a little-endian ELF64 object\n", path);
        free(buf);
        return -1;
    }

    e_shoff = rd64(buf + 0x28);
    e_shentsize = rd16(buf + 0x3a);
    e_shnum = rd16(buf + 0x3c);

    /* overflow-safe section-header-table bounds */
    if (e_shnum == 0 || e_shentsize < 64 || e_shoff > sz ||
        (uint64_t)e_shnum > (sz - e_shoff) / e_shentsize) {
        fprintf(stderr, "hc: audit: %s has invalid/overflowing section header table\n", path);
        free(buf);
        return -1;
    }

    {
    int saw_symtab = 0;
    for (i = 0; i < e_shnum; i++) {
        const unsigned char *sh = buf + e_shoff + (uint64_t)i * e_shentsize;
        uint32_t sh_type = rd32(sh + 4);
        uint64_t sh_offset, sh_size, sh_entsize, str_off, str_size, n, k;
        uint32_t sh_link;
        const unsigned char *strsh;
        if (sh_type != 2 /*SYMTAB*/ && sh_type != 11 /*DYNSYM*/) continue;
        saw_symtab = 1;
        sh_offset = rd64(sh + 0x18);
        sh_size   = rd64(sh + 0x20);
        sh_link   = rd32(sh + 0x28);
        sh_entsize= rd64(sh + 0x38);
        /* validate the symbol section */
        if (sh_entsize < 24 || sh_link >= e_shnum ||
            sh_offset > sz || sh_size > sz - sh_offset) {
            fprintf(stderr, "hc: audit: %s has invalid symbol section\n", path);
            free(buf); return -1;
        }
        strsh = buf + e_shoff + (uint64_t)sh_link * e_shentsize;
        str_off = rd64(strsh + 0x18);
        str_size = rd64(strsh + 0x20);
        if (str_off > sz || str_size > sz - str_off) {
            fprintf(stderr, "hc: audit: %s has invalid string table\n", path);
            free(buf); return -1;
        }
        n = sh_size / sh_entsize;
        for (k = 0; k < n; k++) {
            const unsigned char *sym = buf + sh_offset + k * sh_entsize;
            uint32_t st_name = rd32(sym + 0);
            const char *name;
            size_t nlen, avail;
            if (st_name == 0 || (uint64_t)st_name >= str_size) continue;
            name = (const char *)(buf + str_off + st_name);
            avail = (size_t)(str_size - st_name);
            /* require NUL-termination within the string table */
            if (memchr(name, '\0', avail) == NULL) continue;
            nlen = strcspn(name, "@");   /* strip @GLIBC_.. / @@.. version suffix */
            if (nlen == 0) continue;
            if (banned_always(name, nlen) ||
                (profile == HC_PROFILE_COMPONENT && banned_component(name, nlen))) {
                fprintf(stderr, "hc: ELF64 audit FAIL: %s imports/defines banned symbol '%.*s'\n",
                        path, (int)nlen, name);
                offenders++;
            }
        }
    }
    if (!saw_symtab) {
        /* F9: a fully-linked binary with neither .symtab nor .dynsym cannot be
           proven lock-free (e.g. `gcc -static -s`). Fail closed. */
        fprintf(stderr, "hc: ELF64 audit FAIL: %s has no symbol table "
                "(stripped/static) — cannot prove zero-lock; failing closed\n", path);
        offenders++;
    }
    }
    free(buf);
    return offenders;
}
