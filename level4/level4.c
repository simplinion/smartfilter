#include "level4/level4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct event_line {
    sf_line line;
    char *display;
    char *original;
};

typedef enum { PLAIN, CONTEXT, PRIMARY, SOURCE, GUTTER, MARKER } line_kind;
typedef struct {
    line_kind kind;
    sf_event_kind severity;
    char file[LEVEL4_FILE_LIMIT];
    char function[LEVEL4_FUNCTION_LIMIT];
    char message[LEVEL4_MESSAGE_LIMIT];
    size_t row;
    size_t column;
    size_t caret_column;
    bool caret;
} parsed_line;

static bool space(char ch) { return ch == ' ' || ch == '\t'; }
static bool digit(char ch) { return ch >= '0' && ch <= '9'; }

static bool copy_text(char *out, size_t capacity, const char *start, size_t length)
{
    if (length >= capacity)
        return false;
    memcpy(out, start, length);
    out[length] = '\0';
    return true;
}

static bool number(const char **at, size_t *value)
{
    const char *p = *at;
    if (!digit(*p))
        return false;
    *value = 0;
    while (digit(*p)) {
        unsigned part = (unsigned)(*p++ - '0');
        if (*value > (SIZE_MAX - part) / 10)
            return false;
        *value = *value * 10 + part;
    }
    *at = p;
    return true;
}

static bool plain_text(sf_line line, char text[LEVEL4_TEXT_LIMIT + 1])
{
    const char *data = line.original ? line.original : line.data;
    size_t length = line.original ? line.original_length : line.length;
    if (length > LEVEL4_TEXT_LIMIT || (length && memchr(data, '\0', length)))
        return false;
    size_t written = 0;
    for (size_t i = 0; i < length;) {
        if (data[i] == '\x1b' && length - i >= 3 && data[i + 1] == '[') {
            size_t end = i + 2;
            while (end < length && ((data[end] >= '0' && data[end] <= '9') ||
                   data[end] == ';' || data[end] == ':'))
                ++end;
            if (end < length && data[end] == 'm') {
                i = end + 1;
                continue;
            }
        }
        text[written++] = data[i++];
    }
    while (written && (text[written - 1] == '\n' || text[written - 1] == '\r'))
        --written;
    text[written] = '\0';
    return true;
}

static parsed_line parse(const char *text)
{
    parsed_line result = {0};
    /* Gutter/source syntax wins over diagnostic-like text inside code. */
    const char *at = text;
    while (space(*at))
        ++at;
    const char *digits = at;
    size_t row;
    if (number(&digits, &row)) {
        while (space(*digits))
            ++digits;
        if (*digits == '|') {
            result.kind = SOURCE;
            result.row = row;
            return result;
        }
    }
    if (*at == '|') {
        bool underline = true;
        for (const char *p = at + 1; *p; ++p)
            if (!strchr("^~+- ", *p) && *p != '\t')
                underline = false;
        if (underline) {
            result.kind = GUTTER;
            return result;
        }
    }
    bool marker = *at != '\0';
    for (const char *p = at; *p; ++p) {
        if (*p == '^')
            result.caret = true;
        if (!strchr("^~+- ", *p) && *p != '\t')
            marker = false;
    }
    if (marker && result.caret) {
        result.kind = MARKER;
        for (const char *p = text; p < at; ++p)
            result.caret_column += *p == '\t' ? 8 - result.caret_column % 8 : 1;
    }
    if (result.kind == MARKER)
        return result;

    /* Find a complete file:line[:column]: severity: prefix. Colons elsewhere
     * (drive letters, C++ names, filenames, message payload) are not locations. */
    for (const char *colon = strchr(text, ':'); colon; colon = strchr(colon + 1, ':')) {
        const char *at = colon + 1;
        size_t row, column = 0;
        if (!number(&at, &row) || !row || *at++ != ':')
            continue;
        if (digit(*at) && (!number(&at, &column) || *at++ != ':'))
            continue;
        while (space(*at))
            ++at;
        const char *labels[] = {"warning:", "fatal error:", "error:", "note:"};
        const sf_event_kind kinds[] = {SF_EVENT_WARNING, SF_EVENT_ERROR, SF_EVENT_ERROR, SF_EVENT_NOTE};
        for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
            size_t label_length = strlen(labels[i]);
            if (strncmp(at, labels[i], label_length) != 0)
                continue;
            const char *message = at + label_length;
            while (space(*message))
                ++message;
            if (colon == text || !copy_text(result.file, sizeof(result.file), text, (size_t)(colon - text)) ||
                !copy_text(result.message, sizeof(result.message), message, strlen(message)))
                return (parsed_line){0};
            if (strpbrk(result.file, "\"'();|"))
                return (parsed_line){0};
            result.kind = PRIMARY;
            result.severity = kinds[i];
            result.row = row;
            result.column = column;
            return result;
        }
    }
    const char *markers[] = {": In function ", ": In member function ", ": At top level:"};
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i) {
        const char *at = strstr(text, markers[i]);
        if (!at)
            continue;
        if (at == text || !copy_text(result.file, sizeof(result.file), text, (size_t)(at - text)))
            return (parsed_line){0};
        if (i < 2) {
            const char *function = at + strlen(markers[i]);
            size_t length = strlen(function);
            if (length && function[length - 1] == ':')
                --length;
            if (length >= 2 && function[0] == '\'' && function[length - 1] == '\'') {
                ++function;
                length -= 2;
            }
            if (!copy_text(result.function, sizeof(result.function), function, length))
                return (parsed_line){0};
        }
        result.kind = CONTEXT;
        return result;
    }

    return result;
}

