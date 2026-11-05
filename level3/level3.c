#include "level3/level3.h"
#include "level0/level0.h"
#include "level3/model/weights.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Model categories keep their exported order; formatting/listing are rule categories. */
typedef char model_categories_match[(L3_CLASSES == CATEGORY_FORMATTING) ? 1 : -1];

#define TEXT_LIMIT 4096
#define WORD_LIMIT 63

static const char *const names[CATEGORY_COUNT] = {
    "progress", "error", "warning", "info", "result", "stats", "version", "unknown", "formatting", "listing", "diagnostic"
};

const char *level3_category_name(log_category category)
{
    return names[category];
}

bool level3_parse_categories(const char *text, unsigned *mask)
{
    if (strcmp(text, "all") == 0) {
        *mask = LEVEL3_ALL;
        return true;
    }
    unsigned result = 0;
    while (*text) {
        const char *end = strchr(text, ',');
        size_t length = end ? (size_t)(end - text) : strlen(text);
        bool found = false;
        for (size_t i = 0; i < CATEGORY_COUNT; ++i) {
            if (strlen(names[i]) == length && memcmp(text, names[i], length) == 0) {
                result |= 1u << i;
                found = true;
                break;
            }
        }
        if (!found || (end && !end[1]))
            return false;
        if (!end)
            break;
        text = end + 1;
    }
    if (!result)
        return false;
    *mask = result;
    return true;
}

