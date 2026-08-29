/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tests.h"

int g_checks;
int g_failures;

int main(void)
{
    test_frame();
    test_datatype();
    test_sim_link();

    printf("%d checks, %d failures\n", g_checks, g_failures);

    return g_failures == 0 ? 0 : 1;
}
