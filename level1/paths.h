#ifndef SMARTFILTER_PATHS_H
#define SMARTFILTER_PATHS_H

#include "base/base.h"

typedef struct {
    char *value;
    size_t length;
    char name[128];
    size_t occurrences;
    bool used;
    bool url;
} path_entry;

typedef struct {
    path_entry *entries;
    size_t count;
    size_t capacity;
    char *rendered;
    size_t rendered_length;
    size_t rendered_capacity;
    bool used;
} paths_context;

bool paths_init(paths_context *context);
bool paths_transform(paths_context *context, sf_line input, sf_line *result,
                     bool abbreviate_paths, bool abbreviate_urls);
void paths_finish(paths_context *context, sf_output *output);
void paths_destroy(paths_context *context);

#endif
