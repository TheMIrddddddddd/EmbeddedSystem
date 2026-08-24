#include <ctype.h>
#include <string.h>

#include "cli.h"

static cli_status_t cli_copy_line(const char *line,
                                  char *line_copy)
{
    size_t line_length;

    line_length = strlen(line);
    if (line_length > CLI_MAX_LINE_LENGTH)
    {
        return CLI_STATUS_LINE_TOO_LONG;
    }

    memcpy(line_copy, line, line_length + 1U);
    return CLI_STATUS_OK;
}

static const cli_command_t *cli_find_command(const cli_command_t *commands,
                                             size_t command_count,
                                             const char *name)
{
    size_t index;

    for (index = 0U; index < command_count; index++)
    {
        if ((commands[index].name != NULL) &&
            (commands[index].handler != NULL) &&
            (strcmp(commands[index].name, name) == 0))
        {
            return &commands[index];
        }
    }

    return NULL;
}

cli_status_t cli_execute_line(const cli_command_t *commands,
                              size_t command_count,
                              const char *line,
                              char *output,
                              size_t output_size)
{
    char line_copy[CLI_MAX_LINE_LENGTH + 1U];
    const char *argv[CLI_MAX_ARGUMENTS];
    const cli_command_t *command;
    char *cursor;
    size_t argc = 0U;
    cli_status_t status;

    if ((commands == NULL) || (line == NULL) || (output == NULL))
    {
        return CLI_STATUS_NULL_POINTER;
    }

    status = cli_copy_line(line, line_copy);
    if (status != CLI_STATUS_OK)
    {
        return status;
    }

    cursor = line_copy;
    while (*cursor != '\0')
    {
        while ((*cursor != '\0') &&
               (isspace((unsigned char)*cursor) != 0))
        {
            cursor++;
        }

        if (*cursor == '\0')
        {
            break;
        }

        if (argc >= CLI_MAX_ARGUMENTS)
        {
            return CLI_STATUS_INVALID_ARGUMENTS;
        }

        argv[argc] = cursor;
        argc++;

        while ((*cursor != '\0') &&
               (isspace((unsigned char)*cursor) == 0))
        {
            cursor++;
        }

        if (*cursor != '\0')
        {
            *cursor = '\0';
            cursor++;
        }
    }

    if (argc == 0U)
    {
        return CLI_STATUS_EMPTY_LINE;
    }

    command = cli_find_command(commands, command_count, argv[0]);
    if (command == NULL)
    {
        return CLI_STATUS_UNKNOWN_COMMAND;
    }

    return command->handler((int)argc, argv, output, output_size);
}
