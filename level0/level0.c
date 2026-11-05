#include "level0/level0.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    sf_line line;
    size_t position;
} cursor;

/* Ignore complete SGR sequences while classifying, including color changes
 * inside the marker itself. Preserve all original bytes in the actual output. */
static int peek(cursor *input)
{
    while (input->position < input->line.length) {
        size_t start = input->position;
        const char *data = input->line.data;
        if (data[start] == '\x1b' && input->line.length - start >= 3 && data[start + 1] == '[') {
            size_t end = start + 2;
            while (end < input->line.length &&
                   ((data[end] >= '0' && data[end] <= '9') || data[end] == ';' || data[end] == ':'))
                ++end;
            if (end < input->line.length && data[end] == 'm') {
                input->position = end + 1;
                continue;
            }
        }
        return (unsigned char)data[start];
    }
    return -1;
}

static bool letter(int ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static log_type type_of(const char *word)
{
    if (strcmp(word, "verbose") == 0)
        return LOG_VERBOSE;
    if (strcmp(word, "info") == 0)
        return LOG_INFO;
    if (strcmp(word, "error") == 0 || strcmp(word, "err") == 0)
        return LOG_ERROR;
    if (strcmp(word, "warning") == 0 || strcmp(word, "warn") == 0)
        return LOG_WARNING;
    return LOG_UNKNOWN;
}

log_type level0_classify(sf_line line)
{
    cursor input = {line, 0};
    int previous = -1;
    int ch;
    while ((ch = peek(&input)) != -1) {
        bool boundary = previous == -1 || previous == '[' || previous == ':' ||
                        isspace((unsigned char)previous);
        if (boundary && letter(ch)) {
            char word[16];
            size_t length = 0;
            bool too_long = false;
            do {
                if (length < sizeof(word) - 1)
                    word[length++] = (char)tolower((unsigned char)ch);
                else
                    too_long = true;
                ++input.position;
                ch = peek(&input);
            } while (letter(ch));
            word[length] = '\0';
            bool marked = ch == ':' || (previous == '[' && ch == ']');
            log_type type = too_long || !marked ? LOG_UNKNOWN : type_of(word);
            if (type != LOG_UNKNOWN)
                return type;
            previous = length ? word[length - 1] : previous;
        } else {
            previous = ch;
            ++input.position;
        }
    }
    return LOG_UNKNOWN;
}

static bool filter(void *context, const void *config, sf_line line, sf_output *output)
{
    level0_context *state = context;
    const level0_config *options = config;
    (void)output;
    ++state->lines_seen;
    /* Keep the default passthrough cheap. */
    if (!(options->hide_verbose || options->hide_info ||
          options->hide_error || options->hide_warning))
        return true;
    log_type type = level0_classify(line);
    ++state->counts[type];
    bool hide = (type == LOG_VERBOSE && options->hide_verbose) ||
                (type == LOG_INFO && options->hide_info) ||
                (type == LOG_ERROR && options->hide_error) ||
                (type == LOG_WARNING && options->hide_warning);
    if (hide)
        ++state->lines_dropped;
    return !hide;
}

sf_level level0_create(level0_context *context, const level0_config *config)
{
    return (sf_level){context, config, filter, NULL};
}
