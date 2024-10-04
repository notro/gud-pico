// SPDX-License-Identifier: MIT

////////////////////////////////////////////////////////////////////////////////////////////////////
//
// This example has extra code for testing pixel formats that are not supported by the Linux gadget.
// Maybe look at mi0283qt for a cleaner example.
//
////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board.h"
#include "tusb.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "pico/unique_id.h"

#include "driver.h"
#include "gud.h"
#include "mipi_dbi.h"
#include "cie1931.h"

#define USE_WATCHDOG    1
#define PANIC_REBOOT_BLINK_LED_MS   100

#define LOG
#define LOG2
#define LOG3

/*
 * 0 = led off
 * 1 = power led, off while flushing
 * 2 = on while flushing
 */
#define LED_ACTION  1

#define BL_GPIO 20
#define BL_DEF_LEVEL 100

#define XOFF    40
#define YOFF    53
#define WIDTH   240
#define HEIGHT  135

static uint16_t framebuffer[WIDTH * HEIGHT];
static uint16_t compress_buf[WIDTH * HEIGHT];

// Used when emulating pixel formats
static uint16_t conversion_buffer[WIDTH * HEIGHT];

static bool display_enabled;
static uint64_t panic_reboot_blink_time;

static const struct mipi_dbi dbi = {
    .spi = spi0,
    .sck = 18,
    .mosi = 19,
    .cs = 17,
    .dc = 16,
    .baudrate = 64 * 1024 * 1024, // 64MHz
};

static uint brightness = BL_DEF_LEVEL;

static void backlight_set(int level)
{
    uint16_t pwm;

    LOG("Set backlight: %d\n", level);
    if (level > 100)
        return;

    if (level < 0)
        pwm = 0;
    else
        pwm = cie1931[level];

    pwm_set_gpio_level(BL_GPIO, pwm);
}

static void backlight_init(uint gpio)
{
    pwm_config cfg = pwm_get_default_config();
    pwm_set_wrap(pwm_gpio_to_slice_num(gpio), 65535);
    pwm_init(pwm_gpio_to_slice_num(gpio), &cfg, true);
    gpio_set_function(gpio, GPIO_FUNC_PWM);
}

static int controller_enable(const struct gud_display *disp, uint8_t enable)
{
    LOG("%s: enable=%u\n", __func__, enable);
    return 0;
}

static int display_enable(const struct gud_display *disp, uint8_t enable)
{
    LOG("%s: enable=%u\n", __func__, enable);

    display_enabled = enable;

    if (enable)
        backlight_set(brightness);
    else
        backlight_set(-1);

    return 0;
}

static int state_commit(const struct gud_display *disp, const struct gud_state_req *state, uint8_t num_properties)
{
    LOG("%s: mode=%ux%u format=0x%02x connector=%u num_properties=%u\n",
        __func__, state->mode.hdisplay, state->mode.vdisplay, state->format, state->connector, num_properties);

    for (uint8_t i = 0; i < num_properties; i++) {
        const struct gud_property_req *prop = &state->properties[i];
        LOG("  prop=%u val=%llu\n", prop->prop, prop->val);
        switch (prop->prop) {
            case GUD_PROPERTY_BACKLIGHT_BRIGHTNESS:
                brightness = prop->val;
                if (display_enabled)
                    backlight_set(brightness);
                break;
            default:
                LOG("Unknown property: %u\n", prop->prop);
                break;
        }
    }

    return 0;
}

static int set_buffer(const struct gud_display *disp, const struct gud_set_buffer_req *set_buf)
{
    LOG3("%s: x=%u y=%u width=%u height=%u length=%u compression=0x%x\n", __func__,
         set_buf->x, set_buf->y, set_buf->width, set_buf->height, set_buf->length, set_buf->compression);

    mipi_dbi_update_wait(&dbi);

    if (LED_ACTION == 1)
        board_led_write(false);
    else if (LED_ACTION == 2)
        board_led_write(true);

    return 0;
}

static size_t r1_to_rgb565(uint16_t *dst, uint8_t *src, uint16_t src_width, uint16_t src_height)
{
    uint8_t val = 0;
    size_t len = 0;

    for (uint16_t y = 0; y < src_height; y++) {
        for (uint16_t x = 0; x < src_width; x++) {
            if (!(x % 8))
                val = *src++;
            *dst++ = val & 0x80 ? 0xffffffff : 0;
            len += sizeof(*dst);
            val <<= 1;
        }
   }

   return len;
}

