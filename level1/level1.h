#ifndef SMARTFILTER_LEVEL1_H
#define SMARTFILTER_LEVEL1_H

#include "base/base.h"

typedef struct {
    bool drop_empty_lines;
} level1_config;

typedef struct {
    size_t lines_seen;
    size_t lines_dropped;
} level1_context;

sf_level level1_create(level1_context *context, const level1_config *config);

#endif
