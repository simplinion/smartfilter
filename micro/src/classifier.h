#ifndef CLASSIFIER_H
#define CLASSIFIER_H

#include <stdint.h>

// Line classification types
typedef enum {
    LINE_UNKNOWN = 0,
    LINE_ERROR,
    LINE_WARNING,
    LINE_INFO,
    LINE_DEBUG,
    LINE_SUCCESS,
    LINE_PROGRESS,
    LINE_BUILD,      // build-specific noise (e.g. "Compiling foo.c")
    LINE_TEST,       // test-specific noise (e.g. test framework output)
    LINE_NOISE,      // definitely skip
} LineType;

// Classification result
typedef struct {
    LineType type;
    int      score;      // 0-100, how "important" is this line
    char     matched[64]; // which pattern matched (for debug)
} Classification;

// Classify a single line
Classification classify(const char *line);

// Initialize classifier (compile regexes)
int  classifier_init(void);
void classifier_free(void);

#endif // CLASSIFIER_H
