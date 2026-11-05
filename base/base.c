#include "base/base.h"

struct sf_output {
    sf_pipeline *pipeline;
    size_t next;
};

static bool dispatch(sf_pipeline *pipeline, size_t index, sf_line line)
{
    if (pipeline->failed)
        return false;

    if (index == pipeline->count) {
        if (!pipeline->sink(pipeline->sink_context, line))
            pipeline->failed = true;
        return !pipeline->failed;
    }

    const sf_level *level = &pipeline->levels[index];
    sf_output output = {pipeline, index + 1};
    if (level->filter(level->context, level->config, line, &output))
        return dispatch(pipeline, index + 1, line);

    return !pipeline->failed;
}

sf_line sf_line_rewrite(sf_line source, const char *data, size_t length)
{
    if (!source.original) {
        source.original = source.data;
        source.original_length = source.length;
    }
    source.data = data;
    source.length = length;
    source.hash = 0;
    return source;
}

uint64_t sf_line_hash(sf_line line)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < line.length; ++i) {
        hash ^= (unsigned char)line.data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

void sf_output_fail(sf_output *output)
{
    output->pipeline->failed = true;
}

bool sf_emit(sf_output *output, sf_line line)
{
    line.hash = sf_line_hash(line);
    return dispatch(output->pipeline, output->next, line);
}

bool sf_pipeline_process(sf_pipeline *pipeline, sf_line line)
{
    if (!line.original) {
        line.original = line.data;
        line.original_length = line.length;
    }
    line.hash = sf_line_hash(line);
    return dispatch(pipeline, 0, line);
}

bool sf_pipeline_finish(sf_pipeline *pipeline)
{
    for (size_t i = 0; i < pipeline->count && !pipeline->failed; ++i) {
        const sf_level *level = &pipeline->levels[i];
        sf_output output = {pipeline, i + 1};
        if (level->finish)
            level->finish(level->context, level->config, &output);
    }
    return !pipeline->failed;
}
