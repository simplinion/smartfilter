#include "filter.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <regex.h>

// Default configuration
static const FilterConfig default_config = {
    .mode              = MODE_PERMISSIVE,
    .min_level         = LINE_UNKNOWN,  // show all by default
    .max_line_len      = 200,
    .collapse_threshold = 3,
    .collapse_enabled  = true,
    .summarize         = false,
    .stream_pass       = 0,
    .custom_wl         = NULL,
    .custom_bl         = NULL,
};

// Static buffers and state for collapse tracking
static char collapse_pending_line[512] = {0};
static int collapse_pending_count = 0;
static int collapse_threshold = 3;

// Static buffer for processed output
static char output_buf[1024];

Filter *filter_create(void) {
    Filter *f = calloc(1, sizeof(Filter));
    if (!f) return NULL;
    
    f->config = default_config;
    f->ctx = ctx_create(32);
    f->collapse = collapse_create();
    
    collapse_threshold = f->config.collapse_threshold;
    collapse_pending_line[0] = '\0';
    collapse_pending_count = 0;
    
    if (!f->ctx || !f->collapse) {
        if (f->ctx) ctx_destroy(f->ctx);
        if (f->collapse) collapse_destroy(f->collapse);
        free(f);
        return NULL;
    }
    
    f->total_in = 0;
    f->total_out = 0;
    f->skipped = 0;
    f->collapsed = 0;
    f->stream_pass_remaining = 0;
    
    return f;
}

Filter *filter_create_from_cmds(const ProtocolCmd *cmds, int n_cmds) {
    Filter *f = filter_create();
    if (!f) return NULL;
    
    filter_apply_cmds(f, cmds, n_cmds);
    return f;
}

void filter_destroy(Filter *f) {
    if (!f) return;
    
    // Flush any pending collapse at end
    if (collapse_pending_line[0] != '\0' && collapse_pending_count > 0) {
        if (collapse_pending_count >= f->config.collapse_threshold) {
            char summary_buf[128];
            snprintf(summary_buf, sizeof(summary_buf), 
                     "  \033[90m[%dx: %.40s%s]\033[0m\n", 
                     collapse_pending_count, 
                     collapse_pending_line,
                     strlen(collapse_pending_line) > 40 ? "..." : "");
            fwrite(summary_buf, 1, strlen(summary_buf), stdout);
        }
        collapse_pending_line[0] = '\0';
        collapse_pending_count = 0;
    }
    
    ctx_destroy(f->ctx);
    collapse_destroy(f->collapse);
    free(f->config.custom_wl);
    free(f->config.custom_bl);
    free(f);
}

static int apply_config_cmd(Filter *f, const ProtocolCmd *cmd) {
    if (!f || !cmd) return -1;
    
    switch (cmd->type) {
        case CMD_SET_MODE:
            f->config.mode = (Mode)cmd->arg_int;
            break;
            
        case CMD_ADD_WL:
            free(f->config.custom_wl);
            f->config.custom_wl = cmd->arg_str ? strdup(cmd->arg_str) : NULL;
            break;
            
        case CMD_ADD_BL:
            free(f->config.custom_bl);
            f->config.custom_bl = cmd->arg_str ? strdup(cmd->arg_str) : NULL;
            break;
            
        case CMD_SKIP_REPEAT:
            f->config.collapse_threshold = cmd->arg_int;
            collapse_threshold = cmd->arg_int;
            break;
            
        case CMD_COLLAPSE_REPEAT:
            f->config.collapse_enabled = cmd->arg_int;
            collapse_set_enabled(f->collapse, cmd->arg_int);
            break;
            
        case CMD_MAX_LINE_LEN:
            f->config.max_line_len = cmd->arg_int;
            break;
            
        case CMD_SUMMARIZE:
            f->config.summarize = cmd->arg_int;
            break;
            
        case CMD_STREAM_PASS:
            f->stream_pass_remaining = cmd->arg_int;
            break;
            
        case CMD_CTX_WINDOW:
            // Recreate context with new size
            ctx_destroy(f->ctx);
            f->ctx = ctx_create(cmd->arg_int);
            break;
            
        case CMD_RESET:
            filter_reset(f);
            break;
            
        case CMD_LOG_LEVEL:
            f->config.min_level = (LineType)cmd->arg_int;
            break;
            
        default:
            break;
    }
    
    return 0;
}

