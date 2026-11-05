#include <stdio.h>
#include <locale.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>

#include "base/base.h"
#include "level0/level0.h"
#include "level1/level1.h"
#include "level2/level2.h"
#include "level3/level3.h"
#include "level4/level4.h"

static bool write_line(void *context, sf_line line)
{
    FILE *stream = context;
    return fwrite(line.data, 1, line.length, stream) == line.length;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s [-dcpPubview] [--classify] [--color] [--show LIST] [--group-diagnostics] [--events]\n"
            "  -v/-i/-e/-w  hide explicit verbose/info/error/warning labels (level 0)\n"
            "  -d deduplicate; -c compact; -p paths; -P print dictionary; -u URLs; -b blocks\n"
            "  --classify   annotate level3 categories without changing line contents\n"
            "  --color      color complete lines by level3 category (ANSI/VT100)\n"
            "  --group-diagnostics  attach compiler event metadata; preserve output\n"
            "  --events     group diagnostics and show event boundaries\n"
            "  --show LIST  progress,error,warning,info,result,stats,version,formatting,listing,diagnostic,unknown or all\n"
            "               unknown and diagnostic context always remain visible\n", program);
}

int main(int argc, char **argv)
{
    setlocale(LC_CTYPE, "");
    level0_context prefilter = {0};
    level0_config prefilter_config = {0};
    level1_context context = {0};
    level2_context blocks = {0};
    level3_context categories = {0};
    level4_context events = {0};
    level4_config event_config = {.maximum_bytes = 256 * 1024};
    level3_config category_config = {.show = LEVEL3_ALL};
    level2_config block_config = {.minimum_lines = 3, .maximum_bytes = 1024 * 1024};
    level1_config config = {
        .compaction = {.minimum_prefix = 4},
    };
    enum { OPT_CLASSIFY = 1000, OPT_SHOW, OPT_COLOR, OPT_GROUP, OPT_EVENTS };
    static const struct option long_options[] = {
        {"classify", no_argument, NULL, OPT_CLASSIFY},
        {"color", no_argument, NULL, OPT_COLOR},
        {"show", required_argument, NULL, OPT_SHOW},
        {"group-diagnostics", no_argument, NULL, OPT_GROUP},
        {"events", no_argument, NULL, OPT_EVENTS},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };
    int option;
    while ((option = getopt_long(argc, argv, "dcpPubviewh", long_options, NULL)) != -1) {
        if (option == OPT_CLASSIFY) {
            category_config.enabled = category_config.annotate = true;
        } else if (option == OPT_COLOR) {
            category_config.enabled = category_config.color = true;
        } else if (option == OPT_GROUP || option == OPT_EVENTS) {
            event_config.enabled = true;
            if (option == OPT_EVENTS)
                event_config.show_events = true;
        } else if (option == OPT_SHOW) {
            if (!level3_parse_categories(optarg, &category_config.show)) {
                fprintf(stderr, "smartfilter: invalid category list: %s\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            category_config.enabled = true;
        } else if (option == 'h') {
            usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (option == 'v')
            prefilter_config.hide_verbose = true;
        else if (option == 'i')
            prefilter_config.hide_info = true;
        else if (option == 'e')
            prefilter_config.hide_error = true;
        else if (option == 'w')
            prefilter_config.hide_warning = true;
        else if (option == 'd')
            config.deduplicate = true;
        else if (option == 'P') {
            config.abbreviate_paths = true;
            config.print_paths = true;
        }
        else if (option == 'b')
            block_config.collapse_blocks = true;
        else if (option == 'u')
            config.abbreviate_urls = true;
        else if (option == 'p')
            config.abbreviate_paths = true;
        else if (option == 'c')
            config.compaction.compact = true;
        else {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (optind != argc) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (!level1_init(&context, &config)) {
        fprintf(stderr, "smartfilter: cannot initialize path dictionary\n");
        level1_destroy(&context);
        return EXIT_FAILURE;
    }
    const sf_level levels[] = {
        level0_create(&prefilter, &prefilter_config),
        level1_create(&context, &config),
        level2_create(&blocks, &block_config),
        level3_create(&categories, &category_config),
        level4_create(&events, &event_config),
    };
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
    if (events.allocation_failed)
        fprintf(stderr, "smartfilter: level4 allocation failed\n");
    if (categories.allocation_failed)
        fprintf(stderr, "smartfilter: level3 allocation failed\n");
    if (blocks.allocation_failed)
        fprintf(stderr, "smartfilter: level2 allocation failed\n");
    if (context.compaction.allocation_failed)
        fprintf(stderr, "smartfilter: compaction allocation failed\n");
    if (context.allocation_failed)
        fprintf(stderr, "smartfilter: level1 allocation failed\n");
    if (fflush(stdout) == EOF || ferror(stdout)) {
        fprintf(stderr, "smartfilter: output write failed\n");
        status = EXIT_FAILURE;
    }
    level4_destroy(&events);
    level3_destroy(&categories);
    level2_destroy(&blocks);
    level1_destroy(&context);
    free(buffer);
    return status;
}
