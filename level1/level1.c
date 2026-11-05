#include "level1/level1.h"

static bool filter(void *context, const void *config, sf_line line,
                   sf_output *output)
{
    level1_context *state = context;
    const level1_config *options = config;
    (void)output;
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
    return true;
}

sf_level level1_create(level1_context *context, const level1_config *config)
{
    return (sf_level){context, config, filter, NULL};
}
