#include "level1/compact.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static bool reserve(char **buffer, size_t *capacity, size_t needed)
{
    if (needed <= *capacity)
        return true;
    size_t grown = *capacity ? *capacity : 256;
    while (grown < needed) {
        if (grown > SIZE_MAX / 2) {
            grown = needed;
            break;
        }
        grown *= 2;
    }
    char *next = realloc(*buffer, grown);
    if (!next)
        return false;
    *buffer = next;
    *capacity = grown;
    return true;
}

static size_t content_length(sf_line line)
{
    size_t length = line.length;
    if (length && line.data[length - 1] == '\n') {
        --length;
        if (length && line.data[length - 1] == '\r')
            --length;
    }
    return length;
}

/* Recognize SGR colors/styles only; other controls retain their original line. */
static size_t sgr_length(const char *data, size_t length)
{
    if (length < 3 || data[0] != '\x1b' || data[1] != '[')
        return 0;
    size_t i = 2;
    while (i < length && ((data[i] >= '0' && data[i] <= '9') ||
                          data[i] == ';' || data[i] == ':'))
        ++i;
    return i < length && data[i] == 'm' ? i + 1 : 0;
}

/* ANSI SGR sequences occupy no terminal columns. */
static bool columns(const char *data, size_t length, size_t *width)
{
    mbstate_t state = {0};
    *width = 0;
    for (size_t i = 0; i < length;) {
        size_t sgr = sgr_length(data + i, length - i);
        if (sgr) {
            i += sgr;
            continue;
        }
        if (data[i] == '\t') {
            *width += 8 - *width % 8;
            ++i;
            continue;
        }
        wchar_t ch;
        size_t count = mbrtowc(&ch, data + i, length - i, &state);
        if (count == (size_t)-1 || count == (size_t)-2 || count == 0)
            return false;
        int cells = wcwidth(ch);
        if (cells < 0)
            return false;
        *width += (size_t)cells;
        i += count;
    }
    return true;
}

/* Keep identifiers, digits, and UTF-8 word bytes together. Punctuation
 * (including path separators) can still be used as a compaction boundary. */
static bool word_byte(unsigned char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '_' || ch >= 0x80;
}

bool compact_filter(void *context, const void *config, sf_line line,
                   sf_output *output)
{
    compact_context *state = context;
    const compact_config *options = config;
    if (!options->compact)
        return true;

    size_t length = content_length(line);
    size_t common = 0;
    size_t matched = 0;
    while (common < length) {
        size_t sgr = sgr_length(line.data + common, length - common);
        if (sgr) {
            common += sgr;
            continue;
        }
        if (matched == state->previous_length ||
            line.data[common] != state->previous[matched])
            break;
        ++common;
        ++matched;
    }
    /* Never cut inside a UTF-8 character. */
    while (common < length && common &&
           ((unsigned char)line.data[common] & 0xc0) == 0x80)
        --common;

    /* If the difference splits a word, expose that entire word. Scan while
     * ignoring SGR, so style changes do not create false word boundaries. */
    bool in_word = false;
    size_t word_start = 0;
    size_t word_matched = 0;
    size_t visible = 0;
    for (size_t i = 0; i < common;) {
        size_t sgr = sgr_length(line.data + i, common - i);
        if (sgr) {
            i += sgr;
            continue;
        }
        bool word = word_byte((unsigned char)line.data[i]);
        if (word && !in_word) {
            word_start = i;
            word_matched = visible;
        }
        in_word = word;
        ++visible;
        ++i;
    }
    size_t next = common;
    while (next < length) {
        size_t sgr = sgr_length(line.data + next, length - next);
        if (!sgr)
            break;
        next += sgr;
    }
    if (in_word && ((next < length && word_byte((unsigned char)line.data[next])) ||
                    (matched < state->previous_length &&
                     word_byte((unsigned char)state->previous[matched])))) {
        common = word_start;
        matched = word_matched;
    }

    size_t width = 0;
    bool compact = matched >= options->minimum_prefix && matched > 0 &&
                   columns(line.data, common, &width) && width > 0;
    size_t rendered_length = 0;
    if (compact) {
        const char marker[] = "\xe2\x86\xb3 "; /* ↳ followed by a space */
        size_t indent = width >= 2 ? width - 2 : 0;
        size_t suffix = line.length - common;
        /* At most common bytes of preserved SGR plus indent, marker, suffix. */
        if (line.length > SIZE_MAX - indent ||
            line.length + indent > SIZE_MAX - (sizeof(marker) - 1))
            goto fail;
        size_t needed = line.length + indent + sizeof(marker) - 1;
        if (!reserve(&state->rendered, &state->rendered_capacity, needed))
            goto fail;
        /* Replay prefix styles so that the remaining suffix has the same
         * effective color, including resets and changes within the prefix. */
        for (size_t i = 0; i < common;) {
            size_t sgr = sgr_length(line.data + i, common - i);
            if (sgr) {
                memcpy(state->rendered + rendered_length, line.data + i, sgr);
                rendered_length += sgr;
                i += sgr;
            } else {
                ++i;
            }
        }
        memset(state->rendered + rendered_length, ' ', indent);
        rendered_length += indent;
        memcpy(state->rendered + rendered_length, marker, sizeof(marker) - 1);
        rendered_length += sizeof(marker) - 1;
        memcpy(state->rendered + rendered_length, line.data + common, suffix);
        rendered_length += suffix;
    }
    if (!reserve(&state->previous, &state->previous_capacity, length))
        goto fail;
    state->previous_length = 0;
    for (size_t i = 0; i < length;) {
        size_t sgr = sgr_length(line.data + i, length - i);
        if (sgr)
            i += sgr;
        else
            state->previous[state->previous_length++] = line.data[i++];
    }
    if (compact) {
        sf_emit(output, sf_line_rewrite(line, state->rendered, rendered_length));
        return false;
    }
    return true;

fail:
    state->allocation_failed = true;
    sf_output_fail(output);
    return false;
}

void compact_destroy(compact_context *context)
{
    free(context->previous);
    free(context->rendered);
    *context = (compact_context){0};
}
