#include "classifier.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

// Pattern definition
typedef struct {
    const char *name;     // pattern identifier
    const char *pattern;  // regex pattern
    LineType    type;     // resulting type
    int         score;    // importance score 0-100
    int         priority; // higher = checked first
} PatternDef;

// All patterns - order matters for matching
static PatternDef patterns[] = {
    // Critical - always show
    {"PANIC",      "^.*(PANIC|FATAL|ABORT|CRASH).*$",           LINE_ERROR,   100, 100},
    {"ERROR",      "^.*[Ee]rror(\\s*\\[|\\s*:|\\s+-).*$",       LINE_ERROR,   95,  95},
    {"FATAL",      "^.*[Ff]atal(\\s+error|:\\s+).*$",           LINE_ERROR,   95,  95},
    {"EXCEPTION",  "^.*[Ee]xception.*$",                        LINE_ERROR,   90,  90},
    {"FAILED",     "^.*([Ff]ailed|[Ee]rror):\\s*.*$",           LINE_ERROR,   85,  85},
    
    // Warnings - usually show
    {"WARN",       "^.*[Ww]arning(\\s*\\[|\\s*:|\\s+-).*$",     LINE_WARNING, 70,  80},
    {"WARN2",      "^.*\\[Ww\\].*$",                            LINE_WARNING, 65,  75},
    {"DEPRECATED", "^.*[Dd]eprecated.*$",                       LINE_WARNING, 60,  70},
    
    // Success indicators - use word boundaries
    {"OK",         "^.*\\[OK\\].*$",                            LINE_SUCCESS, 50,  70},
    {"DONE",       "^.*\\b[Dd]one\\b.*$",                       LINE_SUCCESS, 50,  60},
    {"PASSED",     "^.*\\b[Pp]assed\\b.*$",                     LINE_SUCCESS, 55,  65},
    {"SUCCESS",    "^.*\\b[Ss]uccess(ful)?\\b.*$",              LINE_SUCCESS, 50,  60},
    {"TEST_OK",    "^.*test\\s+\\S+\\s+ok$",                    LINE_SUCCESS, 45,  50},
    
    // Progress bars - typically noise
    {"PROGRESS",   "^.*\\[[#= ]+\\].*\\d+%.*$",                 LINE_PROGRESS, 20, 50},
    {"PROGRESS2",  "^.*\\[[#= ]+\\].*$",                        LINE_PROGRESS, 20, 45},
    {"PERCENT",    "^.*\\d+%.*$",                               LINE_PROGRESS, 15, 40},
    
    // Build system noise
    {"COMPILE",    "^.*[Cc]ompiling?\\s+\\S+.*$",               LINE_BUILD,   30,  40},
    {"LINK",       "^.*[Ll]inking?\\s+\\S+.*$",                 LINE_BUILD,   30,  40},
    {"BUILD_FINISH", "^.*(Building|Compiling|Linking)\\s+.*(done|finished|complete).*$", LINE_SUCCESS, 50, 50},
    {"DOWNLOAD",   "^.*[Dd]ownload(ing|ed)\\s+.*$",             LINE_BUILD,   25,  35},
    {"FETCH",      "^.*[Ff]etch(ing|ed)\\s+.*$",                LINE_BUILD,   25,  35},
    {"CARGO",      "^.*(Compiling|Finished|Checking)\\s+\\S+.*$", LINE_BUILD,  35,  45},
    
    // Test framework noise
    {"TEST_RUN",   "^.*running\\s+\\d+\\s+tests?.*$",           LINE_TEST,    40,  50},
    {"TEST_START", "^.*test\\s+\\S+.*\\.\\.\\.",                 LINE_TEST,    35,  45},
    {"TEST_OK",    "^.*test\\s+\\S+\\s+ok$",                    LINE_TEST,    45,  50},
    {"TEST_FAIL",  "^.*test\\s+\\S+\\s+FAILED$",                LINE_ERROR,   80,  80},
    {"TEST_PANIC", "^.*thread\\s+'[^']+'\\s+panicked.*$",       LINE_ERROR,   90,  90},
    {"RUST_TEST",  "^.*(test result|passed|failed|ignored|running).*$", LINE_TEST, 40, 45},
    
    // Info - medium priority
    {"INFO",       "^.*\\[INFO\\].*$",                          LINE_INFO,    45,  50},
    {"INFO2",      "^.*[Ii]nfo:\\s+.*$",                        LINE_INFO,    45,  50},
    
    // Debug - low priority
    {"DEBUG",      "^.*\\[DEBUG\\].*$",                          LINE_DEBUG,   30,  40},
    {"TRACE",      "^.*\\[TRACE\\].*$",                          LINE_DEBUG,   25,  35},
    
    // System info - typically noise
    {"TIMESTAMP",  "^\\d{4}-\\d{2}-\\d{2}[T\\s]\\d{2}:\\d{2}:\\d{2}.*$", LINE_NOISE, 15, 20},
    {"SYSLOG",     "^\\w{3}\\s+\\d+\\s+\\d{2}:\\d{2}:\\d{2}.*$", LINE_NOISE, 15, 20},
    {"EMPTY",      "^\\s*$",                                    LINE_NOISE,   0,   10},
};

