#include <stdio.h>
#include <string.h>

#include "base/base.h"

typedef struct {
    char bytes[128];
    size_t used;
    size_t visits;
    bool fail;
} test_context;

static bool sink(void *context, sf_line line)
{
    test_context *state = context;
    if (line.hash != sf_line_hash(line))
        return false;
    if (state->fail || line.length > sizeof(state->bytes) - state->used)
        return false;
    memcpy(state->bytes + state->used, line.data, line.length);
    state->used += line.length;
    return true;
}

static bool replace(void *context, const void *config, sf_line line,
                    sf_output *output)
{
    (void)context;
    (void)config;
    if (line.length && line.data[0] == 'x') {
        sf_emit(output, (sf_line){.data = "a\n", .length = 2});
        sf_emit(output, (sf_line){.data = "b\n", .length = 2});
        return false;
    }
    return true;
}

static bool drop(void *context, const void *config, sf_line line,
                 sf_output *output)
{
    (void)config;
    (void)output;
    ++((test_context *)context)->visits;
    return !(line.length && line.data[0] == 'b');
}

static void finish(void *context, const void *config, sf_output *output)
{
    (void)context;
    (void)config;
    sf_emit(output, (sf_line){.data = "end\n", .length = 4});
}

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "failed at line %d: %s\n", __LINE__, #condition); \
    return 1; } } while (0)

int main(void)
{
    test_context state = {0};
    const sf_level levels[] = {
        {NULL, NULL, replace, finish},
        {&state, NULL, drop, finish},
    };
    sf_pipeline pipeline = {levels, 2, sink, &state, false};
    CHECK(sf_pipeline_process(&pipeline, (sf_line){.data = "x\n", .length = 2}));
    CHECK(state.visits == 2);
    CHECK(sf_pipeline_process(&pipeline, (sf_line){.data = "keep\n", .length = 5}));
    CHECK(sf_pipeline_finish(&pipeline));
    CHECK(state.visits == 4);
    CHECK(state.used == 15);
    CHECK(memcmp(state.bytes, "a\nkeep\nend\nend\n", 15) == 0);

    state.fail = true;
    CHECK(!sf_pipeline_process(&pipeline, (sf_line){.data = "keep\n", .length = 5}));
    size_t visits = state.visits;
    CHECK(!sf_pipeline_process(&pipeline, (sf_line){.data = "keep\n", .length = 5}));
    CHECK(state.visits == visits);

    state = (test_context){0};
    pipeline = (sf_pipeline){NULL, 0, sink, &state, false};
    CHECK(sf_pipeline_process(&pipeline, (sf_line){.data = "raw", .length = 3}));
    CHECK(sf_pipeline_finish(&pipeline));
    CHECK(state.used == 3 && memcmp(state.bytes, "raw", 3) == 0);
    return 0;
}
