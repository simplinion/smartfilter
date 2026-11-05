#ifndef COMPACT_H
#define COMPACT_H

#include <stdbool.h>
#include <stddef.h>

// Forward declare LineGroup (defined in compact.c)
typedef struct LineGroup LineGroup;

// Compact state - tracks last shown line + count of similar pending
typedef struct {
    double   similarity_threshold; // 0.0-1.0
    int      min_group_size;
    bool     enabled;
    
    char    *last_shown;        // last line that was shown
    int      similar_pending;   // count of similar lines since last shown
    int      buffer_count;      // legacy field
    
    LineGroup **groups;
    int        group_count;
    
    int        total_lines_in;
    int        total_lines_out;
    int        groups_detected;
    int        lines_grouped;
} CompactState;

CompactState *compact_create(int buffer_size, int min_group_size, double similarity_threshold);
void compact_destroy(CompactState *cs);
const char *compact_process(CompactState *cs, const char *line);
void compact_flush(CompactState *cs);
bool compact_is_enabled(const CompactState *cs);
void compact_set_enabled(CompactState *cs, bool enabled);
int compact_total_in(const CompactState *cs);
int compact_total_out(const CompactState *cs);
int compact_groups_detected(const CompactState *cs);
int compact_lines_grouped(const CompactState *cs);
void compact_reset(CompactState *cs);

#endif // COMPACT_H
