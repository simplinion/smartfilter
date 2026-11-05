#ifndef SMARTFILTER_LEVEL5_H
#define SMARTFILTER_LEVEL5_H
#include "base/base.h"
#include "level5/client.h"

typedef struct {
    const char *query;
    const char *socket_path;
    int timeout_ms;
    size_t batch_size;
    size_t maximum_unit_bytes;
    bool has_threshold;
    float minimum_score;
} level5_config;

typedef struct semantic_unit semantic_unit;
typedef struct {
    embed_client client;
    float query[EMBED_MAX_DIMENSION];
    semantic_unit *current;
    semantic_unit *pending[EMBED_MAX_BATCH];
    size_t count;
    size_t bypass_event;
    bool disabled;
    bool allocation_failed;
    bool warned_oversize;
} level5_context;

/* Service failures disable search and preserve subsequent input. */
void level5_init(level5_context *context, const level5_config *config);
sf_level level5_create(level5_context *context, const level5_config *config);
void level5_destroy(level5_context *context);
#endif
