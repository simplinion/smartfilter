#ifndef CLASSIFIER_H
#define CLASSIFIER_H

#include "../shared/types.h"
#include <regex.h>

// Classification result
typedef struct {
    LineType type;
    int      score;      // 0-100, importance
    char     matched[64]; // which pattern matched
} Classification;

// Initialize classifier with compiled regexes
int  classifier_init(void);
void classifier_free(void);

// Classify a single line
Classification classify(const char *line);

// Classify without allocation (uses static buffer)
LineType classify_quick(const char *line);

// Get line type from classification
LineType classification_type(const Classification *c);

// Get score from classification  
int classification_score(const Classification *c);

// Get matched pattern name
const char *classification_matched(const Classification *c);

// Get human-readable name for LineType
const char *line_type_name(LineType t);

#endif // CLASSIFIER_H
