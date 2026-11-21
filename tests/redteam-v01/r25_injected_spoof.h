/* SPDX-License-Identifier: MIT */
#include <stdatomic.h>
static atomic_flag g_injected_spin = ATOMIC_FLAG_INIT;
__attribute__((constructor)) static void r25_injected_ctor(void) {
    while (atomic_flag_test_and_set_explicit(&g_injected_spin, memory_order_acquire)) {}
    atomic_flag_clear_explicit(&g_injected_spin, memory_order_release);
}
