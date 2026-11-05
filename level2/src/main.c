#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>

// Include level1 components
#include "../level1/src/classifier.h"
#include "../level1/src/context.h"
#include "../level1/src/collapse.h"
#include "../level1/src/dedup.h"
#include "../level1/src/filter.h"

// Include level2 compact
#include "compact.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "SmartFilter Level 2 - Pattern detection + compaction\n");
    fprintf(stderr, "\nUsage: %s [OPTIONS]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -m, --mode MODE       Filter mode: aggressive, permissive, custom\n");
    fprintf(stderr, "  -c, --collapse N      Collapse consecutive repetitions (default: 3)\n");
    fprintf(stderr, "  -d, --dedup           Deduplicate (show each unique line once)\n");
    fprintf(stderr, "  -C, --compact N       Compact similar lines (group if N+ similar, default: off)\n");
    fprintf(stderr, "  -s, --similarity F    Similarity threshold 0.0-1.0 (default: 0.6)\n");
    fprintf(stderr, "  -b, --buffer N        Buffer size for pattern detection (default: 100)\n");
    fprintf(stderr, "  -l, --level TYPE      Minimum level: error, warning, info, debug, all\n");
    fprintf(stderr, "  -v, --verbose         Show filter stats at end\n");
    fprintf(stderr, "  -h, --help            Show this help\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  make 2>&1 | %s -C 3     # compact 3+ similar lines\n", prog);
    fprintf(stderr, "  make 2>&1 | %s -c 2 -C 4  # collapse 2x, compact 4+ similar\n", prog);
    fprintf(stderr, "  make 2>&1 | %s -d -C 3   # dedup + compact\n", prog);
}

int main(int argc, char *argv[]) {
    Mode mode = MODE_PERMISSIVE;
    LineType min_level = LINE_UNKNOWN;
    int collapse_threshold = 3;
    int verbose = 0;
    int no_collapse = 0;
    int dedup = 0;
    int compact_min = 0;  // 0 = disabled
    double similarity = 0.6;
    int buffer_size = 100;
    
    static struct option long_options[] = {
        {"mode",       required_argument, 0, 'm'},
        {"level",      required_argument, 0, 'l'},
        {"collapse",   required_argument, 0, 'c'},
        {"dedup",      no_argument,       0, 'd'},
        {"compact",    required_argument, 0, 'C'},
        {"similarity", required_argument, 0, 's'},
        {"buffer",     required_argument, 0, 'b'},
        {"verbose",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "m:l:c:C:s:b:dvhh", long_options, NULL)) != -1) {
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
                
            case 'd':
                dedup = 1;
                break;
                
            case 'C':
                compact_min = atoi(optarg);
                if (compact_min <= 0) compact_min = 0;
                break;
                
            case 's':
                similarity = atof(optarg);
                if (similarity <= 0) similarity = 0.6;
                if (similarity > 1.0) similarity = 1.0;
                break;
                
            case 'b':
                buffer_size = atoi(optarg);
                if (buffer_size <= 0) buffer_size = 100;
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
    
    // If no arguments, show usage
    if (argc == 1) {
        print_usage(argv[0]);
        return 0;
    }
    
    // Initialize classifier
    if (classifier_init() != 0) {
        fprintf(stderr, "Warning: Some patterns failed to compile\n");
    }
    
    // Create filter (level1)
    Filter *f = filter_create();
    if (!f) {
        fprintf(stderr, "Failed to create filter\n");
        return 1;
    }
    
    // Create compact (level2)
    CompactState *cs = NULL;
    if (compact_min > 0) {
        cs = compact_create(buffer_size, compact_min, similarity);
        if (!cs) {
            fprintf(stderr, "Failed to create compact state\n");
            filter_destroy(f);
            return 1;
        }
    }
    
    // Configure filter
    f->config.mode = mode;
    f->config.min_level = min_level;
    f->config.collapse_threshold = collapse_threshold;
    f->config.collapse_enabled = !no_collapse;
    f->config.dedup_enabled = dedup;
    
    // Process stdin
    char line[4096];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        // Process through level1 filter first
        bool shown;
        const char *filtered = filter_process_ex(f, line, &shown);
        
        if (!shown) continue;
        
        if (filtered[0] == '\0') continue;
        
        // Then process through level2 compact (if enabled)
        if (cs && compact_is_enabled(cs)) {
            const char *compacted = compact_process(cs, filtered);
            if (compacted && compacted[0]) {
                printf("%s\n", compacted);
            }
        } else {
            printf("%s\n", filtered);
        }
    }
    
    // Flush compact
    if (cs) {
        compact_flush(cs);
    }
    
    // Print stats
    if (verbose) {
        fprintf(stderr, "\n\033[1mSmartFilter Level 2 Summary\033[0m\n");
        fprintf(stderr, "  Lines in:        %d\n", filter_total_in(f));
        fprintf(stderr, "  Lines out:       %d\n", filter_total_out(f));
        fprintf(stderr, "  Skipped:         %d (%.1f%%)\n", 
                filter_skipped(f), 
                filter_total_in(f) > 0 ? 100.0 * filter_skipped(f) / filter_total_in(f) : 0);
        
        if (cs) {
            fprintf(stderr, "  Groups detected: %d\n", compact_groups_detected(cs));
            fprintf(stderr, "  Lines grouped:   %d\n", compact_lines_grouped(cs));
        }
    }
    
    // Cleanup
    filter_destroy(f);
    if (cs) compact_destroy(cs);
    classifier_free();
    
    return 0;
}
