// Argv-style parser for the CLI.
//
// The input line is tokenized in-place: NUL terminators are punched into
// the buffer and argv[] receives pointers back into it. That means the
// caller must own the buffer for the lifetime of argv access, and the
// buffer is destructively rewritten.
//
// Rules:
//   - tokens are separated by any run of ASCII whitespace (space, tab).
//   - a double-quoted section ("...") is a single token and preserves
//     the whitespace and other quotes inside it.
//   - a backslash inside quotes escapes the next character (supports
//     \\ and \").
//   - leading and trailing whitespace are ignored.
#ifndef PICOBLE_CLI_PARSER_H
#define PICOBLE_CLI_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#define CLI_MAX_ARGS 16

// Return values:
//   >= 0 : argc (number of tokens written to argv, may be 0 for empty line)
//   CLI_PARSE_ERR_UNCLOSED_QUOTE : quoted string was never closed
//   CLI_PARSE_ERR_TOO_MANY_ARGS  : more than max_argv tokens on the line
#define CLI_PARSE_ERR_UNCLOSED_QUOTE (-1)
#define CLI_PARSE_ERR_TOO_MANY_ARGS  (-2)

int cli_parse(char *line, char **argv, int max_argv);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_CLI_PARSER_H
