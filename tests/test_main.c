/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tests.h"

int g_checks;
int g_failures;

int main(void)
{
    test_json();
    test_frame();
    test_datatype();
    test_registration();
    test_sim_link();
    test_appliance();
    test_payload();
    test_config();
    test_mqtt();

    printf("%d checks, %d failures\n", g_checks, g_failures);

    return g_failures == 0 ? 0 : 1;
}
