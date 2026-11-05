#ifndef DEDUP_H
#define DEDUP_H

#include <stdbool.h>
#include <stdint.h>

// Deduplication state - tracks seen lines
typedef struct {
    uint32_t  *hashes;          // hash values we've seen
    char     **lines;           // original lines (for reference)
    int        capacity;        // max entries
    int        count;           // current entries
    int        max_memory_kb;   // memory limit (0 = unlimited)
    bool       enabled;         // dedup enabled
} DedupState;

// Create dedup state
DedupState *dedup_create(int max_entries);

// Free dedup state
void dedup_destroy(DedupState *d);

// Check if line is new (not seen before)
// Returns true if NEW (should show), false if already seen (skip)
bool dedup_check_and_add(DedupState *d, const char *line);

// Check if line is new without adding
bool dedup_is_new(const DedupState *d, const char *line);

// Add line to seen set (without check)
void dedup_add(DedupState *d, const char *line);

// Check similarity between two lines (0-100)
// Used for "similar enough" deduplication
int dedup_similarity(const char *a, const char *b);

// Clear all seen lines
void dedup_reset(DedupState *d);

// Get count of seen lines
int dedup_count(const DedupState *d);

// Enable/disable
void dedup_set_enabled(DedupState *d, bool enabled);

#endif // DEDUP_H
