#include "level5/level5.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct semantic_line {
    sf_line view;
    char *display;
    char *original;
    struct semantic_line *next;
} semantic_line;

struct semantic_unit {
    semantic_line *first, *last;
    sf_event event;
    char *file, *function, *message;
    char *text;
    size_t text_length;
    size_t bytes;
    bool binary;
};

static void release(semantic_unit *unit)
{
    if (!unit)
        return;
    semantic_line *line = unit->first;
    while (line) {
        semantic_line *next = line->next;
        free(line->display);
        free(line->original);
        free(line);
        line = next;
    }
    free(unit->file);
    free(unit->function);
    free(unit->message);
    free(unit->text);
    free(unit);
}

static semantic_unit *new_unit(sf_line line)
{
    semantic_unit *unit = calloc(1, sizeof(*unit));
    if (!unit)
        return NULL;
    if (line.event) {
        unit->file = strdup(line.event->file ? line.event->file : "");
        unit->function = strdup(line.event->function ? line.event->function : "");
        unit->message = strdup(line.event->message ? line.event->message : "");
        if (!unit->file || !unit->function || !unit->message) {
            release(unit);
            return NULL;
        }
        unit->event = *line.event;
        unit->event.file = unit->file;
        unit->event.function = unit->function;
        unit->event.message = unit->message;
    }
    return unit;
}

static size_t cost(sf_line line)
{
    size_t original = line.original ? line.original_length : 0;
    return original > SIZE_MAX - line.length ? SIZE_MAX : original + line.length;
}

static bool append(semantic_unit *unit, sf_line source)
{
    semantic_line *line = calloc(1, sizeof(*line));
    if (!line)
        return false;
    line->display = malloc(source.length ? source.length : 1);
    if (source.original)
        line->original = malloc(source.original_length ? source.original_length : 1);
    if (!line->display || (source.original && !line->original)) {
        free(line->display);
        free(line->original);
        free(line);
        return false;
    }
    if (source.length)
        memcpy(line->display, source.data, source.length);
    if (source.original && source.original_length)
        memcpy(line->original, source.original, source.original_length);
    line->view = source;
    line->view.data = line->display;
    line->view.original = line->original;
    line->view.event = source.event ? &unit->event : NULL;
    if (unit->last)
        unit->last->next = line;
    else
        unit->first = line;
    unit->last = line;
    unit->bytes += cost(source);
    /* Debug boundaries have an event but no original text: do not embed them. */
    const char *text = source.original ? source.original : source.event ? NULL : source.data;
    size_t length = source.original ? source.original_length : source.event ? 0 : source.length;
    if (length) {
        if (memchr(text, '\0', length))
            unit->binary = true;
        char *joined = realloc(unit->text, unit->text_length + length + 1);
        if (!joined)
            return false;
        unit->text = joined;
        memcpy(unit->text + unit->text_length, text, length);
        unit->text_length += length;
        unit->text[unit->text_length] = '\0';
    }
    return true;
}

static void passthrough(semantic_unit *unit, sf_output *output)
{
    for (semantic_line *line = unit->first; line; line = line->next)
        sf_emit(output, line->view);
}

static bool whitespace_only(semantic_unit *unit)
{
    for (size_t i = 0; i < unit->text_length; ++i) {
        unsigned char ch = (unsigned char)unit->text[i];
        if (!(ch == ' ' || (ch >= '\t' && ch <= '\r')))
            return false;
    }
    return true;
}

static void unavailable(level5_context *state)
{
    if (!state->disabled)
        fprintf(stderr, "smartfilter: level5 disabled: %s; keeping unscored input\n", state->client.error);
    state->disabled = true;
    embed_close(&state->client);
}

static bool annotation(level5_context *state, semantic_unit *unit, float score, sf_output *output)
{
    char prefix[32];
    int length = snprintf(prefix, sizeof(prefix), "[sim %+.3f] ", (double)score);
    for (semantic_line *line = unit->first; line; line = line->next) {
        char *data = malloc((size_t)length + line->view.length);
        if (!data) {
            state->allocation_failed = true;
            sf_output_fail(output);
            return false;
        }
        memcpy(data, prefix, (size_t)length);
        memcpy(data + length, line->view.data, line->view.length);
        sf_line annotated = sf_line_rewrite(line->view, data, (size_t)length + line->view.length);
        annotated.has_similarity = true;
        annotated.similarity = score;
        bool ok = sf_emit(output, annotated);
        free(data);
        if (!ok)
            return false;
    }
    return true;
}

