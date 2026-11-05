#ifndef SMARTFILTER_LEVEL1_H
#define SMARTFILTER_LEVEL1_H

#include "base/base.h"
#include "level1/compact.h"
#include "level1/paths.h"

typedef struct {
    bool drop_empty_lines;
    bool deduplicate;
    bool abbreviate_paths;
    bool abbreviate_urls;
    bool print_paths;
    compact_config compaction;
} level1_config;

typedef struct {
    compact_context compaction;
    paths_context paths;
    size_t lines_seen;
    size_t lines_dropped;
    uint64_t *hashes;
    unsigned char *occupied;
    size_t capacity;
    size_t count;
    bool allocation_failed;
} level1_context;

/* Context must initially be zeroed. Free it after processing, even on error. */
bool level1_init(level1_context *context, const level1_config *config);
void level1_destroy(level1_context *context);
sf_level level1_create(level1_context *context, const level1_config *config);

#endif
