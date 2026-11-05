#include "dedup.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Simple hash function (djb2)
static uint32_t hash_line(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

DedupState *dedup_create(int max_entries) {
    if (max_entries <= 0) max_entries = 10000;
    if (max_entries > 100000) max_entries = 100000;
    
    DedupState *d = calloc(1, sizeof(DedupState));
    if (!d) return NULL;
    
    d->hashes = calloc(max_entries, sizeof(uint32_t));
    d->lines = calloc(max_entries, sizeof(char*));
    
    if (!d->hashes || !d->lines) {
        free(d->hashes);
        free(d->lines);
        free(d);
        return NULL;
    }
    
    d->capacity = max_entries;
    d->count = 0;
    d->enabled = true;
    
    return d;
}

void dedup_destroy(DedupState *d) {
    if (!d) return;
    
    for (int i = 0; i < d->count; i++) {
        free(d->lines[i]);
    }
    free(d->hashes);
    free(d->lines);
    free(d);
}

bool dedup_check_and_add(DedupState *d, const char *line) {
    if (!d || !d->enabled || !line) return true;
    
    uint32_t h = hash_line(line);
    
    // Check if we've seen this hash
    for (int i = 0; i < d->count; i++) {
        if (d->hashes[i] == h) {
            // Hash matches, but verify actual string (avoid hash collision)
            if (d->lines[i] && strcmp(d->lines[i], line) == 0) {
                return false; // Already seen
            }
        }
    }
    
    // New line - add to set
    if (d->count < d->capacity) {
        d->hashes[d->count] = h;
        d->lines[d->count] = strdup(line);
        d->count++;
    }
    
    return true; // New, show it
}

bool dedup_is_new(const DedupState *d, const char *line) {
    if (!d || !d->enabled || !line) return true;
    
    uint32_t h = hash_line(line);
    
    for (int i = 0; i < d->count; i++) {
        if (d->hashes[i] == h && d->lines[i] && strcmp(d->lines[i], line) == 0) {
            return false;
        }
    }
    
    return true;
}

void dedup_add(DedupState *d, const char *line) {
    if (!d || !d->enabled || !line) return;
    
    if (d->count >= d->capacity) {
        // Remove oldest entry (simple FIFO eviction)
        free(d->lines[0]);
        memmove(d->hashes, d->hashes + 1, (d->capacity - 1) * sizeof(uint32_t));
        memmove(d->lines, d->lines + 1, (d->capacity - 1) * sizeof(char*));
        d->count--;
    }
    
    d->hashes[d->count] = hash_line(line);
    d->lines[d->count] = strdup(line);
    d->count++;
}

int dedup_similarity(const char *a, const char *b) {
    if (!a || !b) return 0;
    
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    if (len_a == 0 || len_b == 0) return 0;
    
    // Simple similarity based on common prefix
    size_t common = 0;
    while (common < len_a && common < len_b && a[common] == b[common]) {
        common++;
    }
    
    // Also check common suffix (for messages with varying numbers)
    size_t suffix_a = len_a - 1;
    size_t suffix_b = len_b - 1;
    size_t common_suffix = 0;
    while (suffix_a > 0 && suffix_b > 0 && a[suffix_a] == b[suffix_b]) {
        common_suffix++;
        suffix_a--;
        suffix_b--;
    }
    
    size_t max_len = (len_a > len_b) ? len_a : len_b;
    double sim = (double)(common + common_suffix) / (double)max_len;
    
    return (int)(sim * 100);
}

void dedup_reset(DedupState *d) {
    if (!d) return;
    
    for (int i = 0; i < d->count; i++) {
        free(d->lines[i]);
        d->lines[i] = NULL;
        d->hashes[i] = 0;
    }
    d->count = 0;
}

int dedup_count(const DedupState *d) {
    return d ? d->count : 0;
}

void dedup_set_enabled(DedupState *d, bool enabled) {
    if (d) d->enabled = enabled;
}
