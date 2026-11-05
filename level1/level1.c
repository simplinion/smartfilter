#include "level1/level1.h"

#include <stdlib.h>

static size_t slot(uint64_t hash, size_t capacity)
{
    /* Mix the high bits into the index for power-of-two tables. */
    hash ^= hash >> 33;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33;
    return (size_t)hash & (capacity - 1);
}

static bool grow(level1_context *state)
{
    size_t capacity = state->capacity ? state->capacity * 2 : 1024;
    if (capacity < state->capacity || capacity > SIZE_MAX / sizeof(uint64_t))
        return false;
    uint64_t *hashes = malloc(capacity * sizeof(*hashes));
    unsigned char *occupied = calloc(capacity, sizeof(*occupied));
    if (!hashes || !occupied) {
        free(hashes);
        free(occupied);
        return false;
    }
    for (size_t i = 0; i < state->capacity; ++i) {
        if (!state->occupied[i])
            continue;
        size_t index = slot(state->hashes[i], capacity);
        while (occupied[index])
            index = (index + 1) & (capacity - 1);
        hashes[index] = state->hashes[i];
        occupied[index] = 1;
    }
    free(state->hashes);
    free(state->occupied);
    state->hashes = hashes;
    state->occupied = occupied;
    state->capacity = capacity;
    return true;
}

bool level1_init(level1_context *context, const level1_config *config)
{
    return !(config->abbreviate_paths || config->abbreviate_urls) || paths_init(&context->paths);
}

void level1_destroy(level1_context *context)
{
    compact_destroy(&context->compaction);
    paths_destroy(&context->paths);
    free(context->hashes);
    free(context->occupied);
    *context = (level1_context){0};
}

static bool filter(void *context, const void *config, sf_line line,
                   sf_output *output)
{
    level1_context *state = context;
    const level1_config *options = config;
    ++state->lines_seen;

    size_t length = line.length;
    if (length && line.data[length - 1] == '\n') {
        --length;
        if (length && line.data[length - 1] == '\r')
            --length;
    }
    if (options->drop_empty_lines && length == 0) {
        ++state->lines_dropped;
        return false;
    }
    if (options->deduplicate) {
        if (state->capacity) {
            size_t index = slot(line.hash, state->capacity);
            while (state->occupied[index]) {
                if (state->hashes[index] == line.hash) {
                    ++state->lines_dropped;
                    return false;
                }
                index = (index + 1) & (state->capacity - 1);
            }
        }
        /* Keep load <= 75% for short probing sequences. */
        if ((!state->capacity || state->count >= state->capacity - state->capacity / 4)
            && !grow(state)) {
            state->allocation_failed = true;
            sf_output_fail(output);
            return false;
        }
        size_t index = slot(line.hash, state->capacity);
        while (state->occupied[index])
            index = (index + 1) & (state->capacity - 1);
        state->hashes[index] = line.hash;
        state->occupied[index] = 1;
        ++state->count;
    }
    sf_line transformed = line;
    if ((options->abbreviate_paths || options->abbreviate_urls) &&
        !paths_transform(&state->paths, line, &transformed,
                         options->abbreviate_paths, options->abbreviate_urls)) {
        state->allocation_failed = true;
        sf_output_fail(output);
        return false;
    }
    if (!compact_filter(&state->compaction, &options->compaction, transformed, output))
        return false;
    if (transformed.data != line.data) {
        sf_emit(output, transformed);
        return false;
    }
    return true;
}

static void finish(void *context, const void *config, sf_output *output)
{
    level1_context *state = context;
    const level1_config *options = config;
    if (options->print_paths)
        paths_finish(&state->paths, output);
}

sf_level level1_create(level1_context *context, const level1_config *config)
{
    return (sf_level){context, config, filter, finish};
}
