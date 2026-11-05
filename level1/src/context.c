#include "context.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

Context *ctx_create(int window_size) {
    if (window_size <= 0) window_size = 32;
    if (window_size > 128) window_size = 128;
    
    Context *ctx = calloc(1, sizeof(Context));
    if (!ctx) return NULL;
    
    ctx->max_size = window_size;
    ctx->head = 0;
    ctx->count = 0;
    ctx->error_count = 0;
    ctx->warning_count = 0;
    ctx->repeat_count = 0;
    ctx->phase = LINE_UNKNOWN;
    ctx->last_unique[0] = '\0';
    
    for (int i = 0; i < 32; i++) {
        ctx->lines[i] = NULL;
    }
    
    return ctx;
}

void ctx_destroy(Context *ctx) {
    if (!ctx) return;
    
    for (int i = 0; i < 32; i++) {
        free(ctx->lines[i]);
        ctx->lines[i] = NULL;
    }
    
    free(ctx);
}

void ctx_add(Context *ctx, const char *line, LineType type, int score) {
    if (!ctx || !line) return;
    
    // Track error/warning counts
    if (type == LINE_ERROR) ctx->error_count++;
    if (type == LINE_WARNING) ctx->warning_count++;
    ctx->count++;
    
    // Track phase transitions
    if (type == LINE_BUILD || type == LINE_TEST) {
        if (ctx->phase != type) {
            ctx->phase = type;
        }
    }
    
    // Free old line at head position
    free(ctx->lines[ctx->head]);
    
    // Store new line
    ctx->lines[ctx->head] = strdup(line);
    ctx->types[ctx->head] = type;
    ctx->scores[ctx->head] = score;
    
    // Advance head
    ctx->head = (ctx->head + 1) % 32;
    if (ctx->count > 32) ctx->count = 32;
    
    // Track repetitions (only for non-noise, non-error lines)
    if (type != LINE_NOISE && type != LINE_ERROR) {
        if (strcmp(line, ctx->last_unique) == 0) {
            ctx->repeat_count++;
        } else {
            strncpy(ctx->last_unique, line, sizeof(ctx->last_unique) - 1);
            ctx->last_unique[sizeof(ctx->last_unique) - 1] = '\0';
            ctx->repeat_count = 0;
        }
    }
}

const char **ctx_recent_lines(const Context *ctx, int *out_count) {
    static const char *result[32];
    
    if (!ctx || !out_count) return NULL;
    
    int n = ctx->count < 32 ? ctx->count : 32;
    *out_count = n;
    
    for (int i = 0; i < n; i++) {
        int idx = (ctx->head - 1 - i + 32) % 32;
        result[i] = ctx->lines[idx];
    }
    
    return result;
}

const LineType *ctx_recent_types(const Context *ctx, int *out_count) {
    static LineType result[32];
    
    if (!ctx || !out_count) return NULL;
    
    int n = ctx->count < 32 ? ctx->count : 32;
    *out_count = n;
    
    for (int i = 0; i < n; i++) {
        int idx = (ctx->head - 1 - i + 32) % 32;
        result[i] = ctx->types[idx];
    }
    
    return result;
}

int ctx_is_repetition(const Context *ctx, const char *line) {
    if (!ctx || !line) return 0;
    return strcmp(line, ctx->last_unique) == 0 && ctx->repeat_count > 0;
}

int ctx_repeat_count(const Context *ctx) {
    return ctx ? ctx->repeat_count : 0;
}

int ctx_in_phase(const Context *ctx, LineType phase) {
    return ctx && ctx->phase == phase;
}

void ctx_set_phase(Context *ctx, LineType phase) {
    if (ctx) ctx->phase = phase;
}

int ctx_error_count(const Context *ctx) {
    return ctx ? ctx->error_count : 0;
}

int ctx_warning_count(const Context *ctx) {
    return ctx ? ctx->warning_count : 0;
}

int ctx_total_lines(const Context *ctx) {
    return ctx ? ctx->count : 0;
}

void ctx_reset(Context *ctx) {
    if (!ctx) return;
    
    for (int i = 0; i < 32; i++) {
        free(ctx->lines[i]);
        ctx->lines[i] = NULL;
    }
    
    ctx->head = 0;
    ctx->count = 0;
    ctx->error_count = 0;
    ctx->warning_count = 0;
    ctx->phase = LINE_UNKNOWN;
    ctx->repeat_count = 0;
    ctx->last_unique[0] = '\0';
}