static bool letter(unsigned char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static bool whitespace(unsigned char ch)
{
    return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

static size_t plain_text(sf_line line, char text[TEXT_LIMIT + 1])
{
    size_t length = 0;
    for (size_t i = 0; i < line.length && length < TEXT_LIMIT;) {
        if (line.data[i] == '\x1b' && line.length - i >= 3 && line.data[i + 1] == '[') {
            size_t end = i + 2;
            while (end < line.length && ((line.data[end] >= '0' && line.data[end] <= '9') ||
                   line.data[end] == ';' || line.data[end] == ':'))
                ++end;
            if (end < line.length && line.data[end] == 'm') {
                i = end + 1;
                continue;
            }
        }
        text[length++] = line.data[i++];
    }
    text[length] = '\0';
    return length;
}

static void feature(const char *text, float values[L3_FEATURES])
{
    size_t low = 0, high = L3_FEATURES;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int order = strcmp(text, l3_vocabulary[middle]);
        if (!order) {
            values[middle] = 1;
            return;
        }
        if (order < 0)
            high = middle;
        else
            low = middle + 1;
    }
}

/* Structural evidence independent of the particular package name/version. */
static bool package_version(const char *text, size_t length)
{
    for (size_t at = 1; at < length; ++at) {
        if (text[at] != '@')
            continue;
        size_t start = at;
        while (start && (letter((unsigned char)text[start - 1]) ||
               (text[start - 1] >= '0' && text[start - 1] <= '9') ||
               text[start - 1] == '_' || text[start - 1] == '.' || text[start - 1] == '-'))
            --start;
        if (start == at || !(letter((unsigned char)text[start]) || text[start] == '_'))
            continue;
        size_t version = at + 1;
        while (version < length && strchr("<>=~^", text[version]))
            ++version;
        if (version < length && text[version] >= '0' && text[version] <= '9')
            return true;
    }
    return false;
}

static void measurements(const char *text, size_t length, float values[L3_FEATURES])
{
    for (size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9')
            continue;
        if (i && (letter((unsigned char)text[i - 1]) ||
            (text[i - 1] >= '0' && text[i - 1] <= '9') ||
            strchr("_./$-", text[i - 1])))
            continue;
        size_t end = i;
        while (end < length && text[end] >= '0' && text[end] <= '9')
            ++end;
        if (end + 1 < length && (text[end] == '.' || text[end] == ',') &&
            text[end + 1] >= '0' && text[end + 1] <= '9') {
            ++end;
            while (end < length && text[end] >= '0' && text[end] <= '9')
                ++end;
        }
        while (end < length && (text[end] == ' ' || text[end] == '\t'))
            ++end;
        if (end < length && text[end] == '%')
            feature("@percentage", values);
        char unit[8];
        size_t used = 0;
        while (end < length && letter((unsigned char)text[end]) && used < sizeof(unit) - 1) {
            unsigned char ch = (unsigned char)text[end++];
            unit[used++] = (char)(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch);
        }
        unit[used] = '\0';
        if (end < length && (letter((unsigned char)text[end]) ||
            (text[end] >= '0' && text[end] <= '9') || text[end] == '_'))
            continue;
        const char *units[] = {"b", "byte", "bytes", "kb", "kib", "mb", "mib",
                               "gb", "gib", "tb", "tib"};
        for (size_t u = 0; u < sizeof(units) / sizeof(units[0]); ++u)
            if (strcmp(unit, units[u]) == 0)
                feature("@size", values);
    }
}

static size_t extract(sf_line line, float values[L3_FEATURES])
{
    char text[TEXT_LIMIT + 1];
    size_t length = plain_text(line, text);
    measurements(text, length, values);
    char previous[WORD_LIMIT + 1] = {0};
    for (size_t pos = 0; pos < length;) {
        if (whitespace((unsigned char)text[pos])) {
            ++pos;
            continue;
        }
        size_t end = pos;
        bool path = false;
        while (end < length && !whitespace((unsigned char)text[end])) {
            if (text[end] == '/' || text[end] == '$')
                path = true;
            ++end;
        }
        if (path) {
            pos = end;
            continue;
        }
        if (package_version(text + pos, end - pos))
            feature("@version", values);
        while (pos < end) {
            if (letter((unsigned char)text[pos]) || text[pos] == '_') {
                size_t identifier_end = pos;
                bool identifier = false;
                while (identifier_end < end && (letter((unsigned char)text[identifier_end]) ||
                       (text[identifier_end] >= '0' && text[identifier_end] <= '9') ||
                       text[identifier_end] == '_')) {
                    if (text[identifier_end] == '_')
                        identifier = true;
                    ++identifier_end;
                }
                if (identifier) {
                    pos = identifier_end;
                    continue;
                }
            }
            if (!letter((unsigned char)text[pos])) {
                ++pos;
                continue;
            }
            char word[WORD_LIMIT + 1];
            size_t used = 0;
            bool oversized = false;
            while (pos < end && letter((unsigned char)text[pos])) {
                unsigned char ch = (unsigned char)text[pos++];
                if (used < WORD_LIMIT)
                    word[used++] = (char)(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch);
                else
                    oversized = true;
            }
            word[used] = '\0';
            if (oversized) {
                previous[0] = '\0';
                continue;
            }
            feature(word, values);
            if (previous[0]) {
                char bigram[2 * WORD_LIMIT + 2];
                size_t before = strlen(previous);
                memcpy(bigram, previous, before);
                bigram[before] = ' ';
                memcpy(bigram + before + 1, word, used + 1);
                feature(bigram, values);
            }
            memcpy(previous, word, used + 1);
        }
    }
    float count = 0;
    for (size_t i = 0; i < L3_FEATURES; ++i)
        count += values[i];
    if (count) {
        float norm = sqrtf(count);
        for (size_t i = 0; i < L3_FEATURES; ++i)
            values[i] /= norm;
    }
    return (size_t)count;
}

static void infer(const float values[L3_FEATURES], float scores[CATEGORY_COUNT])
{
    for (size_t c = 0; c < CATEGORY_COUNT; ++c)
        scores[c] = 0;
    float hidden[L3_HIDDEN];
    for (size_t h = 0; h < L3_HIDDEN; ++h) {
        float sum = l3_b1[h];
        for (size_t i = 0; i < L3_FEATURES; ++i)
            sum += values[i] * l3_w1[i * L3_HIDDEN + h];
        hidden[h] = sum > 0 ? sum : 0;
    }
    float maximum = -INFINITY;
    for (size_t c = 0; c < L3_CLASSES; ++c) {
        float sum = l3_b2[c];
        for (size_t h = 0; h < L3_HIDDEN; ++h)
            sum += hidden[h] * l3_w2[h * L3_CLASSES + c];
        scores[c] = sum;
        if (sum > maximum)
            maximum = sum;
    }
    float total = 0;
    for (size_t c = 0; c < L3_CLASSES; ++c) {
        scores[c] = expf(scores[c] - maximum);
        total += scores[c];
    }
    for (size_t c = 0; c < L3_CLASSES; ++c)
        scores[c] /= total;
}

void level3_model_scores(sf_line line, float scores[CATEGORY_COUNT])
{
    float values[L3_FEATURES] = {0};
    extract(line, values);
    infer(values, scores);
}

static size_t content_words(const float features[L3_FEATURES])
{
    size_t count = 0;
    for (size_t i = 0; i < L3_FEATURES; ++i) {
        const char *name = l3_vocabulary[i];
        if (features[i] > 0 && !strchr(name, ' ') && strcmp(name, "info") != 0 &&
            strcmp(name, "verbose") != 0 && strcmp(name, "debug") != 0 &&
            strcmp(name, "trace") != 0)
            ++count;
    }
    return count;
}

typedef struct {
    const char *text;
    size_t length;
} listing_field;

static listing_field next_field(const char **cursor)
{
    while (**cursor && whitespace((unsigned char)**cursor))
        ++*cursor;
    const char *start = *cursor;
    while (**cursor && !whitespace((unsigned char)**cursor))
        ++*cursor;
    return (listing_field){start, (size_t)(*cursor - start)};
}

static bool decimal_field(listing_field field)
{
    if (!field.length)
        return false;
    for (size_t i = 0; i < field.length; ++i)
        if (field.text[i] < '0' || field.text[i] > '9')
            return false;
    return true;
}

static bool listing_size(listing_field field)
{
    size_t i = 0;
    while (i < field.length && field.text[i] >= '0' && field.text[i] <= '9')
        ++i;
    if (!i)
        return false;
    if (i + 1 < field.length && (field.text[i] == '.' || field.text[i] == ',')) {
        ++i;
        size_t fraction = i;
        while (i < field.length && field.text[i] >= '0' && field.text[i] <= '9')
            ++i;
        if (i == fraction)
            return false;
    }
    if (i < field.length && strchr("kKmMgGtTpPeE", field.text[i])) {
        ++i;
        if (i < field.length && field.text[i] == 'B')
            ++i;
    }
    return i == field.length;
}

static bool listing_time(listing_field field)
{
    if (field.length == 4)
        return decimal_field(field); /* ls prints a year for older files. */
    if (field.length != 5 && field.length != 8)
        return false;
    for (size_t i = 0; i < field.length; ++i)
        if ((i == 2 || i == 5) ? field.text[i] != ':' :
            (field.text[i] < '0' || field.text[i] > '9'))
            return false;
    return true;
}

static bool long_listing(const char *text)
{
    listing_field mode = next_field(&text);
    if (mode.length < 10 || mode.length > 11 || !strchr("-dlbcps", mode.text[0]))
        return false;
    for (size_t i = 1; i < 10; ++i) {
        const char *allowed = i % 3 == 1 ? "r-" : i % 3 == 2 ? "w-" :
                              i == 9 ? "x-tT" : "x-sS";
        if (!strchr(allowed, mode.text[i]))
            return false;
    }
    if (mode.length == 11 && !strchr("+.@", mode.text[10]))
        return false;
    if (!decimal_field(next_field(&text))) /* hard links */
        return false;
    if (!next_field(&text).length || !next_field(&text).length) /* owner, group */
        return false;
    listing_field size = next_field(&text);
    if ((mode.text[0] == 'b' || mode.text[0] == 'c') && size.length > 1 &&
        size.text[size.length - 1] == ',') {
        --size.length;
        if (!decimal_field(size) || !decimal_field(next_field(&text)))
            return false;
    } else if (!listing_size(size)) {
        return false;
    }
    listing_field month = next_field(&text);
    if (month.length == 10 && month.text[4] == '-' && month.text[7] == '-') {
        for (size_t i = 0; i < month.length; ++i)
            if (i != 4 && i != 7 && (month.text[i] < '0' || month.text[i] > '9'))
                return false;
    } else {
        if (month.length < 3 || month.length > 16)
            return false;
        for (size_t i = 0; i < month.length; ++i)
            if (!(letter((unsigned char)month.text[i]) || (unsigned char)month.text[i] >= 0x80 ||
                  month.text[i] == '.' || (month.text[i] >= '0' && month.text[i] <= '9')))
                return false;
        listing_field day = next_field(&text);
        if (day.length > 2 || !decimal_field(day))
            return false;
    }
    if (!listing_time(next_field(&text)))
        return false;
    return next_field(&text).length > 0; /* filename; spaces and -> targets allowed */
}

static bool diagnostic_context(const char *text)
{
    while (*text && whitespace((unsigned char)*text))
        ++text;
    const char *cursor = text;
    while (*cursor >= '0' && *cursor <= '9')
        ++cursor;
    bool numbered = cursor != text;
    while (*cursor == ' ' || *cursor == '\t')
        ++cursor;
    if (*cursor == '|' && (numbered || !strchr(cursor + 1, '|')))
        return true; /* Code gutter, source excerpt, or an underline gutter. */
    bool caret = false;
    cursor = text;
    while (*cursor && (strchr("^~+-", *cursor) || whitespace((unsigned char)*cursor))) {
        if (*cursor == '^')
            caret = true;
        ++cursor;
    }
    if (caret && !*cursor)
        return true;
    const char *markers[] = {
        ": In function ", ": In member function ", ": In instantiation of ",
        ": In lambda function", ": At top level:", ": required from ", ": note: "
    };
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i) {
        const char *at = strstr(text, markers[i]);
        if (!at)
            continue;
        for (const char *p = text; p < at; ++p)
            if (*p == '.' || *p == '/' || *p == '\\')
                return true;
    }
    return strncmp(text, "In file included from ", 22) == 0;
}

