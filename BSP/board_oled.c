#include "board_oled.h"

#include <stddef.h>
#include <string.h>

#include "board_i2c.h"

#define BOARD_OLED_CONTROL_COMMAND    0x00U
#define BOARD_OLED_CONTROL_DATA       0x40U

/* One control byte plus one complete 128-byte OLED page. */
static uint8_t s_oled_i2c_buffer[BOARD_OLED_WIDTH + 1U];

static const uint8_t s_oled_clear_page[BOARD_OLED_WIDTH] =
{
    0U
};

/* SSD1306 command sequence used by the project's 128 x 32 OLED. */
static const uint8_t s_oled_init_commands[] =
{
    0xAEU,             /* display off */
    0xD5U, 0x80U,      /* display clock divide ratio */
    0xA8U, 0x1FU,      /* multiplex ratio: 1/32 */
    0xD3U, 0x00U,      /* display offset */
    0x40U,             /* display start line 0 */
    0x8DU, 0x14U,      /* charge pump enabled */
    0xA1U,             /* segment remap */
    0xC8U,             /* COM scan direction remapped */
    0xDAU, 0x00U,      /* COM pins for the existing module */
    0x81U, 0x80U,      /* contrast */
    0xD9U, 0x1FU,      /* pre-charge period */
    0xDBU, 0x40U,      /* VCOMH deselect level */
    0xA4U,             /* resume RAM content display */
    0xAFU              /* display on */
};

static board_oled_status_t board_oled_write_controlled(
    uint8_t control,
    const uint8_t *data,
    uint16_t length)
{
    if ((data == NULL) || (length == 0U) ||
        (length > BOARD_OLED_WIDTH))
    {
        return BOARD_OLED_STATUS_INVALID_ARGUMENT;
    }

    s_oled_i2c_buffer[0] = control;
    (void)memcpy(&s_oled_i2c_buffer[1], data, length);

    if (board_i2c0_write(
            BOARD_OLED_I2C_ADDRESS,
            s_oled_i2c_buffer,
            (uint16_t)(length + 1U)) == 0)
    {
        return BOARD_OLED_STATUS_I2C_ERROR;
    }

    return BOARD_OLED_STATUS_OK;
}

static board_oled_status_t board_oled_set_page(uint8_t page)
{
    board_oled_status_t status;

    if (page >= BOARD_OLED_PAGE_COUNT)
    {
        return BOARD_OLED_STATUS_INVALID_ARGUMENT;
    }

    status = board_oled_write_command((uint8_t)(0xB0U | page));
    if (status != BOARD_OLED_STATUS_OK)
    {
        return status;
    }

    status = board_oled_write_command(0x00U);
    if (status != BOARD_OLED_STATUS_OK)
    {
        return status;
    }

    return board_oled_write_command(0x10U);
}

board_oled_status_t board_oled_write_command(uint8_t command)
{
    return board_oled_write_controlled(
        BOARD_OLED_CONTROL_COMMAND,
        &command,
        1U);
}

board_oled_status_t board_oled_write_data(const uint8_t *data, uint16_t length)
{
    return board_oled_write_controlled(
        BOARD_OLED_CONTROL_DATA,
        data,
        length);
}

board_oled_status_t board_oled_display_on(void)
{
    return board_oled_write_command(0xAFU);
}

board_oled_status_t board_oled_display_off(void)
{
    return board_oled_write_command(0xAEU);
}

board_oled_status_t board_oled_init(void)
{
    uint16_t index;
    board_oled_status_t status;

    for (index = 0U;
         index < (uint16_t)sizeof(s_oled_init_commands);
         index++)
    {
        status = board_oled_write_command(s_oled_init_commands[index]);
        if (status != BOARD_OLED_STATUS_OK)
        {
            return status;
        }
    }

    return board_oled_clear();
}

board_oled_status_t board_oled_clear(void)
{
    uint8_t page;
    board_oled_status_t status;

    for (page = 0U; page < BOARD_OLED_PAGE_COUNT; page++)
    {
        status = board_oled_set_page(page);
        if (status != BOARD_OLED_STATUS_OK)
        {
            return status;
        }

        status = board_oled_write_data(
            s_oled_clear_page,
            BOARD_OLED_WIDTH);
        if (status != BOARD_OLED_STATUS_OK)
        {
            return status;
        }
    }

    return BOARD_OLED_STATUS_OK;
}

board_oled_status_t board_oled_refresh(
    const uint8_t *framebuffer,
    uint16_t length)
{
    uint8_t page;
    board_oled_status_t status;

    if ((framebuffer == NULL) ||
        (length != BOARD_OLED_FRAMEBUFFER_SIZE))
    {
        return BOARD_OLED_STATUS_INVALID_ARGUMENT;
    }

    for (page = 0U; page < BOARD_OLED_PAGE_COUNT; page++)
    {
        status = board_oled_set_page(page);
        if (status != BOARD_OLED_STATUS_OK)
        {
            return status;
        }

        status = board_oled_write_data(
            &framebuffer[(uint16_t)page * BOARD_OLED_WIDTH],
            BOARD_OLED_WIDTH);
        if (status != BOARD_OLED_STATUS_OK)
        {
            return status;
        }
    }

    return BOARD_OLED_STATUS_OK;
}