static size_t line_bytes(sf_line line)
{
    size_t original = line.original ? line.original_length : 0;
    return original > SIZE_MAX - line.length ? SIZE_MAX : original + line.length;
}

static event_line *own(sf_line line)
{
    event_line *owned = calloc(1, sizeof(*owned));
    if (!owned)
        return NULL;
    owned->display = malloc(line.length ? line.length : 1);
    if (line.original)
        owned->original = malloc(line.original_length ? line.original_length : 1);
    if (!owned->display || (line.original && !owned->original)) {
        free(owned->display);
        free(owned->original);
        free(owned);
        return NULL;
    }
    if (line.length)
        memcpy(owned->display, line.data, line.length);
    if (line.original_length && line.original)
        memcpy(owned->original, line.original, line.original_length);
    owned->line = line;
    owned->line.data = owned->display;
    owned->line.original = owned->original;
    return owned;
}

static void release(event_line *line)
{
    if (!line)
        return;
    free(line->display);
    free(line->original);
    free(line);
}

static void emit(level4_context *state, sf_output *output, sf_line line, sf_event_part part)
{
    line.event = part == SF_EVENT_PART_NONE ? NULL : &state->event;
    line.event_part = part;
    if (part != SF_EVENT_PART_NONE)
        ++state->associated_lines;
    sf_emit(output, line);
    if (line.length)
        state->at_line_start = line.data[line.length - 1] == '\n';
}

static void boundary(level4_context *state, sf_output *output, bool begin)
{
    char buffer[LEVEL4_FILE_LIMIT + LEVEL4_FUNCTION_LIMIT + 160];
    int length;
    if (begin) {
        const char *kind = state->event.kind == SF_EVENT_WARNING ? "warning" :
                           state->event.kind == SF_EVENT_ERROR ? "error" : "note";
        length = snprintf(buffer, sizeof(buffer), "[event %zu begin %s %s:%zu:%zu%s%s]\n",
                          state->event.id, kind, state->file, state->event.line, state->event.column,
                          state->function[0] ? " function=" : "", state->function);
    } else {
        length = snprintf(buffer, sizeof(buffer), "[event %zu end]\n", state->event.id);
    }
    if (!state->at_line_start)
        sf_emit(output, (sf_line){.data = "\n", .length = 1});
    sf_emit(output, (sf_line){.data = buffer, .length = (size_t)length});
    state->at_line_start = true;
}

static void close_event(level4_context *state, const level4_config *options, sf_output *output)
{
    if (state->active && options->show_events)
        boundary(state, output, false);
    state->active = state->source_seen = false;
}

static void flush_pending(level4_context *state, sf_output *output, bool associate)
{
    for (size_t i = 0; i < state->pending_count; ++i) {
        emit(state, output, state->pending[i]->line, associate ? SF_EVENT_CONTEXT : SF_EVENT_PART_NONE);
        release(state->pending[i]);
    }
    state->pending_count = state->pending_bytes = 0;
    state->pending_file[0] = state->pending_function[0] = '\0';
}

static void flush_candidate(level4_context *state, sf_output *output, bool associate)
{
    if (!state->candidate_source)
        return;
    emit(state, output, state->candidate_source->line, associate ? SF_EVENT_SOURCE : SF_EVENT_PART_NONE);
    release(state->candidate_source);
    state->candidate_source = NULL;
}