static void flush(level5_context *state, const level5_config *options, sf_output *output)
{
    if (!state->count)
        return;
    const char *texts[EMBED_MAX_BATCH];
    size_t lengths[EMBED_MAX_BATCH];
    for (size_t i = 0; i < state->count; ++i) {
        texts[i] = state->pending[i]->text;
        lengths[i] = state->pending[i]->text_length;
    }
    float *vectors = NULL;
    bool scored = !state->disabled;
    if (scored) {
        vectors = malloc(state->count * state->client.dimension * sizeof(float));
        if (!vectors) {
            state->allocation_failed = true;
            sf_output_fail(output);
            scored = false;
        } else if (!embed_vectors(&state->client, 1, texts, lengths, state->count, vectors)) {
            unavailable(state);
            scored = false;
        }
    }
    for (size_t i = 0; i < state->count; ++i) {
        semantic_unit *unit = state->pending[i];
        if (!scored) {
            passthrough(unit, output);
        } else {
            double dot = 0;
            for (size_t d = 0; d < state->client.dimension; ++d)
                dot += (double)state->query[d] * vectors[i * state->client.dimension + d];
            float score = (float)fmax(-1, fmin(1, dot));
            if (!options->has_threshold || score >= options->minimum_score)
                annotation(state, unit, score, output);
        }
        release(unit);
    }
    state->count = 0;
    free(vectors);
}

static void complete(level5_context *state, const level5_config *options, sf_output *output)
{
    semantic_unit *unit = state->current;
    if (!unit)
        return;
    state->current = NULL;
    if (state->disabled || unit->binary || !unit->text_length || whitespace_only(unit)) {
        flush(state, options, output);
        passthrough(unit, output);
        release(unit);
        return;
    }
    state->pending[state->count++] = unit;
    if (state->count >= options->batch_size)
        flush(state, options, output);
}

void level5_init(level5_context *state, const level5_config *options)
{
    state->client.fd = -1;
    if (!options->query)
        return;
    if (!embed_connect(&state->client, options->socket_path, options->timeout_ms)) {
        unavailable(state);
        return;
    }
    const char *texts[] = {options->query};
    size_t lengths[] = {strlen(options->query)};
    if (!embed_vectors(&state->client, 0, texts, lengths, 1, state->query))
        unavailable(state);
}

static bool filter(void *context, const void *config, sf_line line, sf_output *output)
{
    level5_context *state = context;
    const level5_config *options = config;
    if (!options->query)
        return true;
    size_t event = line.event ? line.event->id : 0;
    if (state->current && (!event || state->current->event.id != event))
        complete(state, options, output);
    if (state->disabled) {
        flush(state, options, output);
        return true;
    }
    if (state->bypass_event && state->bypass_event != event)
        state->bypass_event = 0;
    if (state->bypass_event || (!line.original && !line.event)) {
        flush(state, options, output);
        return true;
    }
    size_t bytes = cost(line);
    if (bytes > options->maximum_unit_bytes ||
        (state->current && bytes > options->maximum_unit_bytes - state->current->bytes)) {
        flush(state, options, output);
        if (state->current) {
            passthrough(state->current, output);
            release(state->current);
            state->current = NULL;
        }
        state->bypass_event = event;
        if (!state->warned_oversize) {
            fprintf(stderr, "smartfilter: level5 unit exceeds buffer limit; keeping it unscored\n");
            state->warned_oversize = true;
        }
        return true;
    }
    if (!state->current)
        state->current = new_unit(line);
    if (!state->current || !append(state->current, line)) {
        state->allocation_failed = true;
        sf_output_fail(output);
        return false;
    }
    if (!event)
        complete(state, options, output);
    return false;
}

static void finish(void *context, const void *config, sf_output *output)
{
    level5_context *state = context;
    const level5_config *options = config;
    complete(state, options, output);
    flush(state, options, output);
}

sf_level level5_create(level5_context *state, const level5_config *options)
{
    return (sf_level){state, options, filter, finish};
}

void level5_destroy(level5_context *state)
{
    release(state->current);
    for (size_t i = 0; i < state->count; ++i)
        release(state->pending[i]);
    embed_close(&state->client);
    *state = (level5_context){0};
    state->client.fd = -1;
}
