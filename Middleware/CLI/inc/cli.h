#ifndef CLI_H
#define CLI_H

#include <stddef.h>

#define CLI_MAX_LINE_LENGTH 128U
#define CLI_MAX_ARGUMENTS   16U

typedef enum
{
    CLI_STATUS_OK = 0,
    CLI_STATUS_NULL_POINTER,
    CLI_STATUS_EMPTY_LINE,
    CLI_STATUS_UNKNOWN_COMMAND,
    CLI_STATUS_INVALID_ARGUMENTS,
    CLI_STATUS_OUTPUT_TOO_SMALL,
    CLI_STATUS_LINE_TOO_LONG
} cli_status_t;

typedef cli_status_t (*cli_command_handler_t)(int argc,
                                              const char *argv[],
                                              char *output,
                                              size_t output_size);

typedef struct
{
    const char *name;
    cli_command_handler_t handler;
} cli_command_t;

cli_status_t cli_execute_line(const cli_command_t *commands,
                              size_t command_count,
                              const char *line,
                              char *output,
                              size_t output_size);

#endif /* CLI_H */
