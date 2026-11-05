#include "level2/level2.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 128
#define MAX_BINDINGS 8

typedef enum { WORD, NUMBER, PATH, PUNCTUATION } token_kind;
typedef struct {
    const char *data;
    size_t length;
    token_kind kind;
} token;

struct block_line {
    char *raw;
    size_t length;
    char *plain;
    token tokens[MAX_TOKENS];
    size_t token_count;
    bool compacted;
    bool safe;
    bool header;
    bool completion;
    char progress[32];
    char subject[128];
};

typedef struct {
    token before[MAX_BINDINGS];
    token after[MAX_BINDINGS];
    size_t count;
} bindings;

static bool equal(token a, token b)
{
    return a.kind == b.kind && a.length == b.length &&
           memcmp(a.data, b.data, a.length) == 0;
}

static bool word_is(token word, const char *text)
{
    if (word.length != strlen(text))
        return false;
    for (size_t i = 0; i < word.length; ++i)
        if (tolower((unsigned char)word.data[i]) != (unsigned char)text[i])
            return false;
    return true;
}

static void release(block_line *line)
{
    free(line->raw);
    free(line->plain);
    free(line);
}

static void forget_pattern(level2_context *state)
{
    for (size_t i = 0; i < state->pattern_count; ++i)
        release(state->pattern[i]);
    state->pattern_count = 0;
}

static void drain(level2_context *state, sf_output *output)
{
    for (size_t i = 0; i < state->count; ++i) {
        block_line *line = state->pending[i];
        sf_emit(output, (sf_line){.data = line->raw, .length = line->length});
        release(line);
    }
    state->count = state->bytes = 0;
}

static block_line *parse(sf_line input)
{
    block_line *line = calloc(1, sizeof(*line));
    if (!line)
        return NULL;
    line->raw = malloc(input.length ? input.length : 1);
    line->plain = malloc(input.length + 1);
    if (!line->raw || !line->plain) {
        release(line);
        return NULL;
    }
    memcpy(line->raw, input.data, input.length);
    line->length = input.length;
    size_t length = 0;
    for (size_t i = 0; i < input.length;) {
        if (input.data[i] == '\x1b' && i + 1 < input.length && input.data[i + 1] == '[') {
            size_t end = i + 2;
            while (end < input.length &&
                   !((unsigned char)input.data[end] >= 0x40 &&
                     (unsigned char)input.data[end] <= 0x7e))
                ++end;
            if (end < input.length && input.data[end] == 'm') {
                i = end + 1;
                continue;
            }
        }
        line->plain[length++] = input.data[i++];
    }
    line->plain[length] = '\0';
    line->safe = true;
    size_t pos = 0;
    while (pos < length && isspace((unsigned char)line->plain[pos]))
        ++pos;
    if (length - pos >= 3 && memcmp(line->plain + pos, "\xe2\x86\xb3", 3) == 0) {
        line->compacted = true;
        pos += 3;
    }
    while (pos < length && isspace((unsigned char)line->plain[pos]))
        ++pos;
    /* Ignore benign severity/progress prefixes when comparing structure. */
    if (pos < length && line->plain[pos] == '[') {
        size_t end = pos + 1;
        while (end < length && line->plain[end] != ']')
            ++end;
        bool progress = end > pos + 1;
        for (size_t i = pos + 1; i < end; ++i)
            if (!isdigit((unsigned char)line->plain[i]) &&
                strchr("/%. ", line->plain[i]) == NULL)
                progress = false;
        token tag = {line->plain + pos + 1, end - pos - 1, WORD};
        if (progress && !line->compacted && end < length &&
            memchr(tag.data, '/', tag.length) != NULL) {
            line->header = true;
            size_t size = end - pos + 1;
            if (size >= sizeof(line->progress))
                size = sizeof(line->progress) - 1;
            memcpy(line->progress, line->plain + pos, size);
        }
        if (end < length && (progress || word_is(tag, "info") ||
                             word_is(tag, "debug") || word_is(tag, "trace")))
            pos = end + 1;
    }
    while (pos < length) {
        unsigned char ch = (unsigned char)line->plain[pos];
        if (isspace(ch)) {
            ++pos;
            continue;
        }
        if (line->token_count == MAX_TOKENS) {
            line->safe = false;
            break;
        }
        size_t end = pos;
        while (end < length && !isspace((unsigned char)line->plain[end]))
            ++end;
        bool path = ch == '$';
        for (size_t i = pos; i < end; ++i)
            if (line->plain[i] == '/')
                path = true;
        token item = {.data = line->plain + pos};
        if (path) {
            item.kind = PATH;
        } else if (isalnum(ch) || ch == '_' || ch >= 0x80) {
            end = pos + 1;
            while (end < length && (isalnum((unsigned char)line->plain[end]) ||
                   line->plain[end] == '_' || (unsigned char)line->plain[end] >= 0x80))
                ++end;
            item.kind = NUMBER;
            for (size_t i = pos; i < end; ++i)
                if (!isdigit((unsigned char)line->plain[i]))
                    item.kind = WORD;
            if (item.kind == NUMBER) {
                while (end + 1 < length && line->plain[end] == '.' &&
                       isdigit((unsigned char)line->plain[end + 1])) {
                    ++end;
                    while (end < length && isdigit((unsigned char)line->plain[end]))
                        ++end;
                }
            }
            if (end - pos > 2 && line->plain[pos] == '0' && line->plain[pos + 1] == 'x') {
                bool hex = true;
                for (size_t i = pos + 2; i < end; ++i)
                    if (!isxdigit((unsigned char)line->plain[i]))
                        hex = false;
                if (hex)
                    item.kind = NUMBER;
            }
        } else {
            end = pos + 1;
            item.kind = PUNCTUATION;
            if (ch < ' ' || ch == 0x7f)
                line->safe = false;
        }
        item.length = end - pos;
        line->tokens[line->token_count++] = item;
        if (item.kind == WORD && (word_is(item, "warn") || word_is(item, "warning") ||
            word_is(item, "error") || word_is(item, "fatal") || word_is(item, "failed") ||
            word_is(item, "fail") || word_is(item, "exception")))
            line->safe = false;
        pos = end;
    }
    if (!line->token_count)
        line->safe = false;
    if (line->header) {
        const char *colon = strrchr(line->plain, ':');
        if (colon) {
            ++colon;
            while (*colon && isspace((unsigned char)*colon))
                ++colon;
            size_t size = strcspn(colon, "\r\n");
            if (size >= sizeof(line->subject))
                size = sizeof(line->subject) - 1;
            memcpy(line->subject, colon, size);
            while (size && isspace((unsigned char)line->subject[size - 1]))
                line->subject[--size] = '\0';
        }
        if (!line->subject[0])
            line->header = false;
    }
    if (line->token_count) {
        token first = line->tokens[0];
        line->completion = word_is(first, "installed") || word_is(first, "completed") ||
                           word_is(first, "finished") || word_is(first, "done") ||
                           word_is(first, "built");
    }
    return line;
}

