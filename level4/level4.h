#ifndef SMARTFILTER_LEVEL4_H
#define SMARTFILTER_LEVEL4_H

#include "base/base.h"

#define LEVEL4_MAX_PENDING 32
#define LEVEL4_FILE_LIMIT 4096
#define LEVEL4_FUNCTION_LIMIT 1024
#define LEVEL4_MESSAGE_LIMIT 8192
#define LEVEL4_TEXT_LIMIT 16384

typedef struct {
    bool enabled;
    bool show_events;
    size_t maximum_bytes;
} level4_config;

typedef struct event_line event_line;
typedef struct {
    event_line *pending[LEVEL4_MAX_PENDING];
    size_t pending_count;
    size_t pending_bytes;
    char pending_file[LEVEL4_FILE_LIMIT];
    char pending_function[LEVEL4_FUNCTION_LIMIT];
    event_line *candidate_source;
    sf_event event;
    char file[LEVEL4_FILE_LIMIT];
    char function[LEVEL4_FUNCTION_LIMIT];
    char message[LEVEL4_MESSAGE_LIMIT];
    size_t next_id;
    size_t associated_lines;
    bool active;
    bool source_seen;
    bool at_line_start;
    bool allocation_failed;
} level4_context;

sf_level level4_create(level4_context *context, const level4_config *config);
void level4_destroy(level4_context *context);

#endif
