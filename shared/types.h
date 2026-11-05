#ifndef TYPES_H
#define TYPES_H

// Line classification types - ordered by importance
typedef enum {
    LINE_UNKNOWN = 0,
    LINE_NOISE,        // definitely skip
    LINE_PROGRESS,     // progress bars, spinners
    LINE_BUILD,        // build system noise
    LINE_TEST,         // test framework output
    LINE_DEBUG,        // debug messages
    LINE_INFO,         // informational
    LINE_SUCCESS,      // success indicators
    LINE_WARNING,      // warnings
    LINE_ERROR,        // errors (critical)
} LineType;

// Filter operating modes
typedef enum {
    MODE_CUSTOM = 0,
    MODE_PERMISSIVE,   // show most things
    MODE_AGGRESSIVE,   // hide almost everything
} Mode;

#endif // TYPES_H
