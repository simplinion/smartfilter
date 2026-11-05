#include "filter.h"
#include "../shared/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

static void print_usage(const char *prog) {
    fprintf(stderr, "SmartFilter Level 1 - Regex-based line filter\n");
    fprintf(stderr, "\nUsage: %s [OPTIONS]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -m, --mode MODE       Filter mode: aggressive, permissive, custom\n");
    fprintf(stderr, "  -l, --level TYPE      Minimum level: error, warning, info, debug, all\n");
    fprintf(stderr, "  -c, --collapse N      Collapse after N repetitions (default: 3)\n");
    fprintf(stderr, "  -L, --maxlen N        Max line length (default: 200)\n");
    fprintf(stderr, "  -C, --cmds CMDS       Protocol commands (e.g. 'MODE:aggressive;ADD_BL:.*Compiling.*')\n");
    fprintf(stderr, "  -w, --wl PATTERN      Whitelist pattern (show only matching)\n");
    fprintf(stderr, "  -b, --bl PATTERN      Blacklist pattern (hide matching)\n");
    fprintf(stderr, "  -s, --no-collapse     Disable repetition collapsing\n");
    fprintf(stderr, "  -S, --summarize       Show summary instead of details\n");
    fprintf(stderr, "  -v, --verbose         Show filter stats at end\n");
    fprintf(stderr, "  -h, --help            Show this help\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  make 2>&1 | %s --mode aggressive\n", prog);
    fprintf(stderr, "  cargo build 2>&1 | %s -C 'MODE:aggressive;ADD_BL:.*Compiling.*'\n", prog);
    fprintf(stderr, "  cat log.txt | %s -w 'error|warning|failed'\n", prog);
}

int main(int argc, char *argv[]) {
    Mode mode = MODE_PERMISSIVE;
    LineType min_level = LINE_UNKNOWN;
    int collapse_threshold = 3;
    int max_line_len = 200;
    int verbose = 0;
    int no_collapse = 0;
    int summarize = 0;
    char *wl_pattern = NULL;
    char *bl_pattern = NULL;
    char *cmds_str = NULL;
    
    static struct option long_options[] = {
        {"mode",       required_argument, 0, 'm'},
        {"level",      required_argument, 0, 'l'},
        {"collapse",   required_argument, 0, 'c'},
        {"maxlen",     required_argument, 0, 'L'},
        {"cmds",       required_argument, 0, 'C'},
        {"wl",         required_argument, 0, 'w'},
        {"bl",         required_argument, 0, 'b'},
        {"no-collapse",no_argument,       0, 's'},
        {"summarize",  no_argument,       0, 'S'},
        {"verbose",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "m:l:c:L:C:w:b:sSvhh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'm':
                if (strcmp(optarg, "aggressive") == 0) mode = MODE_AGGRESSIVE;
                else if (strcmp(optarg, "permissive") == 0) mode = MODE_PERMISSIVE;
                else if (strcmp(optarg, "custom") == 0) mode = MODE_CUSTOM;
                else { fprintf(stderr, "Unknown mode: %s\n", optarg); return 1; }
                break;
                
            case 'l':
                if (strcmp(optarg, "error") == 0) min_level = LINE_ERROR;
                else if (strcmp(optarg, "warning") == 0) min_level = LINE_WARNING;
                else if (strcmp(optarg, "info") == 0) min_level = LINE_INFO;
                else if (strcmp(optarg, "debug") == 0) min_level = LINE_DEBUG;
                else if (strcmp(optarg, "all") == 0) min_level = LINE_UNKNOWN;
                else { fprintf(stderr, "Unknown level: %s\n", optarg); return 1; }
                break;
                
            case 'c':
                collapse_threshold = atoi(optarg);
                if (collapse_threshold <= 0) collapse_threshold = 3;
                break;
                
            case 'L':
                max_line_len = atoi(optarg);
                if (max_line_len <= 0) max_line_len = 200;
                break;
                
            case 'C':
                cmds_str = optarg;
                break;
                
            case 'w':
                wl_pattern = optarg;
                break;
                
            case 'b':
                bl_pattern = optarg;
                break;
                
            case 's':
                no_collapse = 1;
                break;
                
            case 'S':
                summarize = 1;
                break;
                
            case 'v':
                verbose = 1;
                break;
                
            case 'h':
            default:
                print_usage(argv[0]);
                return opt == 'h' ? 0 : 1;
        }
    }
    
    // Initialize classifier
    if (classifier_init() != 0) {
        fprintf(stderr, "Warning: Some patterns failed to compile\n");
    }
    
    // Create filter
    Filter *f = filter_create();
    if (!f) {
        fprintf(stderr, "Failed to create filter\n");
        return 1;
    }
    
    // Apply mode
    f->config.mode = mode;
    f->config.min_level = min_level;
    f->config.collapse_threshold = collapse_threshold;
    f->config.max_line_len = max_line_len;
    f->config.collapse_enabled = !no_collapse;
    f->config.summarize = summarize;
    
    // Apply custom patterns
    if (wl_pattern) {
        f->config.custom_wl = strdup(wl_pattern);
    }
    if (bl_pattern) {
        f->config.custom_bl = strdup(bl_pattern);
    }
    
    // Apply protocol commands if provided
    if (cmds_str) {
        ProtocolCmd cmds[64];
        int n = proto_parse_multi(cmds_str, cmds, 64);
        if (n > 0) {
            filter_apply_cmds(f, cmds, n);
            proto_free_cmds(cmds, n);
        }
    }
    
    // Process stdin line by line
    char line[4096];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        bool shown;
        const char *output = filter_process_ex(f, line, &shown);
        
        if (shown && output[0]) {
            printf("%s\n", output);
        }
    }
    
    // Print summary if verbose
    if (verbose) {
        filter_print_summary(f);
    }
    
    // Cleanup
    filter_destroy(f);
    classifier_free();
    
    return 0;
}
