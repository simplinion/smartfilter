#ifndef FILTER_H
#define FILTER_H

#include "../shared/types.h"
#include "../shared/protocol.h"
#include "classifier.h"
#include "context.h"
#include "collapse.h"
#include <stdbool.h>

// Filter configuration
typedef struct {
    Mode       mode;
    LineType   min_level;       // minimum level to show
    int        max_line_len;    // truncate longer lines
    int        collapse_threshold;
    bool       collapse_enabled;
    bool       summarize;
    int        stream_pass;     // pass next N lines unfiltered
    
    // Custom patterns (from L2/L3)
    char      *custom_wl;       // additional whitelist pattern
    char      *custom_bl;       // additional blacklist pattern
} FilterConfig;

// Filter state
typedef struct {
    FilterConfig   config;
    Context       *ctx;
    CollapseState *collapse;
    
    // Runtime stats
    int            total_in;
    int            total_out;
    int            skipped;
    int            collapsed;
    
    // State
    int            stream_pass_remaining;
} Filter;

// Create filter with default config
Filter *filter_create(void);

// Create filter with config from protocol commands
Filter *filter_create_from_cmds(const ProtocolCmd *cmds, int n_cmds);

// Free filter
void filter_destroy(Filter *f);

// Process a single line, returns true if should be shown
bool filter_process(Filter *f, const char *line, bool *out_shown);

// Process line and get modified output
// Returns the line to show (may be truncated or summary)
// Return value is static buffer, do not free
const char *filter_process_ex(Filter *f, const char *line, bool *out_shown);

// Apply protocol command to filter
int filter_apply_cmd(Filter *f, const ProtocolCmd *cmd);

// Apply multiple commands
int filter_apply_cmds(Filter *f, const ProtocolCmd *cmds, int n);

// Reset filter state
void filter_reset(Filter *f);

// Get stats
int filter_total_in(const Filter *f);
int filter_total_out(const Filter *f);
int filter_skipped(const Filter *f);
int filter_collapsed(const Filter *f);

// Get current config
const FilterConfig *filter_config(const Filter *f);

// Set config directly
void filter_set_config(Filter *f, const FilterConfig *cfg);

// Print summary to stdout
void filter_print_summary(const Filter *f);

#endif // FILTER_H
