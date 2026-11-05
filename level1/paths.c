#include "level1/paths.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool add(paths_context *state, const char *value, size_t length)
{
    if (state->count == state->capacity) {
        size_t capacity = state->capacity ? state->capacity * 2 : 16;
        if (capacity < state->capacity || capacity > SIZE_MAX / sizeof(path_entry))
            return false;
        path_entry *entries = realloc(state->entries, capacity * sizeof(*entries));
        if (!entries)
            return false;
        state->entries = entries;
        state->capacity = capacity;
    }
    char *copy = malloc(length + 1);
    if (!copy)
        return false;
    memcpy(copy, value, length);
    copy[length] = '\0';
    path_entry *entry = &state->entries[state->count];
    *entry = (path_entry){.value = copy, .length = length};
    ++state->count;
    return true;
}

bool paths_init(paths_context *state)
{
    size_t capacity = 256;
    for (;;) {
        char *cwd = malloc(capacity);
        if (!cwd)
            return false;
        if (getcwd(cwd, capacity)) {
            bool ok = add(state, cwd, strlen(cwd));
            if (ok)
                state->entries[0].occurrences = 2; /* Known from startup. */
            free(cwd);
            return ok;
        }
        int error = errno;
        free(cwd);
        /* Only ERANGE means the buffer needs growing. */
        if (error != ERANGE || capacity > SIZE_MAX / 2)
            return false;
        capacity *= 2;
    }
}

static bool append(paths_context *state, const char *data, size_t length)
{
    if (length > SIZE_MAX - state->rendered_length)
        return false;
    size_t needed = state->rendered_length + length;
    if (needed > state->rendered_capacity) {
        size_t capacity = state->rendered_capacity ? state->rendered_capacity : 256;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) {
                capacity = needed;
                break;
            }
            capacity *= 2;
        }
        char *buffer = realloc(state->rendered, capacity);
        if (!buffer)
            return false;
        state->rendered = buffer;
        state->rendered_capacity = capacity;
    }
    if (length)
        memcpy(state->rendered + state->rendered_length, data, length);
    state->rendered_length += length;
    return true;
}

static bool delimiter(unsigned char ch)
{
    return ch <= ' ' || strchr("\"'<>[](){},;:=", ch) != NULL;
}

/* Track every directory boundary, so paths in sibling directories can reveal
 * a shared ancestor. A candidate becomes usable on its second occurrence. */
static bool observe(paths_context *state, const char *data, size_t length)
{
    for (size_t end = 1; end < length; ++end) {
        if (data[end] != '/' || data[end - 1] == '/')
            continue;
        size_t index = 0;
        while (index < state->count &&
               (state->entries[index].length != end ||
                memcmp(state->entries[index].value, data, end) != 0))
            ++index;
        if (index == state->count && !add(state, data, end))
            return false;
        if (state->entries[index].occurrences < 2)
            ++state->entries[index].occurrences;
    }
    return true;
}

static bool name_taken(const paths_context *state, const char *name)
{
    for (size_t i = 0; i < state->count; ++i)
        if (state->entries[i].used && strcmp(state->entries[i].name, name) == 0)
            return true;
    return false;
}

static void assign_name(paths_context *state, path_entry *entry)
{
    if (entry->name[0])
        return;
    size_t start = entry->length;
    while (start && entry->value[start - 1] != '/')
        --start;
    char base[84];
    size_t used = 0;
    base[used++] = '$';
    if (start < entry->length && entry->value[start] >= '0' && entry->value[start] <= '9')
        base[used++] = '_';
    for (size_t i = start; i < entry->length && used < sizeof(base) - 1; ++i) {
        unsigned char ch = (unsigned char)entry->value[i];
        base[used++] = ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '_') ? (char)ch : '_';
    }
    base[used] = '\0';
    char candidate[sizeof(entry->name)];
    snprintf(candidate, sizeof(candidate), "%s", base);
    for (size_t suffix = 2; name_taken(state, candidate); ++suffix)
        snprintf(candidate, sizeof(candidate), "%s_%zu", base, suffix);
    memcpy(entry->name, candidate, strlen(candidate) + 1);
}

/* Maximize saved bytes, rather than matched prefix length. Existing variables
 * win ties; candidate names (including collision suffixes) reserve nothing
 * until selected and actually used. */
