#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "types.h"
#include <stddef.h>

// Protocol command types for inter-level communication
typedef enum {
    CMD_NOP = 0,
    CMD_SET_MODE,          // arg: Mode (AGGRESSIVE, PERMISSIVE, CUSTOM)
    CMD_ADD_WL,            // arg: regex string (whitelist - only show matching)
    CMD_ADD_BL,            // arg: regex string (blacklist - hide matching)
    CMD_SKIP_REPEAT,       // arg: count (skip if line repeated N times)
    CMD_COLLAPSE_REPEAT,   // arg: 1=on, 0=off
    CMD_MAX_LINE_LEN,      // arg: max length before truncation
    CMD_SUMMARIZE,         // arg: 1=on, 0=off (show summary instead of details)
    CMD_STREAM_PASS,       // arg: count (pass next N lines unfiltered)
    CMD_CTX_WINDOW,        // arg: size (rolling window size for context)
    CMD_RESET,             // no args - reset all settings
    CMD_LOG_LEVEL,         // arg: LineType (minimum level to show)
    CMD_DONE,              // signal end of commands
} ProtocolCmdType;

// A single protocol command with optional string argument
typedef struct {
    ProtocolCmdType type;
    int             arg_int;       // integer argument
    char           *arg_str;       // string argument (owned by caller)
} ProtocolCmd;

// Parse command string like "ADD_BL:.*Compiling.*" into ProtocolCmd
// Returns NULL on error, caller must NOT free returned cmd
const char *proto_parse_cmd(const char *input, ProtocolCmd *out);

// Serialize ProtocolCmd back to string (for debugging/testing)
// Returns static buffer, not thread-safe
const char *proto_serialize_cmd(const ProtocolCmd *cmd);

// Parse a multi-command string like "ADD_BL:.*foo;ADD_WL:error;MODE:aggressive"
// Returns number of commands parsed, -1 on error
int proto_parse_multi(const char *input, ProtocolCmd *out, int max_out);

// Free string arguments in an array of commands
void proto_free_cmds(ProtocolCmd *cmds, int n);

// Check if two commands conflict (same type)
int proto_conflicts(const ProtocolCmd *a, const ProtocolCmd *b);

// Merge conflicting commands (keeps the more permissive one)
void proto_merge_cmds(ProtocolCmd *dst, const ProtocolCmd *a, const ProtocolCmd *b);

// Protocol version for compatibility
#define PROTOCOL_VERSION 1

// Maximum command string length
#define MAX_CMD_LEN 256

// Maximum number of commands in a batch
#define MAX_CMDS 64

#endif // PROTOCOL_H