static bool bind_word(bindings *map, token before, token after, bool learn)
{
    for (size_t i = 0; i < map->count; ++i) {
        if (equal(map->before[i], before))
            return equal(map->after[i], after);
        if (equal(map->after[i], after))
            return false;
    }
    if (!learn || map->count == MAX_BINDINGS)
        return false;
    map->before[map->count] = before;
    map->after[map->count++] = after;
    return true;
}

static bool similar_line(const block_line *a, const block_line *b,
                         bindings *map, bool first)
{
    if (!a->safe || !b->safe)
        return false;
    size_t ai = 0, bi = 0;
    if (a->token_count != b->token_count) {
        if (!(a->compacted || b->compacted))
            return false;
        /* A compacted line may omit a few leading words. Align its suffix. */
        if (a->token_count > b->token_count)
            ai = a->token_count - b->token_count;
        else
            bi = b->token_count - a->token_count;
        if (ai > 4 || bi > 4)
            return false;
    }
    size_t fixed = 0, words = 0, changed = 0;
    for (; ai < a->token_count && bi < b->token_count; ++ai, ++bi) {
        token x = a->tokens[ai], y = b->tokens[bi];
        if (x.kind != y.kind)
            return false;
        if (x.kind == NUMBER || x.kind == PATH)
            continue;
        if (x.kind == WORD)
            ++words;
        if (x.kind == WORD) {
            for (size_t j = 0; j < map->count; ++j)
                if (equal(map->before[j], x) && !equal(map->after[j], y))
                    return false;
        }
        if (equal(x, y)) {
            if (x.kind == WORD)
                ++fixed;
        } else if (x.kind != WORD || fixed == 0 || !bind_word(map, x, y, first)) {
            return false;
        } else {
            ++changed;
        }
    }
    return fixed > 0 && changed <= 4 && (words == 0 || fixed * 2 >= words);
}

