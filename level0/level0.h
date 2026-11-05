#ifndef SMARTFILTER_LEVEL0_H
#define SMARTFILTER_LEVEL0_H

#include "base/base.h"

typedef enum {
    LOG_UNKNOWN,
    LOG_VERBOSE,
    LOG_INFO,
    LOG_ERROR,
    LOG_WARNING,
    LOG_TYPE_COUNT
} log_type;

typedef struct {
    bool hide_verbose;
    bool hide_info;
    bool hide_error;
    bool hide_warning;
} level0_config;

typedef struct {
    size_t lines_seen;
    size_t lines_dropped;
    size_t counts[LOG_TYPE_COUNT];
} level0_context;

/* First explicit severity marker wins. No inheritance across lines. */
log_type level0_classify(sf_line line);
sf_level level0_create(level0_context *context, const level0_config *config);

#endif
