/* PC tests for the CLI command parser and dispatcher. */

#include "unity.h"
#include "cli.h"
#include <string.h>

static cli_status_t ping_handler(int argc,
                                 const char *argv[],
                                 char *output,
                                 size_t output_size)
{
    (void)argv;

    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }
    if (output_size < 5U)
    {
        return CLI_STATUS_OUTPUT_TOO_SMALL;
    }

    memcpy(output, "pong", 5U);
    return CLI_STATUS_OK;
}

static cli_status_t add_handler(int argc,
                                const char *argv[],
                                char *output,
                                size_t output_size)
{
    (void)output;
    (void)output_size;

    if ((argc != 3) || (strcmp(argv[1], "2") != 0) ||
        (strcmp(argv[2], "3") != 0))
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    return CLI_STATUS_OK;
}

static const cli_command_t commands[] = {
    {"ping", ping_handler},
    {"add", add_handler}
};

void test_cli_dispatches_single_command(void)
{
    char output[8] = {0};

    TEST_ASSERT_EQUAL(CLI_STATUS_OK,
                      cli_execute_line(commands, 2U, "ping", output,
                                       sizeof(output)));
    TEST_ASSERT_EQUAL_STRING("pong", output);
}

void test_cli_splits_arguments_and_ignores_extra_spaces(void)
{
    char output[8] = {0};

    TEST_ASSERT_EQUAL(CLI_STATUS_OK,
                      cli_execute_line(commands, 2U,
                                       "  add   2  3  ", output,
                                       sizeof(output)));
}

void test_cli_rejects_empty_and_unknown_commands(void)
{
    char output[8] = {0};

    TEST_ASSERT_EQUAL(CLI_STATUS_EMPTY_LINE,
                      cli_execute_line(commands, 2U, "   ", output,
                                       sizeof(output)));
    TEST_ASSERT_EQUAL(CLI_STATUS_UNKNOWN_COMMAND,
                      cli_execute_line(commands, 2U, "reset", output,
                                       sizeof(output)));
}

void test_cli_reports_handler_argument_error(void)
{
    char output[8] = {0};

    TEST_ASSERT_EQUAL(CLI_STATUS_INVALID_ARGUMENTS,
                      cli_execute_line(commands, 2U, "add 2", output,
                                       sizeof(output)));
}

void test_cli_reports_output_buffer_error(void)
{
    char output[4] = {0};

    TEST_ASSERT_EQUAL(CLI_STATUS_OUTPUT_TOO_SMALL,
                      cli_execute_line(commands, 2U, "ping", output,
                                       sizeof(output)));
}

void test_cli_rejects_invalid_input_pointers(void)
{
    char output[8] = {0};

    TEST_ASSERT_EQUAL(CLI_STATUS_NULL_POINTER,
                      cli_execute_line(NULL, 2U, "ping", output,
                                       sizeof(output)));
    TEST_ASSERT_EQUAL(CLI_STATUS_NULL_POINTER,
                      cli_execute_line(commands, 2U, NULL, output,
                                       sizeof(output)));
    TEST_ASSERT_EQUAL(CLI_STATUS_NULL_POINTER,
                      cli_execute_line(commands, 2U, "ping", NULL,
                                       sizeof(output)));
}

void test_cli_rejects_line_too_long(void)
{
    char line[CLI_MAX_LINE_LENGTH + 2U];
    char output[8] = {0};

    memset(line, 'x', sizeof(line));
    line[sizeof(line) - 1U] = '\0';

    TEST_ASSERT_EQUAL(CLI_STATUS_LINE_TOO_LONG,
                      cli_execute_line(commands, 2U, line, output,
                                       sizeof(output)));
}