int filter_apply_cmd(Filter *f, const ProtocolCmd *cmd) {
    return apply_config_cmd(f, cmd);
}

int filter_apply_cmds(Filter *f, const ProtocolCmd *cmds, int n) {
    if (!f || !cmds) return -1;
    
    int applied = 0;
    for (int i = 0; i < n; i++) {
        if (cmds[i].type != CMD_NOP && cmds[i].type != CMD_DONE) {
            if (apply_config_cmd(f, &cmds[i]) == 0) {
                applied++;
            }
        }
    }
    
    return applied;
}

void filter_reset(Filter *f) {
    if (!f) return;
    
    f->config = default_config;
    ctx_reset(f->ctx);
    collapse_reset(f->collapse);
    f->total_in = 0;
    f->total_out = 0;
    f->skipped = 0;
    f->collapsed = 0;
    f->stream_pass_remaining = 0;
    collapse_pending_line[0] = '\0';
    collapse_pending_count = 0;
}

static bool should_show_line(Filter *f, LineType type, int score) {
    // Stream pass - show everything
    if (f->stream_pass_remaining > 0) {
        f->stream_pass_remaining--;
        return true;
    }
    
    // Always show errors
    if (type == LINE_ERROR) return true;
    
    // Mode-based filtering
    switch (f->config.mode) {
        case MODE_AGGRESSIVE:
            // Show only errors, warnings, success
            return type == LINE_WARNING || type == LINE_SUCCESS || 
                   type == LINE_UNKNOWN || score >= 60;
            
        case MODE_PERMISSIVE:
            // Show everything except obvious noise
            return type != LINE_NOISE || score >= 10;
            
        case MODE_CUSTOM:
            // Use min_level
            return type >= f->config.min_level;
            
        default:
            return true;
    }
}

static void truncate_line(const char *src, char *dst, size_t max, bool is_error) {
    size_t len = strlen(src);
    
    // Errors get more space
    size_t limit = is_error ? max * 2 : max;
    if (len > limit) {
        memcpy(dst, src, limit - 3);
        memcpy(dst + limit - 3, "...", 4);
    } else {
        strcpy(dst, src);
    }
}

