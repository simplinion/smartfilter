#ifndef SMARTFILTER_LEVEL2_H
#define SMARTFILTER_LEVEL2_H

#include "base/base.h"

typedef struct {
    bool compact;
    size_t minimum_prefix; /* Bytes; shorter common prefixes stay visible. */
} level2_config;

typedef struct {
    char *previous;
    size_t previous_length;
    size_t previous_capacity;
    char *rendered;
    size_t rendered_capacity;
    bool allocation_failed;
} level2_context;

/* Zero-initialize before use; destroy even after a processing error. */
sf_level level2_create(level2_context *context, const level2_config *config);
void level2_destroy(level2_context *context);

#endif
