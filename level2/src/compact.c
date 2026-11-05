#include "compact.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// LineGroup struct
typedef struct LineGroup {
    char    **lines;
    int       count;
    int       capacity;
    int       example_idx;
} LineGroup;

// Levenshtein distance
static int levenshtein(const char *s1, const char *s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    
    int *prev = malloc((len2 + 1) * sizeof(int));
    int *curr = malloc((len2 + 1) * sizeof(int));
    
    for (int j = 0; j <= len2; j++) prev[j] = j;
    
    for (int i = 1; i <= len1; i++) {
        curr[0] = i;
        for (int j = 1; j <= len2; j++) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            curr[j] = (curr[j-1] + 1) < (prev[j] + 1) ? (curr[j-1] + 1) : (prev[j] + 1);
            int v = prev[j-1] + cost;
            if (v < curr[j]) curr[j] = v;
        }
        int *tmp = prev;
        prev = curr;
        curr = tmp;
    }
    
    int dist = prev[len2];
    free(prev);
    free(curr);
    return dist;
}

static double similarity(const char *a, const char *b) {
    if (!a || !b) return 0.0;
    int len_a = strlen(a), len_b = strlen(b);
    if (len_a == 0 && len_b == 0) return 1.0;
    if (len_a == 0 || len_b == 0) return 0.0;
    int max_len = (len_a > len_b) ? len_a : len_b;
    return 1.0 - ((double)levenshtein(a, b) / (double)max_len);
}

static char compact_output[8192];

LineGroup *line_group_create(int capacity) {
    LineGroup *g = malloc(sizeof(LineGroup));
    if (!g) return NULL;
    g->lines = calloc(capacity, sizeof(char*));
    g->capacity = capacity;
    g->count = 0;
    g->example_idx = 0;
    return g;
}

void line_group_destroy(LineGroup *g) {
    if (!g) return;
    for (int i = 0; i < g->count; i++) free(g->lines[i]);
    free(g->lines);
    free(g);
}

CompactState *compact_create(int buffer_size, int min_group_size, double similarity_threshold) {
    (void)buffer_size;
    (void)min_group_size;
    
    CompactState *cs = calloc(1, sizeof(CompactState));
    if (!cs) return NULL;
    
    if (similarity_threshold <= 0) similarity_threshold = 0.6;
    cs->similarity_threshold = similarity_threshold;
    cs->min_group_size = 2;
    cs->enabled = true;
    cs->buffer_count = 0;
    cs->last_shown = NULL;
    cs->similar_pending = 0;
    
    return cs;
}

void compact_destroy(CompactState *cs) {
    if (!cs) return;
    free(cs->last_shown);
    for (int i = 0; i < cs->group_count; i++) line_group_destroy(cs->groups[i]);
    free(cs->groups);
    free(cs);
}

const char *compact_process(CompactState *cs, const char *line) {
    compact_output[0] = '\0';
    
    if (!cs || !cs->enabled || !line) return line;
    
    cs->total_lines_in++;
    
    // No previous line - show this one as-is, remember it
    if (!cs->last_shown) {
        cs->last_shown = strdup(line);
        cs->similar_pending = 0;
        cs->total_lines_out++;
        return line;
    }
    
    // Check similarity to last SHOWN line
    double sim = similarity(line, cs->last_shown);
    
    if (sim >= cs->similarity_threshold) {
        // Similar to previous - hide, count as similar
        cs->similar_pending++;
        cs->lines_grouped++;
        return "";  // hide
    }
    
    // Different from previous
    if (cs->similar_pending > 0) {
        // Output summary line for hidden similar lines, then the new line
        // Format: [-> Nx similar: <example>] where example is truncated last_shown
        char example[96];
        size_t llen = strlen(cs->last_shown);
        if (llen > 80) {
            snprintf(example, sizeof(example), "%.77s...", cs->last_shown);
        } else {
            snprintf(example, sizeof(example), "%s", cs->last_shown);
        }
        snprintf(compact_output, sizeof(compact_output),
                 "  \033[90m[-> %dx similar: %s]\033[0m\n%s",
                 cs->similar_pending, example, line);
        cs->total_lines_out += 2;
        cs->groups_detected++;
    } else {
        // Just show the new line
        strcpy(compact_output, line);
        cs->total_lines_out++;
    }
    
    // Update last shown
    free(cs->last_shown);
    cs->last_shown = strdup(line);
    cs->similar_pending = 0;
    
    return compact_output;
}

void compact_flush(CompactState *cs) {
    if (!cs) return;
    if (cs->similar_pending > 0) {
        char example[96];
        size_t llen = strlen(cs->last_shown);
        if (llen > 80) {
            snprintf(example, sizeof(example), "%.77s...", cs->last_shown);
        } else {
            snprintf(example, sizeof(example), "%s", cs->last_shown);
        }
        printf("  \033[90m[-> %dx similar: %s]\033[0m\n", cs->similar_pending, example);
        cs->total_lines_out++;
    }
    cs->similar_pending = 0;
}

bool compact_is_enabled(const CompactState *cs) { return cs && cs->enabled; }
void compact_set_enabled(CompactState *cs, bool enabled) { if (cs) cs->enabled = enabled; }
int compact_total_in(const CompactState *cs) { return cs ? cs->total_lines_in : 0; }
int compact_total_out(const CompactState *cs) { return cs ? cs->total_lines_out : 0; }
int compact_groups_detected(const CompactState *cs) { return cs ? cs->groups_detected : 0; }
int compact_lines_grouped(const CompactState *cs) { return cs ? cs->lines_grouped : 0; }

void compact_reset(CompactState *cs) {
    if (!cs) return;
    free(cs->last_shown);
    cs->last_shown = NULL;
    cs->buffer_count = 0;
    cs->similar_pending = 0;
    for (int i = 0; i < cs->group_count; i++) line_group_destroy(cs->groups[i]);
    free(cs->groups);
    cs->groups = NULL;
    cs->group_count = 0;
    cs->total_lines_in = cs->total_lines_out = cs->groups_detected = cs->lines_grouped = 0;
}