static path_entry *shortest(paths_context *state, const char *data, size_t length)
{
    path_entry *best = NULL;
    size_t savings = 0;
    char best_name[sizeof(state->entries[0].name)] = {0};
    for (int existing = 1; existing >= 0; --existing) {
        for (size_t i = 0; i < state->count; ++i) {
            path_entry *entry = &state->entries[i];
            if (entry->used != (existing != 0) || entry->occurrences < 2 ||
                entry->length <= 1 || entry->length > length ||
                memcmp(entry->value, data, entry->length) != 0 ||
                (entry->length != length && data[entry->length] != '/'))
                continue;
            path_entry proposal = *entry;
            if (!entry->used) {
                proposal.name[0] = '\0';
                assign_name(state, &proposal);
            }
            size_t name_length = strlen(proposal.name);
            if (name_length < entry->length && entry->length - name_length > savings) {
                best = entry;
                savings = entry->length - name_length;
                memcpy(best_name, proposal.name, sizeof(best_name));
            }
        }
    }
    if (best && !best->used)
        memcpy(best->name, best_name, sizeof(best->name));
    return best;
}

bool paths_transform(paths_context *state, sf_line input, sf_line *result)
{
    state->rendered_length = 0;
    bool changed = false;
    for (size_t i = 0; i < input.length;) {
        /* Avoid relative paths, URL authorities, and slashes inside words. */
        bool start = input.data[i] == '/' &&
                     (i == 0 || (delimiter((unsigned char)input.data[i - 1]) &&
                                 input.data[i - 1] != ':')) &&
                     (i + 1 == input.length || input.data[i + 1] != '/');
        if (!start) {
            if (!append(state, input.data + i, 1))
                return false;
            ++i;
            continue;
        }
        size_t end = i + 1;
        char quote = i && (input.data[i - 1] == '\'' || input.data[i - 1] == '"')
                     ? input.data[i - 1] : 0;
        while (end < input.length) {
            unsigned char ch = (unsigned char)input.data[end];
            if (ch < ' ' || (quote ? ch == (unsigned char)quote : delimiter(ch)))
                break;
            ++end;
        }
        size_t length = end - i;
        if (!observe(state, input.data + i, length))
            return false;
        path_entry *entry = shortest(state, input.data + i, length);
        if (entry) {
            if (!append(state, entry->name, strlen(entry->name)) ||
                !append(state, input.data + i + entry->length, length - entry->length))
                return false;
            changed = state->used = entry->used = true;
        } else if (!append(state, input.data + i, length)) {
            return false;
        }
        i = end;
    }
    *result = changed ? (sf_line){.data = state->rendered,
                                .length = state->rendered_length} : input;
    return true;
}

static int definition_order(const void *left, const void *right)
{
    const path_entry *a = *(const path_entry *const *)left;
    const path_entry *b = *(const path_entry *const *)right;
    if (a->length != b->length)
        return a->length < b->length ? -1 : 1;
    return strcmp(a->value, b->value);
}

void paths_finish(paths_context *state, sf_output *output)
{
    if (!state->used)
        return;
    path_entry **definitions = malloc(state->count * sizeof(*definitions));
    if (!definitions) {
        sf_output_fail(output);
        return;
    }
    size_t count = 0;
    for (size_t i = 0; i < state->count; ++i)
        if (state->entries[i].used)
            definitions[count++] = &state->entries[i];
    /* Strictly shorter parents always precede their children, even if the
     * parent was discovered after the child during streaming. */
    qsort(definitions, count, sizeof(*definitions), definition_order);
    if (!sf_emit(output, (sf_line){.data = "\n[paths]\n", .length = 9}))
        goto done;
    for (size_t i = 0; i < count; ++i) {
        path_entry *entry = definitions[i];
        const path_entry *parent = NULL;
        size_t savings = 0;
        for (size_t j = 0; j < i; ++j) {
            const path_entry *candidate = definitions[j];
            size_t name_length = strlen(candidate->name);
            if (candidate->length < entry->length && name_length < candidate->length &&
                candidate->length - name_length > savings &&
                entry->value[candidate->length] == '/' &&
                memcmp(candidate->value, entry->value, candidate->length) == 0) {
                parent = candidate;
                savings = candidate->length - name_length;
            }
        }
        state->rendered_length = 0;
        if (!append(state, entry->name, strlen(entry->name)) ||
            !append(state, " = ", 3) ||
            (parent && !append(state, parent->name, strlen(parent->name))) ||
            !append(state, entry->value + (parent ? parent->length : 0),
                    entry->length - (parent ? parent->length : 0)) ||
            !append(state, "\n", 1)) {
            sf_output_fail(output);
            goto done;
        }
        if (!sf_emit(output, (sf_line){.data = state->rendered,
                                     .length = state->rendered_length}))
            goto done;
    }
done:
    free(definitions);
}

void paths_destroy(paths_context *state)
{
    for (size_t i = 0; i < state->count; ++i)
        free(state->entries[i].value);
    free(state->entries);
    free(state->rendered);
    *state = (paths_context){0};
}
