#ifndef CONTEXT_H
#define CONTEXT_H

#include "../shared/types.h"
#include "classifier.h"
#include <stddef.h>

// Rolling context window - remembers recent lines
typedef struct {
    char          *lines[32];      // recent lines (owned)
    LineType       types[32];      // their types
    int            scores[32];     // their scores
    int            head;           // next write position
    int            count;          // total lines seen
    int            max_size;       // window size
    
    // State tracking
    int            error_count;
    int            warning_count;
    int            phase;          // current build phase (BUILD, TEST, etc.)
    char           last_unique[256]; // last non-noise line
    int            repeat_count;   // how many times last_unique repeated
} Context;

// Initialize context with given window size
Context *ctx_create(int window_size);

// Free context and all stored lines
void ctx_destroy(Context *ctx);

// Add a line to the context
void ctx_add(Context *ctx, const char *line, LineType type, int score);

// Get recent lines (newest first)
const char **ctx_recent_lines(const Context *ctx, int *out_count);

// Get recent types
const LineType *ctx_recent_types(const Context *ctx, int *out_count);

// Check if line is a repetition of last significant line
int ctx_is_repetition(const Context *ctx, const char *line);

// Get repetition count
int ctx_repeat_count(const Context *ctx);

// Check if we're in a specific phase
int ctx_in_phase(const Context *ctx, LineType phase);

// Increment phase
void ctx_set_phase(Context *ctx, LineType phase);

// Get counts
int ctx_error_count(const Context *ctx);
int ctx_warning_count(const Context *ctx);
int ctx_total_lines(const Context *ctx);

// Reset context state
void ctx_reset(Context *ctx);

#endif // CONTEXT_H