static size_t r8_to_rgb565(uint16_t *dst, uint8_t *src, uint16_t src_width, uint16_t src_height)
{
    size_t len = 0;

    for (uint16_t y = 0; y < src_height; y++) {
        for (uint16_t x = 0; x < src_width; x++) {
            #define GREY_MASK   0xff // 8-bits = 0xff, 4-bits = 0xf0, 2-bits = 0xc0
            uint8_t grey = *src++ & GREY_MASK;
            *dst++ = ((grey >> 3) << 11) | ((grey >> 2) << 5) | (grey >> 3);
            len += sizeof(*dst);
        }
   }

   return len;
}

static size_t rgb111_to_rgb565(uint16_t *dst, uint8_t *src, uint16_t src_width, uint16_t src_height)
{
    uint8_t rgb111, val = 0;
    size_t len = 0;

    for (uint16_t y = 0; y < src_height; y++) {
        for (uint16_t x = 0; x < src_width; x++) {
            if (!(x % 2))
                val = *src++;
            rgb111 = val >> 4;
            *dst++ = ((rgb111 & 0x04) << 13) | ((rgb111 & 0x02) << 9) | ((rgb111 & 0x01) << 4);
            len += sizeof(*dst);
            val <<= 4;
        }
    }

   return len;
}

static size_t rgb332_to_rgb565(uint16_t *dst, uint8_t *src, uint16_t src_width, uint16_t src_height)
{
    size_t len = 0;

    for (uint16_t y = 0; y < src_height; y++) {
        for (uint16_t x = 0; x < src_width; x++) {
            *dst++ = ((*src & 0xe0) << 8) | ((*src & 0x1c) << 6) | ((*src & 0x03) << 3);
            src++;
            len += sizeof(*dst);
        }
    }

   return len;
}

static void write_buffer(const struct gud_display *disp, const struct gud_set_buffer_req *set_buf, void *buf)
{
    uint32_t length = set_buf->length;

    LOG2("%s: x=%u y=%u width=%u height=%u length=%u compression=0x%x\n", __func__,
         set_buf->x, set_buf->y, set_buf->width, set_buf->height, set_buf->length, set_buf->compression);

    if (disp->formats[0] == GUD_PIXEL_FORMAT_R1) {
        length = r1_to_rgb565(conversion_buffer, buf, set_buf->width, set_buf->height);
        buf = conversion_buffer;
    } else if (disp->formats[0] == GUD_PIXEL_FORMAT_R8) {
        length = r8_to_rgb565(conversion_buffer, buf, set_buf->width, set_buf->height);
        buf = conversion_buffer;
    } else if (disp->formats[0] == GUD_PIXEL_FORMAT_XRGB1111) {
        length = rgb111_to_rgb565(conversion_buffer, buf, set_buf->width, set_buf->height);
        buf = conversion_buffer;
    } else if (disp->formats[0] == GUD_PIXEL_FORMAT_RGB332) {
        length = rgb332_to_rgb565(conversion_buffer, buf, set_buf->width, set_buf->height);
        buf = conversion_buffer;
    }

    mipi_dbi_update16(&dbi, set_buf->x + XOFF, set_buf->y + YOFF, set_buf->width, set_buf->height, buf, length);

    if (LED_ACTION == 1)
        board_led_write(true);
    else if (LED_ACTION == 2)
        board_led_write(false);
}

static const uint8_t pixel_formats[] = {
//    GUD_PIXEL_FORMAT_R1,
//    GUD_PIXEL_FORMAT_R8,
//    GUD_PIXEL_FORMAT_XRGB1111,
//    GUD_PIXEL_FORMAT_RGB332,
    GUD_PIXEL_FORMAT_RGB565,
};

static const struct gud_property_req connector_properties[] = {
    {
        .prop = GUD_PROPERTY_BACKLIGHT_BRIGHTNESS,
        .val = BL_DEF_LEVEL,
    },
};

static uint32_t gud_display_edid_get_serial_number(void)
{
    pico_unique_board_id_t id_out;

    pico_get_unique_board_id(&id_out);
    return *((uint64_t*)(id_out.id));
}

