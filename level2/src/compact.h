#ifndef COMPACT_H
#define COMPACT_H

#include <stdbool.h>
#include <stddef.h>

// Forward declare LineGroup (defined in compact.c)
typedef struct LineGroup LineGroup;

// Compact state - buffers lines and detects patterns
typedef struct {
    char     **buffer;        // rolling buffer of recent lines
    int        buffer_size;   // max buffer entries
    int        buffer_count;  // current entries
    
    LineGroup **groups;       // detected similar groups
    int        group_count;
    
    int        min_group_size;    // minimum lines to form a group
    double     similarity_threshold; // 0.0-1.0, min similarity to group
    bool       enabled;
    
    // Stats
    int        total_lines_in;
    int        total_lines_out;
    int        groups_detected;
    int        lines_grouped;
} CompactState;

// Create compact state
CompactState *compact_create(int buffer_size, int min_group_size, double similarity_threshold);

// Free compact state
void compact_destroy(CompactState *cs);

// Process a line, returns output string (caller must not free)
const char *compact_process(CompactState *cs, const char *line);

// Force flush any pending groups (at end of input)
void compact_flush(CompactState *cs);

// Check if compact is enabled
bool compact_is_enabled(const CompactState *cs);

// Enable/disable compact mode
void compact_set_enabled(CompactState *cs, bool enabled);

// Get stats
int compact_total_in(const CompactState *cs);
int compact_total_out(const CompactState *cs);
int compact_groups_detected(const CompactState *cs);
int compact_lines_grouped(const CompactState *cs);

// Reset state
void compact_reset(CompactState *cs);

#endif // COMPACT_H