static bool similar(block_line *const *a, block_line *const *b, size_t count,
                    bindings *map)
{
    *map = (bindings){0};
    for (size_t i = 0; i < count; ++i)
        if (!similar_line(a[i], b[i], map, i == 0))
            return false;
    return true;
}

static bool subject_complete(const block_line *header, const block_line *last)
{
    if (!last->completion)
        return false;
    const char *subject = header->subject;
    size_t length = strlen(subject);
    /* The subject must begin the completion payload, not be a later token
     * inside another dependency's name (e.g. lib-mv must not complete mv). */
    const char *at = last->tokens[0].data + last->tokens[0].length;
    while (*at && isspace((unsigned char)*at))
        ++at;
    if (*at == ':') {
        ++at;
    } else {
        const char *roles[] = {"module", "task", "target"};
        for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i) {
            size_t role_length = strlen(roles[i]);
            if (strncmp(at, roles[i], role_length) == 0 &&
                isspace((unsigned char)at[role_length])) {
                at += role_length;
                break;
            }
        }
    }
    while (*at && isspace((unsigned char)*at))
        ++at;
    return strlen(at) >= length && memcmp(at, subject, length) == 0 &&
           (at[length] == '\0' || at[length] == '@' || isspace((unsigned char)at[length]));
}

static void summary(block_line *const *pattern, block_line *const *current,
                    size_t count, sf_output *output)
{
    bindings map;
    (void)similar(pattern, current, count, &map);
    char text[512];
    if (current[0]->header) {
        int size = snprintf(text, sizeof(text), "%s [repeat] %s (%zu lines; like %s)\n",
                            current[0]->progress, current[0]->subject, count,
                            pattern[0]->subject);
        sf_emit(output, (sf_line){.data = text, .length = (size_t)size});
        return;
    }
    size_t used = (size_t)snprintf(text, sizeof(text), "[repeat] ");
    for (size_t i = 0; i < map.count && used < 400; ++i) {
        size_t length = map.after[i].length > 80 ? 80 : map.after[i].length;
        int written = snprintf(text + used, sizeof(text) - used, "%s%.*s",
                               i ? ", " : "", (int)length, map.after[i].data);
        if (written > 0)
            used += (size_t)written;
    }
    if (!map.count) {
        const block_line *a = pattern[0], *b = current[0];
        token label = {0};
        if (a->token_count == b->token_count) {
            for (size_t i = 0; i < a->token_count; ++i)
                if ((b->tokens[i].kind == NUMBER || b->tokens[i].kind == PATH) &&
                    !equal(a->tokens[i], b->tokens[i])) {
                    label = b->tokens[i];
                    break;
                }
        }
        if (label.length)
            used += (size_t)snprintf(text + used, sizeof(text) - used, "%.*s",
                                   (int)(label.length > 80 ? 80 : label.length), label.data);
        else
            used += (size_t)snprintf(text + used, sizeof(text) - used, "block");
    }
    used += (size_t)snprintf(text + used, sizeof(text) - used,
                             " (%zu similar lines)\n", count);
    sf_emit(output, (sf_line){.data = text, .length = used});
}

static void adopt(level2_context *state, size_t start)
{
    forget_pattern(state);
    state->pattern_count = state->count - start;
    for (size_t i = start; i < state->count; ++i)
        state->pattern[i - start] = state->pending[i];
    state->count = state->bytes = 0;
}

static void release_pattern(block_pattern *pattern)
{
    for (size_t i = 0; i < pattern->count; ++i)
        release(pattern->lines[i]);
    *pattern = (block_pattern){0};
}

static void evict(level2_context *state)
{
    state->cache_bytes -= state->known[0].bytes;
    release_pattern(&state->known[0]);
    --state->known_count;
    memmove(state->known, state->known + 1, state->known_count * sizeof(state->known[0]));
    state->known[state->known_count] = (block_pattern){0};
}