#define NUM_PATTERNS (sizeof(patterns) / sizeof(patterns[0]))

// Compiled regex storage
static regex_t compiled[NUM_PATTERNS];
static int initialized = 0;
static int compile_errors[NUM_PATTERNS];

int classifier_init(void) {
    if (initialized) return 0;
    
    int errors = 0;
    for (size_t i = 0; i < NUM_PATTERNS; i++) {
        int res = regcomp(&compiled[i], patterns[i].pattern, REG_EXTENDED | REG_NOSUB);
        if (res != 0) {
            compile_errors[i] = res;
            errors++;
        } else {
            compile_errors[i] = 0;
        }
    }
    
    initialized = 1;
    return errors;
}

void classifier_free(void) {
    if (!initialized) return;
    
    for (size_t i = 0; i < NUM_PATTERNS; i++) {
        if (compile_errors[i] == 0) {
            regfree(&compiled[i]);
        }
    }
    initialized = 0;
}

Classification classify(const char *line) {
    Classification result = {LINE_UNKNOWN, 0, {0}};
    
    if (!line) return result;
    
    // Try patterns in priority order
    for (size_t i = 0; i < NUM_PATTERNS; i++) {
        if (compile_errors[i] != 0) continue;
        
        if (regexec(&compiled[i], line, 0, NULL, 0) == 0) {
            result.type = patterns[i].type;
            result.score = patterns[i].score;
            strncpy(result.matched, patterns[i].name, sizeof(result.matched) - 1);
            result.matched[sizeof(result.matched) - 1] = '\0';
            break;
        }
    }
    
    return result;
}

LineType classify_quick(const char *line) {
    Classification c = classify(line);
    return c.type;
}

LineType classification_type(const Classification *c) {
    return c ? c->type : LINE_UNKNOWN;
}

int classification_score(const Classification *c) {
    return c ? c->score : 0;
}

const char *classification_matched(const Classification *c) {
    return c ? c->matched : "";
}

const char *line_type_name(LineType t) {
    switch (t) {
        case LINE_ERROR:   return "ERROR";
        case LINE_WARNING: return "WARNING";
        case LINE_INFO:    return "INFO";
        case LINE_DEBUG:   return "DEBUG";
        case LINE_SUCCESS: return "SUCCESS";
        case LINE_PROGRESS:return "PROGRESS";
        case LINE_BUILD:   return "BUILD";
        case LINE_TEST:    return "TEST";
        case LINE_NOISE:   return "NOISE";
        case LINE_UNKNOWN: 
        default:           return "UNKNOWN";
    }
}
