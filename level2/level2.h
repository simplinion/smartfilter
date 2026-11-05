#ifndef SMARTFILTER_LEVEL2_H
#define SMARTFILTER_LEVEL2_H

#include "base/base.h"

#define LEVEL2_MAX_BLOCK_LINES 128
#define LEVEL2_MAX_PATTERNS 16
#define LEVEL2_MAX_BUFFER_LINES (2 * LEVEL2_MAX_BLOCK_LINES)

typedef struct {
    bool collapse_blocks;
    size_t minimum_lines;
    size_t maximum_bytes;
} level2_config;

typedef struct block_line block_line;
typedef struct {
    block_line *lines[LEVEL2_MAX_BLOCK_LINES];
    size_t count;
    size_t bytes;
} block_pattern;

typedef struct {
    block_line *pending[LEVEL2_MAX_BUFFER_LINES];
    size_t count;
    size_t bytes;
    block_line *pattern[LEVEL2_MAX_BLOCK_LINES];
    size_t pattern_count;
    block_pattern known[LEVEL2_MAX_PATTERNS];
    size_t known_count;
    size_t cache_bytes;
    bool framed_stream;
    bool frame_active;
    bool allocation_failed;
} level2_context;

sf_level level2_create(level2_context *context, const level2_config *config);
void level2_destroy(level2_context *context);

#endif