static void finish_frame(level2_context *state, const level2_config *options,
                         sf_output *output)
{
    size_t minimum = options->minimum_lines < 3 ? 3 : options->minimum_lines;
    bool complete = state->count >= minimum &&
                    subject_complete(state->pending[0], state->pending[state->count - 1]);
    if (complete) {
        bindings map;
        for (size_t i = 0; i < state->known_count; ++i) {
            block_pattern *pattern = &state->known[i];
            if (pattern->count == state->count &&
                similar(pattern->lines, state->pending, state->count, &map)) {
                summary(pattern->lines, state->pending, state->count, output);
                for (size_t j = 0; j < state->count; ++j)
                    release(state->pending[j]);
                state->count = state->bytes = 0;
                state->frame_active = false;
                return;
            }
        }
        while (state->known_count && (state->known_count == LEVEL2_MAX_PATTERNS ||
               state->bytes > options->maximum_bytes - state->cache_bytes))
            evict(state);
        block_pattern *pattern = &state->known[state->known_count++];
        pattern->count = state->count;
        pattern->bytes = state->bytes;
        state->cache_bytes += state->bytes;
        for (size_t i = 0; i < state->count; ++i) {
            block_line *line = state->pending[i];
            sf_emit(output, (sf_line){.data = line->raw, .length = line->length});
            pattern->lines[i] = line;
        }
        state->count = state->bytes = 0;
    } else {
        drain(state, output);
    }
    state->frame_active = false;
}

static bool filter(void *context, const void *config, sf_line input, sf_output *output)
{
    level2_context *state = context;
    const level2_config *options = config;
    if (!options->collapse_blocks)
        return true;
    if (input.length > options->maximum_bytes) {
        drain(state, output);
        forget_pattern(state);
        state->frame_active = false;
        return true;
    }
    block_line *line = parse(input);
    if (!line) {
        state->allocation_failed = true;
        sf_output_fail(output);
        return false;
    }
    /* Progress headers delimit operations. Never learn an arbitrary slice of
     * a nested operation once such headers have established the stream. */
    if (line->header && line->safe) {
        if (state->frame_active)
            finish_frame(state, options, output);
        else
            drain(state, output);
        forget_pattern(state);
        state->framed_stream = state->frame_active = true;
    }
    if (state->framed_stream) {
        if (!state->frame_active || !line->safe ||
            state->count == LEVEL2_MAX_BLOCK_LINES ||
            input.length > options->maximum_bytes - state->bytes) {
            drain(state, output);
            state->frame_active = false;
            release(line);
            return true;
        }
        state->pending[state->count++] = line;
        state->bytes += input.length;
        if (state->count > 1 && subject_complete(state->pending[0], line))
            finish_frame(state, options, output);
        return false;
    }
    if (!line->safe) {
        drain(state, output);
        forget_pattern(state);
        release(line);
        return true;
    }
    if (state->count == LEVEL2_MAX_BUFFER_LINES ||
        input.length > options->maximum_bytes - state->bytes) {
        drain(state, output);
        forget_pattern(state);
    }
    state->pending[state->count++] = line;
    state->bytes += input.length;
    bindings map;
    if (state->pattern_count) {
        if (similar(state->pattern, state->pending, state->count, &map)) {
            if (state->count == state->pattern_count) {
                summary(state->pattern, state->pending, state->count, output);
                adopt(state, 0);
            }
            return false;
        }
        forget_pattern(state);
    }
    size_t minimum = options->minimum_lines < 3 ? 3 : options->minimum_lines;
    for (size_t size = minimum; size <= state->count / 2; ++size) {
        size_t start = state->count - 2 * size;
        if (!similar(state->pending + start, state->pending + start + size, size, &map))
            continue;
        /* Repeated table rows or arbitrary middle slices are not operations.
         * Require distinct beginning/end actions and a complete final event. */
        block_line *begin = state->pending[start];
        block_line *end = state->pending[start + size - 1];
        if (!end->completion || equal(begin->tokens[0], end->tokens[0]))
            continue;
        size_t second = state->count - size;
        for (size_t i = 0; i < second; ++i) {
            block_line *original = state->pending[i];
            sf_emit(output, (sf_line){.data = original->raw, .length = original->length});
        }
        summary(state->pending + start, state->pending + second, size, output);
        for (size_t i = 0; i < second; ++i)
            release(state->pending[i]);
        adopt(state, second);
        break;
    }
    return false;
}

static void finish(void *context, const void *config, sf_output *output)
{
    level2_context *state = context;
    const level2_config *options = config;
    if (state->frame_active)
        finish_frame(state, options, output);
    drain(state, output);
    forget_pattern(state);
}

sf_level level2_create(level2_context *context, const level2_config *config)
{
    return (sf_level){context, config, filter, finish};
}

void level2_destroy(level2_context *context)
{
    for (size_t i = 0; i < context->count; ++i)
        release(context->pending[i]);
    forget_pattern(context);
    for (size_t i = 0; i < context->known_count; ++i)
        release_pattern(&context->known[i]);
    *context = (level2_context){0};
}
