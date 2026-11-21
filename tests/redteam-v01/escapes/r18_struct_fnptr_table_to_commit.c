/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_SHARED static uint64_t g_secret_ledger = 0;

HS_PHASE_COMMIT
static uint64_t hidden_commit_fn(uint64_t x)
{
    g_secret_ledger ^= x;
    return g_secret_ledger;
}

typedef uint64_t (*op_fn_t)(uint64_t);
static const op_fn_t g_ops[1] = { hidden_commit_fn };

HS_PHASE_PULL
uint64_t r18_table_dispatch(uint64_t phase)
{
    return g_ops[0](phase);
}
