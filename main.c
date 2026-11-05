#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "base/base.h"
#include "level1/level1.h"

static bool write_line(void *context, sf_line line)
{
    FILE *stream = context;
    return fwrite(line.data, 1, line.length, stream) == line.length;
}

int main(void)
{
    level1_context context = {0};
    const level1_config config = {.drop_empty_lines = false};
    const sf_level levels[] = {level1_create(&context, &config)};
    sf_pipeline pipeline = {
        .levels = levels,
        .count = sizeof(levels) / sizeof(levels[0]),
        .sink = write_line,
        .sink_context = stdout,
    };

    char *buffer = NULL;
    size_t capacity = 0;
    ssize_t length;
    int status = EXIT_SUCCESS;

    while ((length = getline(&buffer, &capacity, stdin)) != -1) {
        if (!sf_pipeline_process(&pipeline,
                                 (sf_line){buffer, (size_t)length})) {
            status = EXIT_FAILURE;
            break;
        }
    }
    if (status == EXIT_SUCCESS && (!feof(stdin) || ferror(stdin))) {
        fprintf(stderr, "smartfilter: input read failed\n");
        status = EXIT_FAILURE;
    }
    if (status == EXIT_SUCCESS && !sf_pipeline_finish(&pipeline))
        status = EXIT_FAILURE;
    if (fflush(stdout) == EOF || pipeline.failed) {
        fprintf(stderr, "smartfilter: output write failed\n");
        status = EXIT_FAILURE;
    }
    free(buffer);
    return status;
}
