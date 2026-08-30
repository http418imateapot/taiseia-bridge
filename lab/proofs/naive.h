/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * See naive.c. Demonstration only; nothing in the program links this.
 */
#ifndef TSB_LAB_PROOFS_NAIVE_H
#define TSB_LAB_PROOFS_NAIVE_H

#include <stdint.h>

int32_t naive_sigma8(uint8_t lo);
int32_t naive_sigma16(uint16_t field);

int32_t defined_sigma8(uint8_t lo);
int32_t defined_sigma16(uint16_t field);

#endif /* TSB_LAB_PROOFS_NAIVE_H */