static bool save(event_line **destination, sf_line line, level4_context *state, sf_output *output)
{
    *destination = own(line);
    if (*destination)
        return true;
    state->allocation_failed = true;
    sf_output_fail(output);
    return false;
}

static bool code_candidate(const char *text)
{
    /* Clang's unguttered source must be confirmed by the next aligned caret.
     * Do not attach arbitrary indented log text. */
    return *text && strpbrk(text, ";{}()#") && !strstr(text, "[repeat]") &&
           !strstr(text, ": warning:") && !strstr(text, ": error:");
}

static bool filter(void *context, const void *config, sf_line line, sf_output *output)
{
    level4_context *state = context;
    const level4_config *options = config;
    if (!options->enabled)
        return true;
    char text[LEVEL4_TEXT_LIMIT + 1];
    bool readable = plain_text(line, text);
    if (!readable || line_bytes(line) > options->maximum_bytes) {
        close_event(state, options, output);
        flush_candidate(state, output, false);
        flush_pending(state, output, false);
        emit(state, output, line, SF_EVENT_PART_NONE);
        return false;
    }
    parsed_line parsed = parse(text);
    if (state->candidate_source) {
        if (parsed.kind == MARKER && state->event.column &&
            parsed.caret_column == state->event.column - 1) {
            flush_candidate(state, output, true);
            emit(state, output, line, SF_EVENT_MARKER);
            state->source_seen = true;
            return false;
        }
        close_event(state, options, output);
        flush_candidate(state, output, false);
    }
    if (parsed.kind == CONTEXT) {
        close_event(state, options, output);
        if (state->pending_count && strcmp(state->pending_file, parsed.file) != 0)
            flush_pending(state, output, false);
        if (state->pending_count == LEVEL4_MAX_PENDING ||
            line_bytes(line) > options->maximum_bytes - state->pending_bytes)
            flush_pending(state, output, false);
        if (!save(&state->pending[state->pending_count], line, state, output))
            return false;
        ++state->pending_count;
        state->pending_bytes += line_bytes(line);
        strcpy(state->pending_file, parsed.file);
        strcpy(state->pending_function, parsed.function);
        return false;
    }
    if (parsed.kind == PRIMARY) {
        close_event(state, options, output);
        bool related = state->pending_count && strcmp(state->pending_file, parsed.file) == 0;
        if (!related)
            flush_pending(state, output, false);
        strcpy(state->file, parsed.file);
        strcpy(state->message, parsed.message);
        strcpy(state->function, related ? state->pending_function : "");
        state->event = (sf_event){.id = ++state->next_id, .kind = parsed.severity,
            .file = state->file, .line = parsed.row, .column = parsed.column,
            .function = state->function, .message = state->message};
        state->active = true;
        if (options->show_events)
            boundary(state, output, true);
        flush_pending(state, output, true);
        emit(state, output, line, SF_EVENT_MESSAGE);
        return false;
    }
    flush_pending(state, output, false);
    if (state->active) {
        if (parsed.kind == SOURCE && parsed.row == state->event.line) {
            emit(state, output, line, SF_EVENT_SOURCE);
            state->source_seen = true;
            return false;
        }
        if (parsed.kind == GUTTER) {
            const char *payload = strchr(text, '|') + 1;
            while (space(*payload))
                ++payload;
            if (state->source_seen || !*payload) {
                emit(state, output, line, state->source_seen ? SF_EVENT_MARKER : SF_EVENT_CONTEXT);
                return false;
            }
        }
        if (parsed.kind == MARKER && state->source_seen) {
            emit(state, output, line, SF_EVENT_MARKER);
            return false;
        }
        if (parsed.kind == PLAIN && code_candidate(text) && !state->source_seen) {
            save(&state->candidate_source, line, state, output);
            return false;
        }
        close_event(state, options, output);
    }
    emit(state, output, line, SF_EVENT_PART_NONE);
    return false;
}

static void finish(void *context, const void *config, sf_output *output)
{
    level4_context *state = context;
    const level4_config *options = config;
    close_event(state, options, output);
    flush_candidate(state, output, false);
    flush_pending(state, output, false);
}

sf_level level4_create(level4_context *context, const level4_config *config)
{
    context->at_line_start = true;
    return (sf_level){context, config, filter, finish};
}

void level4_destroy(level4_context *context)
{
    for (size_t i = 0; i < context->pending_count; ++i)
        release(context->pending[i]);
    release(context->candidate_source);
    *context = (level4_context){0};
}
