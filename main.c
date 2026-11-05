#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "base/base.h"
#include "level1/level1.h"

static bool write_line(void *context, sf_line line)
{
    FILE *stream = context;
    return fwrite(line.data, 1, line.length, stream) == line.length;
}

int main(int argc, char **argv)
{
    level1_context context = {0};
    level1_config config = {0};
    int option;
    while ((option = getopt(argc, argv, "d")) != -1) {
        if (option == 'd')
            config.deduplicate = true;
        else {
            fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (optind != argc) {
        fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
        return EXIT_FAILURE;
    }
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
                                 (sf_line){.data = buffer, .length = (size_t)length})) {
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
    if (context.allocation_failed)
        fprintf(stderr, "smartfilter: deduplication allocation failed\n");
    if (fflush(stdout) == EOF || ferror(stdout)) {
        fprintf(stderr, "smartfilter: output write failed\n");
        status = EXIT_FAILURE;
    }
    level1_destroy(&context);
    free(buffer);
    return status;
}