level3_prediction level3_classify(sf_line line)
{
    level3_prediction prediction = {.category = CATEGORY_UNKNOWN};
    if (line.original) {
        line.data = line.original;
        line.length = line.original_length;
    }
    if (!line.length)
        return prediction;
    char text[TEXT_LIMIT + 1];
    size_t length = 0;
    bool readable = line.length <= TEXT_LIMIT && !memchr(line.data, '\0', line.length);
    if (readable) {
        length = plain_text(line, text);
        /* Filenames may themselves contain strings such as "error:". */
        if (long_listing(text)) {
            prediction.category = CATEGORY_LISTING;
            prediction.scores[CATEGORY_LISTING] = 1;
            return prediction;
        }
        /* Source text can itself contain words such as "warning:". */
        if (diagnostic_context(text)) {
            prediction.category = CATEGORY_DIAGNOSTIC;
            prediction.scores[CATEGORY_DIAGNOSTIC] = 1;
            return prediction;
        }
    }
    log_type severity = level0_classify(line);
    /* An explicit error/warning label takes priority over semantic guesses. */
    if (severity == LOG_ERROR || severity == LOG_WARNING) {
        prediction.category = severity == LOG_ERROR ? CATEGORY_ERROR : CATEGORY_WARNING;
        prediction.scores[prediction.category] = 1;
        return prediction;
    }
    /* Keep diagnostics, metadata, binary data and oversized lines visible. */
    if (!readable)
        return prediction;
    size_t first = 0;
    while (first < length && whitespace((unsigned char)text[first]))
        ++first;
    /* Pure decoration is explicit syntax, independent of the learned model.
     * Do not classify headings, ellipses, or diagnostic carets as decoration. */
    size_t end = length;
    while (end > first && whitespace((unsigned char)text[end - 1]))
        --end;
    if (end - first >= 4 && strchr("=-*", text[first])) {
        size_t at = first;
        while (at < end && text[at] == text[first])
            ++at;
        if (at == end) {
            prediction.category = CATEGORY_FORMATTING;
            prediction.scores[CATEGORY_FORMATTING] = 1;
            return prediction;
        }
    }
    if (first == length || text[first] == '^' || text[first] == '~' ||
        memchr(text, '|', length) || strstr(text, "[repeat]") ||
        strstr(text, "[paths]") || (text[first] == '$' && strstr(text, " = ")))
        return prediction;
    /* An explicit INFO tag is useful when semantic evidence is insufficient;
     * confident semantic progress/result predictions may still refine it. */
    if (severity == LOG_INFO)
        prediction.category = CATEGORY_INFO;
    float features[L3_FEATURES] = {0};
    extract(line, features);
    if (content_words(features) < 2)
        return prediction;
    infer(features, prediction.scores);
    size_t best = 0;
    float second = 0;
    for (size_t i = 1; i < CATEGORY_COUNT; ++i)
        if (prediction.scores[i] > prediction.scores[best])
            best = i;
    for (size_t i = 0; i < CATEGORY_COUNT; ++i)
        if (i != best && prediction.scores[i] > second)
            second = prediction.scores[i];
    if (best != CATEGORY_UNKNOWN && prediction.scores[best] >= L3_CONFIDENCE &&
        prediction.scores[best] - second >= L3_MARGIN)
        prediction.category = (log_category)best;
    return prediction;
}

