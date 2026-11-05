#include <stdio.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include <math.h>
#include <errno.h>

#include "base/base.h"
#include "level0/level0.h"
#include "level1/level1.h"
#include "level2/level2.h"
#include "level3/level3.h"
#include "level4/level4.h"
#include "level5/level5.h"

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
            "  --query TEXT  annotate cosine similarity; enables event grouping\n"
            "  --embedding-socket PATH  local embedding service socket\n"
            "  --min-score N  filter whole semantic units below this cosine score\n"
            "  --semantic-timeout MS  service deadline (default 5000)\n"
            "  --semantic-batch N  units per request (1..32, default 1)\n"
            "  --show LIST  progress,error,warning,info,result,stats,version,formatting,listing,diagnostic,summary,unknown or all\n"
            "               unknown, diagnostic context and block summaries always remain visible\n", program);
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
    level5_context semantic = {0};
    level5_config semantic_config = {.timeout_ms = 5000, .batch_size = 1,
                                     .maximum_unit_bytes = 65536};
    char socket_path[4096];
    bool semantic_option = false;
    level4_config event_config = {.maximum_bytes = 256 * 1024};
    level3_config category_config = {.show = LEVEL3_ALL};
    level2_config block_config = {.minimum_lines = 3, .maximum_bytes = 1024 * 1024};
    level1_config config = {
        .compaction = {.minimum_prefix = 4},
    };
    enum { OPT_CLASSIFY = 1000, OPT_SHOW, OPT_COLOR, OPT_GROUP, OPT_EVENTS, OPT_QUERY, OPT_SOCKET, OPT_SCORE, OPT_TIMEOUT, OPT_BATCH };
    static const struct option long_options[] = {
        {"classify", no_argument, NULL, OPT_CLASSIFY},
        {"color", no_argument, NULL, OPT_COLOR},
        {"show", required_argument, NULL, OPT_SHOW},
        {"group-diagnostics", no_argument, NULL, OPT_GROUP},
        {"events", no_argument, NULL, OPT_EVENTS},
        {"query", required_argument, NULL, OPT_QUERY},
        {"embedding-socket", required_argument, NULL, OPT_SOCKET},
        {"min-score", required_argument, NULL, OPT_SCORE},
        {"semantic-timeout", required_argument, NULL, OPT_TIMEOUT},
        {"semantic-batch", required_argument, NULL, OPT_BATCH},
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
        } else if (option == OPT_QUERY) {
            semantic_config.query = optarg;
        } else if (option == OPT_SOCKET) {
            semantic_config.socket_path = optarg;
            semantic_option = true;
        } else if (option == OPT_SCORE) {
            char *end;
            errno = 0;
            semantic_config.minimum_score = strtof(optarg, &end);
            if (errno || !*optarg || *end || !isfinite(semantic_config.minimum_score) ||
                semantic_config.minimum_score < -1 || semantic_config.minimum_score > 1) {
                fprintf(stderr, "smartfilter: --min-score requires a finite value between -1 and 1\n");
                return EXIT_FAILURE;
            }
            semantic_config.has_threshold = semantic_option = true;
        } else if (option == OPT_TIMEOUT || option == OPT_BATCH) {
            char *end;
            errno = 0;
            long value = strtol(optarg, &end, 10);
            long maximum = option == OPT_TIMEOUT ? 60000 : EMBED_MAX_BATCH;
            if (errno || !*optarg || *end || value < 1 || value > maximum) {
                fprintf(stderr, "smartfilter: invalid semantic timeout/batch value\n");
                return EXIT_FAILURE;
            }
            if (option == OPT_TIMEOUT)
                semantic_config.timeout_ms = (int)value;
            else
                semantic_config.batch_size = (size_t)value;
            semantic_option = true;
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
    if ((!semantic_config.query && semantic_option) ||
        (semantic_config.query && (!*semantic_config.query || strlen(semantic_config.query) > EMBED_MAX_TEXT ||
         strspn(semantic_config.query, " \t\r\n") == strlen(semantic_config.query)))) {
        fprintf(stderr, "smartfilter: semantic options require a nonempty --query (max 64 KiB)\n");
        return EXIT_FAILURE;
    }
    if (semantic_config.query) {
        event_config.enabled = true;
        if (!semantic_config.socket_path) {
            const char *cache = getenv("XDG_CACHE_HOME");
            const char *home = getenv("HOME");
            if (cache && cache[0] == '/')
                snprintf(socket_path, sizeof(socket_path), "%s/smartfilter/embeddings.sock", cache);
            else
                snprintf(socket_path, sizeof(socket_path), "%s/.cache/smartfilter/embeddings.sock", home ? home : "");
            semantic_config.socket_path = socket_path;
        }
    }
    if (!level1_init(&context, &config)) {
        fprintf(stderr, "smartfilter: cannot initialize path dictionary\n");
        level1_destroy(&context);
        return EXIT_FAILURE;
    }
    level5_init(&semantic, &semantic_config);
    const sf_level levels[] = {
        level0_create(&prefilter, &prefilter_config),
        level1_create(&context, &config),
        level2_create(&blocks, &block_config),
        level3_create(&categories, &category_config),
        level4_create(&events, &event_config),
        level5_create(&semantic, &semantic_config),
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
    if (semantic.allocation_failed)
        fprintf(stderr, "smartfilter: level5 allocation failed\n");
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
    level5_destroy(&semantic);
    level4_destroy(&events);
    level3_destroy(&categories);
    level2_destroy(&blocks);
    level1_destroy(&context);
    free(buffer);
    return status;
}
