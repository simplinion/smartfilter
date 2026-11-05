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

bool sf_emit(sf_output *output, sf_line line)
{
    return dispatch(output->pipeline, output->next, line);
}

bool sf_pipeline_process(sf_pipeline *pipeline, sf_line line)
{
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
