//  Copyright (c) 2017
//  Benjamin Vanheuverzwijn <bvanheu@gmail.com>
//  Marc-Etienne M. Leveille <marc.etienne.ml@gmail.com>
//
//  License: MIT (see LICENSE for details)


#include "ble/nsec_ble.h"

#include <nrf.h>
#include <nrf_error.h>
#include <nrf_gpio.h>
#include <nrf_delay.h>
#include <nrf52.h>
#include <nrf52_bitfields.h>
#include <nordic_common.h>
#include <stdint.h>
#include <nrf_soc.h>

#include "ble/ble_device.h"
#include "drivers/battery.h"
#include "drivers/battery_manager.h"
#include "drivers/buttons.h"
#include "drivers/display.h"
#include "drivers/power.h"
#include "drivers/softdevice.h"
#include "drivers/uart.h"
#include "drivers/ws2812fx.h"
#include "drivers/nsec_storage.h"

#include "logs.h"
#include "gfx_effect.h"
#include "identity.h"
#include "menu.h"
#include "nsec_conf_schedule.h"
#include "nsec_settings.h"
#include "timer.h"
#include "status_bar.h"
#include "utils.h"
#include "nsec_led_pattern.h"
#include "nsec_warning.h"
#include "nsec_led_ble.h"

#include "images/nsec_logo_bitmap.c"
#include "ble/service_characteristic.h"
#include "ble/vendor_service.h"

static char g_device_id[10];
bool is_at_main_menu = false;

extern uint16_t gfx_width;
extern uint16_t gfx_height;

/*
 * Callback function when APP_ERROR_CHECK() fails
 *
 *  - Display the filename, line number and error code.
 *  - Flash all neopixels red for 5 seconds.
 *  - Reset the system.
 */
void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
    static int error_displayed = 0;
    uint8_t count = 50;

    if(!error_displayed) {
        char error_msg[128];
        error_info_t *err_info = (error_info_t *) info;
        snprintf(error_msg, sizeof(error_msg), "%s:%u -> error 0x%08x\r\n",
                 err_info->p_file_name, (unsigned int)err_info->line_num,
                 (unsigned int)err_info->err_code);
        puts(error_msg);
        gfx_set_cursor(0, 0);
        gfx_puts(error_msg);
        gfx_update();
        error_displayed = 1;
    }

    init_WS2812FX();
    setBrightness_WS2812FX(64);
    setSpeed_WS2812FX(200);
    setColor_WS2812FX(255, 0, 0);
    setMode_WS2812FX(FX_MODE_BLINK);
    start_WS2812FX();

    while (count > 0) {
        service_WS2812FX();
        nrf_delay_ms(100);
        count--;
    }

    NVIC_SystemReset();
}

static
void nsec_intro(void) {
    for(uint8_t noise = 128; noise <= 128; noise -= 8) {
        gfx_fill_screen(DISPLAY_BLACK);
        gfx_draw_bitmap(17, 11, nsec_logo_bitmap, nsec_logo_bitmap_width, nsec_logo_bitmap_height, DISPLAY_BLUE);
        nsec_gfx_effect_addNoise(noise);
        gfx_update();
    }
    for(uint8_t noise = 0; noise <= 128; noise += 8) {
        gfx_fill_screen(DISPLAY_BLACK);
        gfx_draw_bitmap(17, 11, nsec_logo_bitmap, nsec_logo_bitmap_width, nsec_logo_bitmap_height, DISPLAY_BLUE);
        nsec_gfx_effect_addNoise(noise);
        gfx_update();
    }
}

void open_conference_schedule(uint8_t item) {
    menu_close();
    is_at_main_menu = false;
    nsec_schedule_show_dates();
}

void open_led_pattern(uint8_t item) {
    menu_close();
    is_at_main_menu = false;
    nsec_led_pattern_show();
}

void open_settings(uint8_t item) {
    menu_close();
    is_at_main_menu = false;
    nsec_setting_show();
}

void open_warning(uint8_t item) {
    menu_close();
    is_at_main_menu = false;
    nsec_warning_show();
}

void open_battery_status(uint8_t item) {
    menu_close();
    is_at_main_menu = false;
    show_battery_status();
}

#ifdef NSEC_FLAVOR_CTF
menu_item_s main_menu_items[] = {
    {
        .label = "LED pattern",
        .handler = open_led_pattern,
    }, {
        .label = "Settings",
        .handler = open_settings,
    }, {
        .label = "Battery status",
        .handler = open_battery_status,
    }, {
        .label = "Battery Warning",
        .handler = open_warning,
    }
};
#else
static
menu_item_s main_menu_items[] = {
    {
        .label = "Conference schedule",
        .handler = open_conference_schedule,
    }, {
        .label = "LED pattern",
        .handler = open_led_pattern,
    }, {
        .label = "Settings",
        .handler = open_settings,
    }, {
        .label = "Battery status",
        .handler = open_battery_status,
    }, {
        .label = "Battery Warning",
        .handler = open_warning,
    }
};
#endif

void show_main_menu(void) {
    nsec_identity_draw();
    menu_init(0, gfx_height-8, gfx_width, 8, sizeof(main_menu_items) / sizeof(main_menu_items[0]), main_menu_items);
    is_at_main_menu = true;
}


int main(void) {
#if defined(NSEC_HARDCODED_BLE_DEVICE_ID)
    sprintf(g_device_id, "%.8s", NSEC_STRINGIFY(NSEC_HARDCODED_BLE_DEVICE_ID));
#else
    sprintf(g_device_id, "NSEC%04X", (uint16_t)(NRF_FICR->DEVICEID[1] % 0xFFFF));
#endif
    g_device_id[9] = '\0';

    /*
     * Initialize base hardware
     */
    log_init();
    power_init();
    softdevice_init();
    uart_init();
    timer_init();
    init_WS2812FX();
    display_init();
    gfx_set_text_background_color(DISPLAY_WHITE, DISPLAY_BLACK);
    nsec_buttons_init();

    /*
     * Initialize bluetooth stack
     */
    create_ble_device(g_device_id);
    configure_advertising();
    nsec_led_ble_init();
    init_identity_service();
    start_advertising();

    nsec_status_bar_init();
    nsec_battery_manager_init();
    nsec_status_set_name(g_device_id);
    nsec_status_set_badge_class(NSEC_STRINGIFY(NSEC_HARDCODED_BADGE_CLASS));
    nsec_status_set_ble_status(STATUS_BLUETOOTH_ON);

    load_stored_led_settings();

    // nsec_intro like before work really bad on the new screen, we need something new
    //nsec_intro();
    show_main_menu();

    /*
     * Main loop
     */
    while(true) {
        service_WS2812FX();
        nsec_storage_update();
        power_manage();
    }

    return 0;
}