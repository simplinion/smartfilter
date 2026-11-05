#include "level4/level4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t id;
    sf_event_part part;
    sf_event_kind kind;
    size_t row, column;
    char file[128], function[128], message[256], original[512];
} record;

typedef struct {
    record records[128];
    size_t count;
    char output[8192];
    size_t length;
    bool fail;
} observation;

static bool sink(void *context, sf_line line)
{
    observation *state = context;
    if (state->fail || state->count == 128 || line.length > sizeof(state->output) - state->length)
        return false;
    if (line.hash != sf_line_hash(line))
        return false;
    memcpy(state->output + state->length, line.data, line.length);
    state->length += line.length;
    record *entry = &state->records[state->count++];
    entry->part = line.event_part;
    if (line.original) {
        size_t length = line.original_length < sizeof(entry->original) - 1 ?
                        line.original_length : sizeof(entry->original) - 1;
        memcpy(entry->original, line.original, length);
    }
    if (line.event) {
        entry->id = line.event->id;
        entry->kind = line.event->kind;
        entry->row = line.event->line;
        entry->column = line.event->column;
        snprintf(entry->file, sizeof(entry->file), "%s", line.event->file);
        snprintf(entry->function, sizeof(entry->function), "%s", line.event->function);
        snprintf(entry->message, sizeof(entry->message), "%s", line.event->message);
    }
    return true;
}

/* A later stage may rewrite the displayed text without losing event metadata. */
static bool forward(void *context, const void *config, sf_line line, sf_output *output)
{
    (void)context;
    (void)config;
    sf_emit(output, sf_line_rewrite(line, line.data, line.length));
    return false;
}

static bool input(sf_pipeline *pipeline, char *buffer, const char *original, const char *display)
{
    strcpy(buffer, original);
    return sf_pipeline_process(pipeline, (sf_line){.data = display, .length = strlen(display),
        .original = buffer, .original_length = strlen(buffer)});
}

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    level4_context context = {0};
    const level4_config config = {.enabled = true, .maximum_bytes = 256 * 1024};
    sf_level levels[] = {level4_create(&context, &config), {NULL, NULL, forward, NULL}};
    observation state = {0};
    sf_pipeline pipeline = {levels, 2, sink, &state, false};
    char buffer[2048];
    CHECK(input(&pipeline, buffer, "/src/port.c: In function 'scheduler':\n", "short function\n"));
    CHECK(state.count == 0);
    CHECK(input(&pipeline, buffer, "/src/port.c:315:13: warning: unused variable 'x'\n", "short warning\n"));
    CHECK(input(&pipeline, buffer, "  315 | int x;\n", "short source\n"));
    CHECK(input(&pipeline, buffer, "      |     ^\n", "short marker\n"));
    CHECK(state.count == 4);
    for (size_t i = 0; i < 4; ++i) {
        record *r = &state.records[i];
        CHECK(r->id == 1 && r->kind == SF_EVENT_WARNING);
        CHECK(r->row == 315 && r->column == 13);
        CHECK(strcmp(r->file, "/src/port.c") == 0);
        CHECK(strcmp(r->function, "scheduler") == 0);
        CHECK(strcmp(r->message, "unused variable 'x'") == 0);
    }
    CHECK(state.records[0].part == SF_EVENT_CONTEXT);
    CHECK(strstr(state.records[0].original, "In function 'scheduler'"));
    CHECK(state.records[1].part == SF_EVENT_MESSAGE);
    CHECK(state.records[2].part == SF_EVENT_SOURCE);
    CHECK(state.records[3].part == SF_EVENT_MARKER);
    CHECK(input(&pipeline, buffer, "unrelated\n", "unrelated\n"));
    CHECK(state.records[4].id == 0);
    CHECK(input(&pipeline, buffer, "/src/b.c:8:5: error: unknown symbol\n", "error\n"));
    CHECK(input(&pipeline, buffer, "    foo();\n", "clang source\n"));
    CHECK(state.count == 6);
    CHECK(input(&pipeline, buffer, "    ^~~\n", "clang marker\n"));
    CHECK(state.records[5].id == 2 && state.records[5].kind == SF_EVENT_ERROR);
    CHECK(state.records[5].function[0] == '\0');
    CHECK(state.records[6].id == 2 && state.records[6].part == SF_EVENT_SOURCE);
    CHECK(strcmp(state.records[6].original, "    foo();\n") == 0);
    CHECK(state.records[7].id == 2 && state.records[7].part == SF_EVENT_MARKER);
    /* A different source line is not attached to that error. */
    CHECK(input(&pipeline, buffer, "   19 | other();\n", "other source\n"));
    CHECK(state.records[8].id == 0);
    /* A context header from a different file remains unassociated. */
    CHECK(input(&pipeline, buffer, "/src/a.c: In function 'a':\n", "context a\n"));
    CHECK(input(&pipeline, buffer, "/src/b.c:10: note: declared here\n", "note b\n"));
    CHECK(state.records[9].id == 0);
    CHECK(state.records[10].id == 3 && state.records[10].kind == SF_EVENT_NOTE);
    CHECK(input(&pipeline, buffer, "/src/orphan.c: At top level:\n", "orphan context"));
    CHECK(sf_pipeline_finish(&pipeline));
    CHECK(state.records[11].id == 0);
    level4_destroy(&context);
    state.fail = true;
    context = (level4_context){0};
    levels[0] = level4_create(&context, &config);
    pipeline.failed = false;
    CHECK(!input(&pipeline, buffer, "/a.c:1: error: failed\n", "error\n"));
    level4_destroy(&context);
    return 0;
}
