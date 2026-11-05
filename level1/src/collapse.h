#ifndef COLLAPSE_H
#define COLLAPSE_H

#include <stdbool.h>

// Collapse state - tracks repeated lines
typedef struct {
    char     last_line[512];    // last line content
    int      count;             // repetition count
    int      max_count;         // threshold for collapsing
    bool     enabled;           // collapse enabled
    bool     pending;           // we have lines to collapse
} CollapseState;

// Initialize collapse state
CollapseState *collapse_create(void);

// Free collapse state
void collapse_destroy(CollapseState *cs);

// Process a line, returns true if line should be shown
// If collapsed, sets *shown to false and updates count
bool collapse_process(CollapseState *cs, const char *line, bool *shown);

// Force show pending collapsed summary
void collapse_flush(CollapseState *cs);

// Check if there are pending collapsed lines
bool collapse_has_pending(const CollapseState *cs);

// Get the collapsed line that would be shown
const char *collapse_get_summary(const CollapseState *cs);

// Enable/disable collapse
void collapse_set_enabled(CollapseState *cs, bool enabled);

// Set max count before collapsing
void collapse_set_threshold(CollapseState *cs, int max);

// Reset state
void collapse_reset(CollapseState *cs);

#endif // COLLAPSE_H
