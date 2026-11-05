#ifndef SMARTFILTER_LEVEL3_H
#define SMARTFILTER_LEVEL3_H

#include "base/base.h"

typedef enum {
    CATEGORY_PROGRESS,
    CATEGORY_ERROR,
    CATEGORY_WARNING,
    CATEGORY_INFO,
    CATEGORY_RESULT,
    CATEGORY_STATS,
    CATEGORY_VERSION,
    CATEGORY_UNKNOWN,
    CATEGORY_FORMATTING, /* Deterministic separator category, outside the neural outputs. */
    CATEGORY_LISTING,
    CATEGORY_DIAGNOSTIC,
    CATEGORY_COUNT
} log_category;

#define LEVEL3_ALL ((1u << CATEGORY_COUNT) - 1u)

typedef struct {
    log_category category;
    float scores[CATEGORY_COUNT]; /* Softmax scores, not calibrated certainty. */
} level3_prediction;

typedef struct {
    bool enabled;
    bool annotate;
    bool color;
    unsigned show;
} level3_config;

typedef struct {
    size_t counts[CATEGORY_COUNT];
    size_t lines_dropped;
    char *rendered;
    size_t capacity;
    bool allocation_failed;
} level3_context;

const char *level3_category_name(log_category category);
bool level3_parse_categories(const char *text, unsigned *mask);
/* Exposed separately for Python/C parity checks. Inference only, no rules. */
void level3_model_scores(sf_line line, float scores[CATEGORY_COUNT]);
level3_prediction level3_classify(sf_line line);
sf_level level3_create(level3_context *context, const level3_config *config);
void level3_destroy(level3_context *context);

#endif
