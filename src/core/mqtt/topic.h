/*
 * Copyright 2026 Calvin
 * SPDX-License-Identifier: Apache-2.0
 *
 * Topic templates.
 *
 * Every deployment has its own topic conventions, and a bridge that hard-codes
 * one is useful in exactly one place. Making only the prefix configurable is
 * the usual half-measure -- it lets an operator change a version segment and
 * nothing else, when the layout below it is what actually differs.
 *
 * So each topic is a template with two placeholders, and rendering is a pure
 * function that can be checked at startup rather than at publish time. A topic
 * that turns out not to fit is a poor thing to discover once per reporting
 * interval on a device already in the field.
 */
#ifndef TSB_CORE_MQTT_TOPIC_H
#define TSB_CORE_MQTT_TOPIC_H

#include "core/util/result.h"

#include <stddef.h>

/* MQTT allows far longer, but a topic this size is already a design smell and
 * the ceiling keeps every packet buffer in the program bounded. */
#define TOPIC_MAX           160u
#define TOPIC_TEMPLATE_MAX  128u

/**
 * Render `tmpl` into `out`, substituting {prefix} and {id}.
 *
 * An unknown placeholder or an unclosed brace is RES_EFORMAT rather than being
 * passed through: a topic containing a literal "{tenant}" would publish
 * somewhere nobody is listening, and silence is the hardest failure to notice.
 *
 * RES_ETOOBIG if the result does not fit.
 */
result_t topic_render(char *out, size_t cap, const char *tmpl,
                      const char *prefix, const char *device_id);

/**
 * Check a whole topic set at startup: each must render and fit, and no two may
 * be equal.
 *
 * Duplicates matter more than they look. Two publishers on one topic makes a
 * consumer see alternating payload shapes, which reads as data corruption
 * rather than as the configuration mistake it is.
 *
 * `which` receives the index of the offending template, or -1 for a duplicate
 * pair reported at the later of the two.
 */
result_t topic_check_set(const char *const *tmpls, size_t n,
                         const char *prefix, const char *device_id,
                         size_t *which);

#endif /* TSB_CORE_MQTT_TOPIC_H */
