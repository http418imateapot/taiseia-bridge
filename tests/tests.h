/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * A test harness small enough to read in one sitting. Checks do not abort:
 * one failure should not hide the next twenty, because the pattern across
 * failures is usually more informative than the first one alone.
 */
#ifndef TSB_TESTS_H
#define TSB_TESTS_H

#include <stdio.h>
#include <string.h>

extern int g_checks;
extern int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

#define CHECK_EQ(got, want)                                                  \
    do {                                                                     \
        long long g_ = (long long)(got);                                     \
        long long w_ = (long long)(want);                                    \
        g_checks++;                                                          \
        if (g_ != w_) {                                                      \
            g_failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d  %s == %s (got %lld, want %lld)\n",  \
                    __FILE__, __LINE__, #got, #want, g_, w_);                \
        }                                                                    \
    } while (0)

#define CHECK_STREQ(got, want)                                               \
    do {                                                                     \
        const char *g_ = (got);                                              \
        const char *w_ = (want);                                             \
        g_checks++;                                                          \
        if (!g_ || !w_ || strcmp(g_, w_) != 0) {                             \
            g_failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d  %s == \"%s\" (got \"%s\")\n",       \
                    __FILE__, __LINE__, #got, w_ ? w_ : "(null)",            \
                    g_ ? g_ : "(null)");                                     \
        }                                                                    \
    } while (0)

void test_json(void);
void test_frame(void);
void test_datatype(void);
void test_registration(void);
void test_sim_link(void);
void test_appliance(void);
void test_payload(void);
void test_config(void);
void test_mqtt(void);
void test_client(void);
void test_platform(void);
void test_uart(void);

#endif /* TSB_TESTS_H */
