/* SPDX-License-Identifier: MIT */
#ifndef HC_ELF_AUDIT_H
#define HC_ELF_AUDIT_H

#include "hc_rules.h"

/*
 * Pure-C ELF64 (little-endian) zero-lock symbol audit of a .o or dynamic
 * binary. Reads .symtab/.strtab and .dynsym/.dynstr directly. Returns the
 * number of banned symbols found (0 = clean). Offenders printed to stderr.
 */
int hc_elf_audit(const char *path, hc_profile profile);

#endif /* HC_ELF_AUDIT_H */
