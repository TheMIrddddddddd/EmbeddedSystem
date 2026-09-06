#include <string.h>

#include "app_cli.h"
#include "app_config.h"
#include "board_usart.h"
#include "cli.h"

#include "FreeRTOS.h"
#include "task.h"

#define APP_CLI_DRAIN_TIMEOUT_TICKS    500U
#define APP_CLI_OUTPUT_BUFFER_SIZE     64U

/*
 * banner 与 help 共用这份文本，只此一份，不放两处。
 */
static const char s_cli_help_text[] =
    " Commands:\r\n"
    "   test              system self check\r\n"
    "   rtc config/now    set or show time\r\n"
    "   conf              import config.ini from TF\r\n"
    "   ratio ch0/ch1     set channel ratio (0-100)\r\n"
    "   limit ch0/ch1     set channel limit (0-500)\r\n"
    "   config save/read  store or show parameters\r\n"
    "   protocol          switch RS485 protocol\r\n"
    "   id                set device id\r\n"
    "   baud              set RS485 baudrate\r\n"
    "   start / stop      sampling on / off\r\n"
    "   hide / unhide     hidden hex format on / off\r\n"
    "   help              show this list\r\n"
    "   version           show firmware version\r\n"
    "==================================================\r\n";

static const char s_cli_version_text[] =
    "app version " APP_CLI_VERSION " (" __DATE__ " " __TIME__ ")\r\n";

static cli_status_t app_cli_start(int argc, const char *argv[],
                                  char *output, size_t output_size)
{
    (void)argv;

    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_config_sample_enable_set(1U);

    (void)strcpy(output, "sampling started");

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_stop(int argc, const char *argv[],
                                 char *output, size_t output_size)
{
    (void)argv;

    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_config_sample_enable_set(0U);

    (void)strcpy(output, "sampling stopped");

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_hide(int argc, const char *argv[],
                                 char *output, size_t output_size)
{
    (void)argv;

    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_config_hide_mode_set(1U);

    (void)strcpy(output, "hide mode on");

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_unhide(int argc, const char *argv[],
                                   char *output, size_t output_size)
{
    (void)argv;

    if (argc != 1)
    {
        return CLI_STATUS_INVALID_ARGUMENTS;
    }

    (void)app_config_hide_mode_set(0U);

    (void)strcpy(output, "hide mode off");

    return CLI_STATUS_OK;
}

int app_cli_write(const char *data, uint16_t length)
{
    uint16_t offset;
    uint32_t waited_ticks;

    if (data == NULL)
    {
        return 0;
    }

    offset = 0U;
    waited_ticks = 0U;

    while (offset < length)
    {
        uint16_t free_space;
        uint16_t chunk;

        free_space = board_usart0_tx_free_get();

        if (free_space == 0U)
        {
            if (waited_ticks >= APP_CLI_DRAIN_TIMEOUT_TICKS)
            {
                return 0;
            }

            vTaskDelay(1U);
            waited_ticks++;

            continue;
        }

        chunk = (uint16_t)(length - offset);

        if (chunk > free_space)
        {
            chunk = free_space;
        }

        if (board_usart0_send_buffer((const uint8_t *)&data[offset], chunk) != chunk)
        {
            /* 正常到不了这里：ISR 只消费不生产，free 只会变大。
             * 防将来出现第二个发送者时静默丢字。 */
            return 0;
        }

        offset = (uint16_t)(offset + chunk);
    }

    return 1;
}

int app_cli_print(const char *text)
{
    static const char newline[2] = { '\r', '\n' };
    uint16_t length;

    if (text == NULL)
    {
        return 0;
    }

    length = (uint16_t)strlen(text);

    if ((app_cli_write(text, length) == 0) ||
        (app_cli_write(newline, 2U) == 0))
    {
        return 0;
    }

    return 1;
}

void app_cli_banner_print(void)
{
    (void)app_cli_print("==================================================");
    (void)app_cli_print(" GD32F470 Industrial Data Terminal");
    (void)app_cli_print(" Console ready. Type 'help' for commands.");
    (void)app_cli_print("==================================================");

    (void)app_cli_write(s_cli_help_text, (uint16_t)(sizeof(s_cli_help_text) - 1U));

    (void)app_cli_write("\r\n", 2U);
}

/*
 * 约定：短回复（一两行）写进 output 缓冲由这里统一打印；
 * help 这类长文本直接 app_cli_write 流式输出，output 保持空。
 */
static cli_status_t app_cli_help(int argc, const char *argv[],
                                 char *output, size_t output_size)
{
    (void)argc;
    (void)argv;
    (void)output;
    (void)output_size;

    (void)app_cli_write(s_cli_help_text, (uint16_t)(sizeof(s_cli_help_text) - 1U));

    return CLI_STATUS_OK;
}

static cli_status_t app_cli_version(int argc, const char *argv[],
                                    char *output, size_t output_size)
{
    (void)argc;
    (void)argv;

    if (strlen(s_cli_version_text) >= output_size)
    {
        return CLI_STATUS_OUTPUT_TOO_SMALL;
    }

    (void)strcpy(output, s_cli_version_text);

    return CLI_STATUS_OK;
}

static const cli_command_t s_cli_commands[] =
{
    { "help",    app_cli_help },
    { "version", app_cli_version },
    { "start",   app_cli_start },
    { "stop",    app_cli_stop },
    { "hide",    app_cli_hide },
    { "unhide",  app_cli_unhide }
};

#define APP_CLI_COMMAND_COUNT \
    (sizeof(s_cli_commands) / sizeof(s_cli_commands[0]))

void app_cli_execute_line(const char *line)
{
    char output[APP_CLI_OUTPUT_BUFFER_SIZE];
    cli_status_t status;

    output[0] = '\0';    /* handler 可能不写 output（如 help 直接流式输出），先置空串 */

    status = cli_execute_line(s_cli_commands, APP_CLI_COMMAND_COUNT,
                              line, output, sizeof(output));

    switch (status)
    {
    case CLI_STATUS_OK:
        if (output[0] != '\0')
        {
            (void)app_cli_print(output);
        }
        break;

    case CLI_STATUS_UNKNOWN_COMMAND:
        (void)app_cli_write("unknown command: ", 17U);
        (void)app_cli_print(line);
        break;

    case CLI_STATUS_LINE_TOO_LONG:
        (void)app_cli_print("line too long");
        break;

    case CLI_STATUS_INVALID_ARGUMENTS:
        (void)app_cli_print("invalid arguments");
        break;

    case CLI_STATUS_OUTPUT_TOO_SMALL:
        (void)app_cli_print("output buffer too small");
        break;

    case CLI_STATUS_NULL_POINTER:
    case CLI_STATUS_EMPTY_LINE:
    default:
        /* EMPTY_LINE 到不了：空行在 ControlTask 已过滤 */
        (void)app_cli_print("cli error");
        break;
    }
}
