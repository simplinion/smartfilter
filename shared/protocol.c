#include "protocol.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define streq(a, b) (strcmp((a), (b)) == 0)

// Helper: trim leading/trailing whitespace
static const char *trim(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    const char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    return s;
}

// Helper: copy trimmed string, caller must free
static char *trimdup(const char *s, size_t len) {
    const char *p = s;
    const char *end = s + len;
    while (p < end && isspace((unsigned char)*p)) p++;
    while (end > p && isspace((unsigned char)*(end - 1))) end--;
    size_t n = end - p;
    char *r = malloc(n + 1);
    memcpy(r, p, n);
    r[n] = '\0';
    return r;
}

const char *proto_parse_cmd(const char *input, ProtocolCmd *out) {
    if (!input || !out) return NULL;
    
    memset(out, 0, sizeof(*out));
    const char *colon = strchr(input, ':');
    const char *cmd_name;
    const char *arg;
    
    if (colon) {
        size_t name_len = colon - input;
        char name[64];
        if (name_len >= sizeof(name)) return "command name too long";
        memcpy(name, input, name_len);
        name[name_len] = '\0';
        cmd_name = trim(name);
        arg = trim(colon + 1);
    } else {
        cmd_name = trim(input);
        arg = "";
    }
    
    if (streq(cmd_name, "NOP") || streq(cmd_name, "")) {
        out->type = CMD_NOP;
    } else if (streq(cmd_name, "SET_MODE") || streq(cmd_name, "MODE")) {
        out->type = CMD_SET_MODE;
        if (streq(arg, "AGGRESSIVE") || streq(arg, "aggressive") || streq(arg, "1"))
            out->arg_int = MODE_AGGRESSIVE;
        else if (streq(arg, "PERMISSIVE") || streq(arg, "permissive") || streq(arg, "2"))
            out->arg_int = MODE_PERMISSIVE;
        else if (streq(arg, "CUSTOM") || streq(arg, "custom") || streq(arg, "0"))
            out->arg_int = MODE_CUSTOM;
        else
            return "invalid mode";
    } else if (streq(cmd_name, "ADD_WL") || streq(cmd_name, "WHITELIST") || streq(cmd_name, "+WL")) {
        out->type = CMD_ADD_WL;
        out->arg_str = strdup(arg);
    } else if (streq(cmd_name, "ADD_BL") || streq(cmd_name, "BLACKLIST") || streq(cmd_name, "+BL")) {
        out->type = CMD_ADD_BL;
        out->arg_str = strdup(arg);
    } else if (streq(cmd_name, "SKIP_REPEAT") || streq(cmd_name, "SKIP_RPT")) {
        out->type = CMD_SKIP_REPEAT;
        out->arg_int = atoi(arg);
        if (out->arg_int <= 0) out->arg_int = 3;
    } else if (streq(cmd_name, "COLLAPSE_REPEAT") || streq(cmd_name, "COLLAPSE")) {
        out->type = CMD_COLLAPSE_REPEAT;
        out->arg_int = (streq(arg, "1") || streq(arg, "on") || streq(arg, "yes") || streq(arg, "")) ? 1 : 0;
    } else if (streq(cmd_name, "MAX_LINE_LEN") || streq(cmd_name, "MAXLEN")) {
        out->type = CMD_MAX_LINE_LEN;
        out->arg_int = atoi(arg);
        if (out->arg_int <= 0) out->arg_int = 200;
    } else if (streq(cmd_name, "SUMMARIZE") || streq(cmd_name, "SUMMARY")) {
        out->type = CMD_SUMMARIZE;
        out->arg_int = (streq(arg, "1") || streq(arg, "on") || streq(arg, "yes") || streq(arg, "")) ? 1 : 0;
    } else if (streq(cmd_name, "STREAM_PASS") || streq(cmd_name, "PASS")) {
        out->type = CMD_STREAM_PASS;
        out->arg_int = atoi(arg);
        if (out->arg_int <= 0) out->arg_int = 1;
    } else if (streq(cmd_name, "CTX_WINDOW") || streq(cmd_name, "CTX")) {
        out->type = CMD_CTX_WINDOW;
        out->arg_int = atoi(arg);
        if (out->arg_int <= 0) out->arg_int = 10;
    } else if (streq(cmd_name, "RESET")) {
        out->type = CMD_RESET;
    } else if (streq(cmd_name, "LOG_LEVEL") || streq(cmd_name, "LEVEL")) {
        out->type = CMD_LOG_LEVEL;
        // Map string to LineType
        if (streq(arg, "ERROR") || streq(arg, "error") || streq(arg, "err"))
            out->arg_int = LINE_ERROR;
        else if (streq(arg, "WARNING") || streq(arg, "warning") || streq(arg, "warn") || streq(arg, "warn"))
            out->arg_int = LINE_WARNING;
        else if (streq(arg, "INFO") || streq(arg, "info"))
            out->arg_int = LINE_INFO;
        else if (streq(arg, "DEBUG") || streq(arg, "debug"))
            out->arg_int = LINE_DEBUG;
        else if (streq(arg, "ALL") || streq(arg, "all"))
            out->arg_int = LINE_UNKNOWN;
        else
            out->arg_int = LINE_ERROR;
    } else if (streq(cmd_name, "DONE")) {
        out->type = CMD_DONE;
    } else {
        return "unknown command";
    }
    
    return NULL; // success
}