const char *filter_process_ex(Filter *f, const char *line, bool *out_shown) {
    if (!f || !line) {
        if (out_shown) *out_shown = false;
        return "";
    }
    
    f->total_in++;
    output_buf[0] = '\0';
    
    // Handle stream pass
    if (f->stream_pass_remaining > 0) {
        f->stream_pass_remaining--;
        f->total_out++;
        if (out_shown) *out_shown = true;
        truncate_line(line, output_buf, sizeof(output_buf), false);
        return output_buf;
    }
    
    // Classify line
    Classification c = classify(line);
    LineType type = c.type;
    int score = c.score;
    
    // Check if this line matches the pending collapse
    if (f->config.collapse_enabled && 
        type != LINE_ERROR && 
        type != LINE_WARNING &&
        collapse_pending_line[0] != '\0' &&
        strcmp(line, collapse_pending_line) == 0) {
        
        // Same line again - increment and hide
        collapse_pending_count++;
        f->skipped++;
        
        // If we've hit threshold, output summary and keep hiding
        if (collapse_pending_count >= f->config.collapse_threshold) {
            if (out_shown) *out_shown = false;
            return "";
        }
        
        // First few repetitions - show them
        f->total_out++;
        if (out_shown) *out_shown = true;
        return line;
    }
    
    // New line - flush any pending collapse first
    if (collapse_pending_line[0] != '\0' && collapse_pending_count > 0) {
        // We had a collapsed sequence - output summary if there were enough
        if (collapse_pending_count >= f->config.collapse_threshold) {
            // Print summary directly (not to output_buf, which is for current line)
            char summary_buf[128];
            snprintf(summary_buf, sizeof(summary_buf), 
                     "  \033[90m[%dx: %.40s%s]\033[0m\n", 
                     collapse_pending_count, 
                     collapse_pending_line,
                     strlen(collapse_pending_line) > 40 ? "..." : "");
            fwrite(summary_buf, 1, strlen(summary_buf), stdout);
            f->total_out++;
        }
        // Clear pending and continue to process current line
        collapse_pending_line[0] = '\0';
        collapse_pending_count = 0;
    }
    
    // Update context (after flush check)
    ctx_add(f->ctx, line, type, score);
    
    // Check if should show
    bool shown = should_show_line(f, type, score);
    
    // Check custom patterns
    if (shown && f->config.custom_bl) {
        regex_t re;
        if (regcomp(&re, f->config.custom_bl, REG_EXTENDED) == 0) {
            if (regexec(&re, line, 0, NULL, 0) == 0) {
                shown = false;
                f->skipped++;
            }
            regfree(&re);
        }
    }
    
    // Custom whitelist overrides
    if (!shown && f->config.custom_wl) {
        regex_t re;
        if (regcomp(&re, f->config.custom_wl, REG_EXTENDED) == 0) {
            if (regexec(&re, line, 0, NULL, 0) == 0) {
                shown = true;
            }
            regfree(&re);
        }
    }
    
    // Truncate if needed
    if (shown) {
        f->total_out++;
        truncate_line(line, output_buf, f->config.max_line_len, type == LINE_ERROR);
        
        // Start tracking for collapse if this is collapseable type
        if (f->config.collapse_enabled && 
            type != LINE_ERROR && 
            type != LINE_WARNING &&
            type != LINE_NOISE) {
            strncpy(collapse_pending_line, line, sizeof(collapse_pending_line) - 1);
            collapse_pending_line[sizeof(collapse_pending_line) - 1] = '\0';
            collapse_pending_count = 0;  // This is first occurrence
        }
    } else {
        f->skipped++;
    }
    
    if (out_shown) *out_shown = shown;
    return output_buf;
}

bool filter_process(Filter *f, const char *line, bool *out_shown) {
    filter_process_ex(f, line, out_shown);
    return out_shown ? *out_shown : false;
}

int filter_total_in(const Filter *f) { return f ? f->total_in : 0; }
int filter_total_out(const Filter *f) { return f ? f->total_out : 0; }
int filter_skipped(const Filter *f) { return f ? f->skipped : 0; }
int filter_collapsed(const Filter *f) { return f ? f->collapsed : 0; }
const FilterConfig *filter_config(const Filter *f) { return f ? &f->config : NULL; }

void filter_set_config(Filter *f, const FilterConfig *cfg) {
    if (f && cfg) f->config = *cfg;
}

void filter_print_summary(const Filter *f) {
    if (!f) return;
    
    // Flush any pending collapse first
    if (collapse_pending_line[0] != '\0' && collapse_pending_count > 0) {
        if (collapse_pending_count >= collapse_threshold) {
            char summary_buf[128];
            snprintf(summary_buf, sizeof(summary_buf), 
                     "  \033[90m[%dx: %.40s%s]\033[0m", 
                     collapse_pending_count, 
                     collapse_pending_line,
                     strlen(collapse_pending_line) > 40 ? "..." : "");
            fprintf(stderr, "%s\n", summary_buf);
        }
        collapse_pending_line[0] = '\0';
        collapse_pending_count = 0;
    }
    
    fprintf(stderr, "\n\033[1mSmartFilter Summary\033[0m\n");
    fprintf(stderr, "  Lines in:     %d\n", f->total_in);
    fprintf(stderr, "  Lines out:    %d\n", f->total_out);
    fprintf(stderr, "  Skipped:      %d (%.1f%%)\n", 
            f->skipped, 
            f->total_in > 0 ? 100.0 * f->skipped / f->total_in : 0);
    fprintf(stderr, "  Errors:       %d\n", ctx_error_count(f->ctx));
    fprintf(stderr, "  Warnings:     %d\n", ctx_warning_count(f->ctx));
}
