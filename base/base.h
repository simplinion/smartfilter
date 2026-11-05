#ifndef SMARTFILTER_BASE_H
#define SMARTFILTER_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { SF_EVENT_NONE, SF_EVENT_WARNING, SF_EVENT_ERROR, SF_EVENT_NOTE } sf_event_kind;
typedef enum {
    SF_EVENT_PART_NONE, SF_EVENT_CONTEXT, SF_EVENT_MESSAGE, SF_EVENT_SOURCE, SF_EVENT_MARKER
} sf_event_part;

/* Borrowed for the duration of downstream callbacks; consumers that retain
 * event metadata must copy it, including all strings. Zero means no location. */
typedef struct {
    size_t id;
    sf_event_kind kind;
    const char *file;
    size_t line;
    size_t column;
    const char *function;
    const char *message;
} sf_event;

/* A borrowed byte sequence, including the newline when present. Never assumed
 * to be NUL-terminated. Valid only for the duration of a callback. */
typedef struct {
    const char *data;
    size_t length;
    const char *original; /* Borrowed input before rewrites; NULL for synthetic output. */
    size_t original_length;
    const sf_event *event;
    sf_event_part event_part;
    uint64_t hash; /* Set by the pipeline; hash of all length bytes. */
} sf_line;

typedef struct sf_output sf_output;

/* true forwards the original line; false drops it. Emitted lines go directly
 * to the next level, synchronously, before the original is forwarded.
 * To replace a line, emit its replacement(s) and return false. */
typedef bool (*sf_filter_fn)(void *context, const void *config,
                             sf_line line, sf_output *output);

/* Called at EOF in pipeline order; may emit buffered lines downstream. */
typedef void (*sf_finish_fn)(void *context, const void *config,
                             sf_output *output);

typedef struct {
    void *context;
    const void *config;
    sf_filter_fn filter;
    sf_finish_fn finish; /* Optional. */
} sf_level;

typedef bool (*sf_sink_fn)(void *context, sf_line line);

/* Caller owns the levels, their contexts/configs, and the sink context. */
typedef struct {
    const sf_level *levels;
    size_t count;
    sf_sink_fn sink;
    void *sink_context;
    bool failed;
} sf_pipeline;

/* Output handles must not be retained after the callback returns. */
/* Construct raw lines with designated .data and .length initializers.
 * Pipeline entry and emit compute the hash once, before dispatch. */
/* Preserve provenance when replacing the displayed contents of one line. */
sf_line sf_line_rewrite(sf_line source, const char *data, size_t length);
uint64_t sf_line_hash(sf_line line);
void sf_output_fail(sf_output *output);
bool sf_emit(sf_output *output, sf_line line);
bool sf_pipeline_process(sf_pipeline *pipeline, sf_line line);
bool sf_pipeline_finish(sf_pipeline *pipeline);

#endif
