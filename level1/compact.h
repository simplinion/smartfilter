#ifndef SMARTFILTER_COMPACT_H
#define SMARTFILTER_COMPACT_H

#include "base/base.h"

typedef struct {
    bool compact;
    size_t minimum_prefix; /* Bytes; shorter common prefixes stay visible. */
} compact_config;

typedef struct {
    char *previous;
    size_t previous_length;
    size_t previous_capacity;
    char *rendered;
    size_t rendered_capacity;
    bool allocation_failed;
} compact_context;

/* Zero-initialize before use; destroy even after a processing error. */
bool compact_filter(void *context, const void *config, sf_line line, sf_output *output);
void compact_destroy(compact_context *context);

#endif