static const struct gud_display_edid edid = {
    .name = "pico display",
    .pnp = "PIM",
    .product_code = 0x01,
    .year = 2021,
    .width_mm = 27,
    .height_mm = 16,

    .gamma = 220,
    .bit_depth = 6,

    .get_serial_number = gud_display_edid_get_serial_number,
};

const struct gud_display display = {
    .width = WIDTH,
    .height = HEIGHT,

//    .flags = GUD_DISPLAY_FLAG_FULL_UPDATE,

    .compression = GUD_COMPRESSION_LZ4,

    .formats = pixel_formats,
    .num_formats = 1,

    .connector_properties = connector_properties,
    .num_connector_properties = 1,
#if USE_WATCHDOG
    // Tell the host to send a connector status request every 10 seconds for our tinyusb "watchdog"
    .connector_flags = GUD_CONNECTOR_FLAGS_POLL_STATUS,
#endif

    .edid = &edid,

    .controller_enable = controller_enable,
    .display_enable = display_enable,

    .state_commit = state_commit,

    .set_buffer = set_buffer,
    .write_buffer = write_buffer,
};

static void init_display(void)
{
    backlight_init(BL_GPIO);
    mipi_dbi_spi_init(&dbi);

    mipi_dbi_command(&dbi, MIPI_DCS_SOFT_RESET);

    sleep_ms(150);

    mipi_dbi_command(&dbi, MIPI_DCS_SET_ADDRESS_MODE, 0x70);
    mipi_dbi_command(&dbi, MIPI_DCS_SET_PIXEL_FORMAT, MIPI_DCS_PIXEL_FORMAT_16BIT);

    mipi_dbi_command(&dbi, MIPI_DCS_ENTER_INVERT_MODE);
    mipi_dbi_command(&dbi, MIPI_DCS_EXIT_SLEEP_MODE);
    mipi_dbi_command(&dbi, MIPI_DCS_SET_DISPLAY_ON);

    sleep_ms(100);

    // Clear display
    mipi_dbi_update16(&dbi, XOFF, YOFF, WIDTH, HEIGHT, framebuffer, WIDTH * HEIGHT * 2);
}

static void pwm_gpio_init(uint gpio, uint16_t val)
{
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_output_polarity(&cfg, true, true);
    pwm_set_wrap(pwm_gpio_to_slice_num(gpio), 65535);
    pwm_init(pwm_gpio_to_slice_num(gpio), &cfg, true);
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    pwm_set_gpio_level(gpio, val);
}

static void turn_off_rgb_led(void)
{
    pwm_gpio_init(6, 0);
    pwm_gpio_init(7, 0);
    pwm_gpio_init(8, 0);
}

int main(void)
{
    board_init();

    if (USE_WATCHDOG && watchdog_caused_reboot()) {
        LOG("Rebooted by Watchdog!\n");
        panic_reboot_blink_time = 1;
    }

    if (LED_ACTION)
        board_led_write(true);

    init_display();

    gud_driver_setup(&display, framebuffer, compress_buf);

    tusb_init();

    LOG("\n\n%s: CFG_TUSB_DEBUG=%d\n", __func__, CFG_TUSB_DEBUG);

    turn_off_rgb_led();

    if (USE_WATCHDOG)
        watchdog_enable(5000, 0); // pause_on_debug=0 so it can reset panics.

    while (1)
    {
        tud_task(); // tinyusb device task

        if (USE_WATCHDOG) {
            watchdog_update();

            uint64_t now = time_us_64();
            if (PANIC_REBOOT_BLINK_LED_MS && panic_reboot_blink_time && panic_reboot_blink_time < now) {
                static bool led_state;
                board_led_write(led_state);
                led_state = !led_state;
                panic_reboot_blink_time = now + (PANIC_REBOOT_BLINK_LED_MS * 1000);
            }

            // Sometimes we stop receiving USB requests, but the host thinks everything is fine.
            // Reset if we haven't heard from tinyusb, the host sends connector status requests every 10 seconds
            // Let the watchdog do the reset
            if (gud_driver_req_timeout(15))
                panic("Request TIMEOUT");
        }
    }

    return 0;
}

void tud_mount_cb(void)
{
    LOG("%s:\n", __func__);
    if (LED_ACTION == 2)
        board_led_write(false);
}
