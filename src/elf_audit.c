/* SPDX-License-Identifier: MIT */
#include "hc_elf_audit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint16_t rd16(const unsigned char *p){ return (uint16_t)(p[0]|(p[1]<<8)); }
static uint32_t rd32(const unsigned char *p){ return (uint32_t)(p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24)); }
static uint64_t rd64(const unsigned char *p){ return (uint64_t)rd32(p) | ((uint64_t)rd32(p+4)<<32); }

static int banned_lock_sym(const char *s)
{
    /* strip GLIBC version suffix */
    if (s == NULL || s[0] == '\0') return 0;
    if (!strncmp(s,"pthread_mutex",13)||!strncmp(s,"pthread_rwlock",14)||
        !strncmp(s,"pthread_cond",12)||!strncmp(s,"pthread_spin",12)||
        !strncmp(s,"pthread_barrier",15)||!strncmp(s,"pthread_once",12)||
        !strncmp(s,"sem_",4)||!strncmp(s,"mtx_",4)||!strncmp(s,"cnd_",4)||
        !strcmp(s,"call_once")||!strncmp(s,"tss_",4)||
        !strcmp(s,"flock")||!strcmp(s,"lockf")||
        !strcmp(s,"dlopen")||!strcmp(s,"dlsym")||!strcmp(s,"dlvsym")||
        !strcmp(s,"omp_set_lock")||!strcmp(s,"omp_set_nest_lock"))
        return 1;
    return 0;
}

int hc_elf_audit(const char *path, hc_profile profile)
{
    FILE *fp = fopen(path, "rb");
    unsigned char *buf = NULL;
    long sz;
    int offenders = 0;
    (void)profile;
    if (fp == NULL) { fprintf(stderr, "hc: audit: cannot open %s\n", path); return -1; }
    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz < 64) { fclose(fp); return 0; }
    buf = (unsigned char *)malloc((size_t)sz);
    if (buf == NULL) { fclose(fp); return -1; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) { free(buf); fclose(fp); return -1; }
    fclose(fp);

    if (memcmp(buf, "\x7f""ELF", 4) != 0 || buf[4] != 2 /*ELFCLASS64*/) { free(buf); return 0; }

    uint64_t e_shoff = rd64(buf + 0x28);
    uint16_t e_shentsize = rd16(buf + 0x3a);
    uint16_t e_shnum = rd16(buf + 0x3c);
    uint16_t i;

    for (i = 0; i < e_shnum; i++) {
        const unsigned char *sh = buf + e_shoff + (uint64_t)i * e_shentsize;
        uint32_t sh_type = rd32(sh + 4);
        if (sh_type != 2 /*SYMTAB*/ && sh_type != 11 /*DYNSYM*/) continue;
        uint64_t sh_offset = rd64(sh + 0x18);
        uint64_t sh_size   = rd64(sh + 0x20);
        uint32_t sh_link   = rd32(sh + 0x28);
        uint64_t sh_entsize= rd64(sh + 0x38);
        if (sh_entsize == 0) continue;
        const unsigned char *strsh = buf + e_shoff + (uint64_t)sh_link * e_shentsize;
        uint64_t str_off = rd64(strsh + 0x18);
        uint64_t str_size = rd64(strsh + 0x20);
        uint64_t n = sh_size / sh_entsize, k;
        for (k = 0; k < n; k++) {
            const unsigned char *sym = buf + sh_offset + k * sh_entsize;
            uint32_t st_name = rd32(sym + 0);
            if (st_name == 0 || str_off + st_name >= (uint64_t)sz) continue;
            const char *name = (const char *)(buf + str_off + st_name);
            if ((uint64_t)st_name >= str_size) continue;
            if (banned_lock_sym(name)) {
                fprintf(stderr, "hc: ELF64 audit FAIL: %s imports/defines banned lock symbol '%s'\n", path, name);
                offenders++;
            }
        }
    }
    free(buf);
    return offenders;
}
