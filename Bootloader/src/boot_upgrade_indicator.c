#include "boot_upgrade_indicator.h"

#include <string.h>

#include "board_i2c.h"
#include "board_oled.h"
#include "gd32f4xx_fwdgt.h"

#define BOOT_UPGRADE_INDICATOR_BAR_LEFT       8U
#define BOOT_UPGRADE_INDICATOR_BAR_RIGHT      (BOARD_OLED_WIDTH - 9U)
#define BOOT_UPGRADE_INDICATOR_BAR_TOP        11U
#define BOOT_UPGRADE_INDICATOR_BAR_BOTTOM     20U
#define BOOT_UPGRADE_INDICATOR_BAR_INNER_LEFT  (BOOT_UPGRADE_INDICATOR_BAR_LEFT + 2U)
#define BOOT_UPGRADE_INDICATOR_BAR_INNER_RIGHT (BOOT_UPGRADE_INDICATOR_BAR_RIGHT - 2U)

static uint8_t s_framebuffer[BOARD_OLED_FRAMEBUFFER_SIZE];
static uint8_t s_last_rendered_progress = 0xFFU;

volatile boot_upgrade_indicator_status_t g_boot_upgrade_indicator_status =
    BOOT_UPGRADE_INDICATOR_STATUS_DISABLED;
volatile uint8_t g_boot_upgrade_indicator_oled_ready;
volatile uint8_t g_boot_upgrade_indicator_progress;

static void boot_upgrade_indicator_pixel(uint8_t x, uint8_t y)
{
    uint16_t index;

    if ((x >= BOARD_OLED_WIDTH) || (y >= BOARD_OLED_HEIGHT))
    {
        return;
    }

    index = (uint16_t)((y / 8U) * BOARD_OLED_WIDTH + x);
    s_framebuffer[index] |= (uint8_t)(1U << (y % 8U));
}

static void boot_upgrade_indicator_draw_bar(uint8_t percent)
{
    uint8_t x;
    uint8_t y;
    uint8_t fill_width;

    (void)memset(
        s_framebuffer,
        0,
        sizeof(s_framebuffer)
    );

    for (x = BOOT_UPGRADE_INDICATOR_BAR_LEFT;
         x <= BOOT_UPGRADE_INDICATOR_BAR_RIGHT;
         x++)
    {
        boot_upgrade_indicator_pixel(x, BOOT_UPGRADE_INDICATOR_BAR_TOP);
        boot_upgrade_indicator_pixel(x, BOOT_UPGRADE_INDICATOR_BAR_BOTTOM);
    }

    for (y = BOOT_UPGRADE_INDICATOR_BAR_TOP;
         y <= BOOT_UPGRADE_INDICATOR_BAR_BOTTOM;
         y++)
    {
        boot_upgrade_indicator_pixel(
            BOOT_UPGRADE_INDICATOR_BAR_LEFT,
            y
        );
        boot_upgrade_indicator_pixel(
            BOOT_UPGRADE_INDICATOR_BAR_RIGHT,
            y
        );
    }

    fill_width = (uint8_t)(
        ((uint16_t)(BOOT_UPGRADE_INDICATOR_BAR_INNER_RIGHT -
                    BOOT_UPGRADE_INDICATOR_BAR_INNER_LEFT + 1U) *
         percent) / 100U
    );

    for (x = BOOT_UPGRADE_INDICATOR_BAR_INNER_LEFT;
         x < (uint8_t)(BOOT_UPGRADE_INDICATOR_BAR_INNER_LEFT + fill_width);
         x++)
    {
        for (y = (BOOT_UPGRADE_INDICATOR_BAR_TOP + 2U);
             y <= (BOOT_UPGRADE_INDICATOR_BAR_BOTTOM - 2U);
             y++)
        {
            boot_upgrade_indicator_pixel(x, y);
        }
    }
}

void boot_upgrade_indicator_init(void)
{
    s_last_rendered_progress = 0xFFU;
    g_boot_upgrade_indicator_status =
        BOOT_UPGRADE_INDICATOR_STATUS_DISABLED;
    g_boot_upgrade_indicator_oled_ready = 0U;
    g_boot_upgrade_indicator_progress = 0U;

    if (board_i2c0_init() == 0)
    {
        g_boot_upgrade_indicator_status =
            BOOT_UPGRADE_INDICATOR_STATUS_I2C_INIT_FAILED;
        return;
    }

    if (board_oled_init() != BOARD_OLED_STATUS_OK)
    {
        g_boot_upgrade_indicator_status =
            BOOT_UPGRADE_INDICATOR_STATUS_OLED_INIT_FAILED;
        return;
    }

    g_boot_upgrade_indicator_oled_ready = 1U;
    g_boot_upgrade_indicator_status = BOOT_UPGRADE_INDICATOR_STATUS_OK;
    boot_upgrade_indicator_set_progress(0U);
}

void boot_upgrade_indicator_set_progress(uint8_t percent)
{
    board_oled_status_t oled_status;

    if (percent > 100U)
    {
        percent = 100U;
    }

    g_boot_upgrade_indicator_progress = percent;

    if ((g_boot_upgrade_indicator_oled_ready == 0U) ||
        (s_last_rendered_progress == percent))
    {
        return;
    }

    boot_upgrade_indicator_draw_bar(percent);

    /* 一次完整刷新很短，但在刷新前后喂狗，避免异常总线等待影响 Boot。 */
    fwdgt_counter_reload();
    oled_status = board_oled_refresh(
        s_framebuffer,
        BOARD_OLED_FRAMEBUFFER_SIZE
    );
    fwdgt_counter_reload();

    if (oled_status != BOARD_OLED_STATUS_OK)
    {
        g_boot_upgrade_indicator_status =
            BOOT_UPGRADE_INDICATOR_STATUS_REFRESH_FAILED;
        g_boot_upgrade_indicator_oled_ready = 0U;
        return;
    }

    s_last_rendered_progress = percent;
}

void boot_upgrade_indicator_set_install_stage(install_stage_t stage)
{
    switch (stage)
    {
        case INSTALL_BACKUP_START:
            boot_upgrade_indicator_set_progress(96U);
            break;

        case INSTALL_BACKUP_VALID:
            boot_upgrade_indicator_set_progress(97U);
            break;

        case INSTALL_APP_ERASING:
            boot_upgrade_indicator_set_progress(98U);
            break;

        case INSTALL_APP_PROGRAMMING:
            boot_upgrade_indicator_set_progress(99U);
            break;

        case INSTALL_APP_VALID:
            boot_upgrade_indicator_set_progress(100U);
            break;

        default:
            boot_upgrade_indicator_set_progress(95U);
            break;
    }
}

void boot_upgrade_indicator_shutdown(void)
{
    if (g_boot_upgrade_indicator_oled_ready == 0U)
    {
        return;
    }

    fwdgt_counter_reload();
    (void)board_oled_clear();
    (void)board_oled_display_off();
    fwdgt_counter_reload();

    g_boot_upgrade_indicator_oled_ready = 0U;
    g_boot_upgrade_indicator_status =
        BOOT_UPGRADE_INDICATOR_STATUS_DISABLED;
}
