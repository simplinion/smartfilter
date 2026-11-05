#include "collapse.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

// Normalize line for comparison (strip trailing spaces, etc.)
static void normalize_line(const char *src, char *dst, size_t max) {
    if (!src || !dst) return;
    
    size_t len = strlen(src);
    if (len >= max) len = max - 1;
    
    // Copy up to max-1 chars
    memcpy(dst, src, len);
    dst[len] = '\0';
    
    // Trim trailing whitespace
    while (len > 0 && isspace((unsigned char)dst[len - 1])) {
        len--;
        dst[len] = '\0';
    }
}

CollapseState *collapse_create(void) {
    CollapseState *cs = calloc(1, sizeof(CollapseState));
    if (!cs) return NULL;
    
    cs->last_line[0] = '\0';
    cs->count = 0;
    cs->max_count = 3;
    cs->enabled = true;
    cs->pending = false;
    
    return cs;
}

void collapse_destroy(CollapseState *cs) {
    free(cs);
}

bool collapse_process(CollapseState *cs, const char *line, bool *shown) {
    if (!cs || !line) return false;
    if (!shown) shown = &(bool){0};
    
    if (!cs->enabled) {
        *shown = true;
        return true;
    }
    
    char normalized[512];
    normalize_line(line, normalized, sizeof(normalized));
    
    // Empty lines - always show
    if (normalized[0] == '\0') {
        collapse_flush(cs);
        *shown = true;
        strncpy(cs->last_line, normalized, sizeof(cs->last_line) - 1);
        cs->count = 0;
        cs->pending = false;
        return true;
    }
    
    // Check if this matches last line
    if (strcmp(normalized, cs->last_line) == 0) {
        cs->count++;
        *shown = false;
        cs->pending = true;
        
        // If we've hit threshold, keep hiding
        if (cs->count >= cs->max_count) {
            return true; // keep hiding
        }
        
        // Show first few repetitions
        *shown = true;
        return true;
    }
    
    // New line - flush previous collapse and start new
    collapse_flush(cs);
    
    strncpy(cs->last_line, normalized, sizeof(cs->last_line) - 1);
    cs->count = 0;
    cs->pending = false;
    *shown = true;
    
    return true;
}

void collapse_flush(CollapseState *cs) {
    // This is a no-op for now - summary is generated on-demand
    // In future, could print directly
}

bool collapse_has_pending(const CollapseState *cs) {
    return cs && cs->pending && cs->count >= cs->max_count;
}

const char *collapse_get_summary(const CollapseState *cs) {
    static char summary[512];
    
    if (!cs || !cs->pending || cs->count < cs->max_count) {
        return NULL;
    }
    
    snprintf(summary, sizeof(summary), "... %dx: %s", cs->count + 1, cs->last_line);
    return summary;
}

void collapse_set_enabled(CollapseState *cs, bool enabled) {
    if (cs) cs->enabled = enabled;
}

void collapse_set_threshold(CollapseState *cs, int max) {
    if (cs && max > 0) cs->max_count = max;
}

void collapse_reset(CollapseState *cs) {
    if (!cs) return;
    
    cs->last_line[0] = '\0';
    cs->count = 0;
    cs->pending = false;
}
