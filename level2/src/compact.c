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

void line_group_add(LineGroup *g, const char *line) {
    if (!g || !line || g->count >= g->capacity) return;
    g->lines[g->count++] = strdup(line);
}

const char *line_group_get_summary(LineGroup *g) {
    static char summary[256];
    if (!g || g->count < 2) return NULL;
    snprintf(summary, sizeof(summary), "  \033[90m[-> %dx similar: %s]\033[0m", 
             g->count, g->lines[0]);
    return summary;
}

CompactState *compact_create(int buffer_size, int min_group_size, double similarity_threshold) {
    if (buffer_size <= 0) buffer_size = 100;
    if (min_group_size < 2) min_group_size = 2;  // Minimum 2 to form a group
    if (similarity_threshold <= 0) similarity_threshold = 0.6;
    
    CompactState *cs = calloc(1, sizeof(CompactState));
    if (!cs) return NULL;
    
    cs->buffer = calloc(buffer_size, sizeof(char*));
    cs->buffer_size = buffer_size;
    cs->min_group_size = min_group_size;
    cs->similarity_threshold = similarity_threshold;
    cs->enabled = true;
    
    return cs;
}

void compact_destroy(CompactState *cs) {
    if (!cs) return;
    for (int i = 0; i < cs->buffer_count; i++) free(cs->buffer[i]);
    free(cs->buffer);
    for (int i = 0; i < cs->group_count; i++) line_group_destroy(cs->groups[i]);
    free(cs->groups);
    free(cs);
}

// Check if lines from start form a group
static int find_group(char **buffer, int count, int start, int min_size, double threshold) {
    if (count - start < min_size) return 0;
    
    for (int i = start + 1; i < count; i++) {
        if (similarity(buffer[start], buffer[i]) < threshold) {
            return 0;  // Gap too big
        }
    }
    
    return count - start;
}

const char *compact_process(CompactState *cs, const char *line) {
    compact_output[0] = '\0';
    
    if (!cs || !cs->enabled || !line) return line;
    
    cs->total_lines_in++;
    
    // Add to buffer
    if (cs->buffer_count < cs->buffer_size) {
        cs->buffer[cs->buffer_count++] = strdup(line);
    } else {
        // Buffer full, can't add more - need to flush something
        // For now, just replace oldest
        free(cs->buffer[0]);
        memmove(cs->buffer, cs->buffer + 1, (cs->buffer_size - 1) * sizeof(char*));
        cs->buffer[cs->buffer_size - 1] = strdup(line);
    }
    
    // Check if we have enough lines to form a group at the start
    int group_size = find_group(cs->buffer, cs->buffer_count, 0, 
                                 cs->min_group_size, cs->similarity_threshold);
    
    if (group_size >= cs->min_group_size) {
        // Found a group at the start of buffer!
        cs->groups_detected++;
        cs->lines_grouped += group_size;
        
        // Output first few + summary
        int to_show = (group_size < 5) ? group_size : 4;
        
        for (int i = 0; i < to_show; i++) {
            strcat(compact_output, cs->buffer[i]);
            strcat(compact_output, "\n");
            cs->total_lines_out++;
        }
        
        if (group_size > to_show) {
            LineGroup g = {.lines = cs->buffer, .count = group_size, .example_idx = 0};
            const char *summary = line_group_get_summary(&g);
            if (summary) {
                strcat(compact_output, summary);
                strcat(compact_output, "\n");
                cs->total_lines_out++;
            }
        }
        
        // Remove group from buffer
        for (int i = 0; i < group_size; i++) free(cs->buffer[i]);
        cs->buffer_count -= group_size;
        if (cs->buffer_count > 0) {
            memmove(cs->buffer, cs->buffer + group_size, cs->buffer_count * sizeof(char*));
        }
        
        return compact_output;
    }
    
    // No group at start. If buffer has unprocessed lines, output them.
    // But only if we have at least min_group_size lines and they're not forming a group
    // Otherwise, keep buffering.
    
    if (cs->buffer_count >= cs->min_group_size * 2) {
        // Buffer getting too full, output oldest line
        char *result = cs->buffer[0];
        memmove(cs->buffer, cs->buffer + 1, (cs->buffer_count - 1) * sizeof(char*));
        cs->buffer_count--;
        cs->total_lines_out++;
        return result;
    }
    
    // Keep buffering
    return "";
}

void compact_flush(CompactState *cs) {
    if (!cs) return;
    
    // First, try to form groups from remaining buffer
    int pos = 0;
    while (pos < cs->buffer_count) {
        int group_size = find_group(cs->buffer, cs->buffer_count, pos,
                                    cs->min_group_size, cs->similarity_threshold);
        
        if (group_size >= cs->min_group_size) {
            // Output group
            cs->groups_detected++;
            cs->lines_grouped += group_size;
            
            int to_show = (group_size < 5) ? group_size : 4;
            for (int i = pos; i < pos + to_show; i++) {
                printf("%s\n", cs->buffer[i]);
                cs->total_lines_out++;
            }
            if (group_size > to_show) {
                printf("  \033[90m[-> %dx similar: %s]\033[0m\n", group_size, cs->buffer[pos]);
                cs->total_lines_out++;
            }
            pos += group_size;
        } else {
            // Output single line
            printf("%s\n", cs->buffer[pos]);
            cs->total_lines_out++;
            pos++;
        }
    }
    
    cs->buffer_count = 0;
}

bool compact_is_enabled(const CompactState *cs) { return cs && cs->enabled; }
void compact_set_enabled(CompactState *cs, bool enabled) { if (cs) cs->enabled = enabled; }
int compact_total_in(const CompactState *cs) { return cs ? cs->total_lines_in : 0; }
int compact_total_out(const CompactState *cs) { return cs ? cs->total_lines_out : 0; }
int compact_groups_detected(const CompactState *cs) { return cs ? cs->groups_detected : 0; }
int compact_lines_grouped(const CompactState *cs) { return cs ? cs->lines_grouped : 0; }

void compact_reset(CompactState *cs) {
    if (!cs) return;
    for (int i = 0; i < cs->buffer_count; i++) free(cs->buffer[i]);
    cs->buffer_count = 0;
    for (int i = 0; i < cs->group_count; i++) line_group_destroy(cs->groups[i]);
    free(cs->groups);
    cs->groups = NULL;
    cs->group_count = 0;
    cs->total_lines_in = cs->total_lines_out = cs->groups_detected = cs->lines_grouped = 0;
}
