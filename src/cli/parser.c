// In-place argv parser. See parser.h for grammar.
#include "cli/parser.h"

#include <stddef.h>
#include <stdbool.h>

static inline bool is_ws(char c) { return c == ' ' || c == '\t'; }

int cli_parse(char *line, char **argv, int max_argv) {
    if (!line || !argv || max_argv <= 0) return 0;

    int argc = 0;
    char *src = line;   // read cursor
    char *dst = line;   // write cursor (may overwrite src as we consume escapes)

    while (*src) {
        while (is_ws(*src)) src++;
        if (*src == '\0') break;

        if (argc >= max_argv) return CLI_PARSE_ERR_TOO_MANY_ARGS;

        // Start of a token. Its final storage begins at dst.
        argv[argc++] = dst;

        bool in_quote = false;
        while (*src) {
            char c = *src;
            if (in_quote) {
                if (c == '\\' && (src[1] == '"' || src[1] == '\\')) {
                    // Escape: emit the second character verbatim.
                    src++;
                    *dst++ = *src++;
                    continue;
                }
                if (c == '"') {
                    in_quote = false;
                    src++;
                    continue;
                }
                *dst++ = c;
                src++;
            } else {
                if (is_ws(c)) break;
                if (c == '"') {
                    in_quote = true;
                    src++;
                    continue;
                }
                *dst++ = c;
                src++;
            }
        }

        if (in_quote) return CLI_PARSE_ERR_UNCLOSED_QUOTE;

        // Terminate this token. If we've eaten characters via escape or
        // quote-stripping, src is ahead of dst — we can safely advance src.
        *dst++ = '\0';
    }

    return argc;
}