static const char *category_color(log_category category)
{
    static const char *const colors[CATEGORY_COUNT] = {
        "\x1b[0;36m", "\x1b[0;1;31m", "\x1b[0;1;33m", "\x1b[0;37m",
        "\x1b[0;1;32m", "\x1b[0;35m", "\x1b[0;36m", "\x1b[0;37m", "\x1b[0;2;37m", "\x1b[0;34m", "\x1b[0;2;37m"
    };
    return colors[category];
}

/* Remove only complete SGR style/color sequences. Preserve cursor controls
 * and every other byte, while giving the category color the whole line. */
static size_t copy_without_sgr(char *destination, sf_line line)
{
    size_t written = 0;
    for (size_t i = 0; i < line.length;) {
        if (line.data[i] == '\x1b' && line.length - i >= 3 && line.data[i + 1] == '[') {
            size_t end = i + 2;
            while (end < line.length && ((line.data[end] >= '0' && line.data[end] <= '9') ||
                   line.data[end] == ';' || line.data[end] == ':'))
                ++end;
            if (end < line.length && line.data[end] == 'm') {
                i = end + 1;
                continue;
            }
        }
        destination[written++] = line.data[i++];
    }
    return written;
}

static bool filter(void *context, const void *config, sf_line line, sf_output *output)
{
    level3_context *state = context;
    const level3_config *options = config;
    if (!options->enabled)
        return true;
    level3_prediction result = level3_classify(line);
    ++state->counts[result.category];
    /* Keep unknown data and compiler context visible under every selection. */
    if (result.category != CATEGORY_UNKNOWN && result.category != CATEGORY_DIAGNOSTIC && !(options->show & (1u << result.category))) {
        ++state->lines_dropped;
        return false;
    }
    if (!options->annotate && !options->color)
        return true;
    const char *name = level3_category_name(result.category);
    size_t name_length = strlen(name);
    size_t prefix = 0;
    for (size_t i = 0; options->annotate && i < CATEGORY_COUNT; ++i) {
        size_t width = strlen(names[i]) + 3; /* Brackets plus trailing space. */
        if (width > prefix)
            prefix = width;
    }
    const char *color = options->color ? category_color(result.category) : "";
    size_t color_length = strlen(color);
    size_t overhead = prefix + color_length + (options->color ? 4 : 0);
    if (line.length > SIZE_MAX - overhead) {
        state->allocation_failed = true;
        sf_output_fail(output);
        return false;
    }
    size_t needed = overhead + line.length;
    if (needed > state->capacity) {
        char *buffer = realloc(state->rendered, needed);
        if (!buffer) {
            state->allocation_failed = true;
            sf_output_fail(output);
            return false;
        }
        state->rendered = buffer;
        state->capacity = needed;
    }
    size_t written = 0;
    if (color_length) {
        memcpy(state->rendered, color, color_length);
        written = color_length;
    }
    if (options->annotate) {
        memset(state->rendered + written, ' ', prefix);
        state->rendered[written] = '[';
        memcpy(state->rendered + written + 1, name, name_length);
        state->rendered[written + name_length + 1] = ']';
        written += prefix;
    }
    if (options->color) {
        written += copy_without_sgr(state->rendered + written, line);
        size_t end = written;
        while (end > color_length + prefix &&
               (state->rendered[end - 1] == '\n' || state->rendered[end - 1] == '\r'))
            --end;
        memmove(state->rendered + end + 4, state->rendered + end, written - end);
        memcpy(state->rendered + end, "\x1b[0m", 4);
        written += 4;
    } else if (line.length) {
        memcpy(state->rendered + written, line.data, line.length);
        written += line.length;
    }
    sf_emit(output, sf_line_rewrite(line, state->rendered, written));
    return false;
}

sf_level level3_create(level3_context *context, const level3_config *config)
{
    return (sf_level){context, config, filter, NULL};
}

void level3_destroy(level3_context *context)
{
    free(context->rendered);
    *context = (level3_context){0};
}
