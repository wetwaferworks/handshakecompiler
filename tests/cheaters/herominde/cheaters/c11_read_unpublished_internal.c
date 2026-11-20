/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

struct fake_hs_internal {
    uint64_t pub_seq;
    uint64_t pub_phase;
    uint64_t ledger_hash;
};

HS_PHASE_PULL
uint64_t c11_cheat_unpublished(struct fake_hs_internal *hs)
{
    /* Reading staging fields without seqlock check and writing ledger_hash directly */
    hs->ledger_hash = hc_fnv1a64_step(hs->ledger_hash, hs->pub_phase);
    return hs->pub_seq;
}