int proto_parse_multi(const char *input, ProtocolCmd *out, int max_out) {
    if (!input || !out || max_out <= 0) return -1;
    
    int count = 0;
    const char *p = input;
    
    while (*p && count < max_out) {
        // Skip separators
        while (*p && (*p == ';' || *p == ',' || *p == '\n' || *p == '|')) p++;
        if (!*p) break;
        
        // Find end of this command
        const char *start = p;
        while (*p && *p != ';' && *p != ',' && *p != '\n' && *p != '|') p++;
        
        size_t len = p - start;
        char buf[MAX_CMD_LEN];
        if (len >= MAX_CMD_LEN) return -1;
        memcpy(buf, start, len);
        buf[len] = '\0';
        
        const char *err = proto_parse_cmd(buf, &out[count]);
        if (err) return -1;
        
        count++;
    }
    
    return count;
}

const char *proto_serialize_cmd(const ProtocolCmd *cmd) {
    static char buf[MAX_CMD_LEN];
    
    switch (cmd->type) {
        case CMD_NOP:         snprintf(buf, sizeof(buf), "NOP"); break;
        case CMD_SET_MODE: {
            const char *m = "CUSTOM";
            if (cmd->arg_int == MODE_AGGRESSIVE) m = "AGGRESSIVE";
            else if (cmd->arg_int == MODE_PERMISSIVE) m = "PERMISSIVE";
            snprintf(buf, sizeof(buf), "SET_MODE:%s", m);
            break;
        }
        case CMD_ADD_WL:      snprintf(buf, sizeof(buf), "ADD_WL:%s", cmd->arg_str ? cmd->arg_str : ""); break;
        case CMD_ADD_BL:      snprintf(buf, sizeof(buf), "ADD_BL:%s", cmd->arg_str ? cmd->arg_str : ""); break;
        case CMD_SKIP_REPEAT: snprintf(buf, sizeof(buf), "SKIP_REPEAT:%d", cmd->arg_int); break;
        case CMD_COLLAPSE_REPEAT: snprintf(buf, sizeof(buf), "COLLAPSE_REPEAT:%d", cmd->arg_int); break;
        case CMD_MAX_LINE_LEN: snprintf(buf, sizeof(buf), "MAX_LINE_LEN:%d", cmd->arg_int); break;
        case CMD_SUMMARIZE:   snprintf(buf, sizeof(buf), "SUMMARIZE:%d", cmd->arg_int); break;
        case CMD_STREAM_PASS: snprintf(buf, sizeof(buf), "STREAM_PASS:%d", cmd->arg_int); break;
        case CMD_CTX_WINDOW:  snprintf(buf, sizeof(buf), "CTX_WINDOW:%d", cmd->arg_int); break;
        case CMD_RESET:       snprintf(buf, sizeof(buf), "RESET"); break;
        case CMD_LOG_LEVEL:   snprintf(buf, sizeof(buf), "LOG_LEVEL:%d", cmd->arg_int); break;
        case CMD_DONE:        snprintf(buf, sizeof(buf), "DONE"); break;
        default:              snprintf(buf, sizeof(buf), "UNKNOWN:%d", cmd->type); break;
    }
    
    return buf;
}

void proto_free_cmds(ProtocolCmd *cmds, int n) {
    if (!cmds) return;
    for (int i = 0; i < n; i++) {
        free(cmds[i].arg_str);
        cmds[i].arg_str = NULL;
    }
}

int proto_conflicts(const ProtocolCmd *a, const ProtocolCmd *b) {
    if (!a || !b) return 0;
    return a->type == b->type;
}

void proto_merge_cmds(ProtocolCmd *dst, const ProtocolCmd *a, const ProtocolCmd *b) {
    if (!dst || !a) return;
    if (b) {
        // Keep the more permissive setting
        switch (a->type) {
            case CMD_SET_MODE:
                // Keep permissive
                dst->arg_int = (a->arg_int < b->arg_int) ? b->arg_int : a->arg_int;
                break;
            case CMD_SKIP_REPEAT:
                dst->arg_int = (a->arg_int > b->arg_int) ? a->arg_int : b->arg_int;
                break;
            case CMD_MAX_LINE_LEN:
                dst->arg_int = (a->arg_int > b->arg_int) ? a->arg_int : b->arg_int;
                break;
            default:
                // For additive things, keep both? Actually just keep a for now
                break;
        }
    }
    dst->type = a->type;
}
