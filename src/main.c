#include <stdio.h>
#include <strings.h>
#include <ctype.h>


#include "mgos.h"

#include "mgos_mqtt.h"
#include "mgos_ro_vars.h"
#include "mgos_shadow.h"
#include "mgos_app.h"
#include "mgos_system.h"
#include "mgos_timers.h"

#include <mgos_neopixel.h>
#include <frozen.h>
#include <hsv2rgb.h>

int blinkstate = 0;
bool blink_up = true;
int offset = 0;

struct pixel
{
    int r;
    int g;
    int b;
    int x; // blink/fade the led if set. Higher values means faster blinking.
};

static struct pixel *pixels;           // My array of pixels.
static struct mgos_neopixel *my_strip; // Low-level led strip.



static void turn_on_led(void *param)
{
    mgos_gpio_write(mgos_sys_config_get_pins_statusLed(), 1);
}

static void turn_off_led(void *param)
{
    mgos_gpio_write(mgos_sys_config_get_pins_statusLed(), 0);
}

void blink_led() {
    turn_on_led(NULL);
    mgos_set_timer(100, 0, turn_off_led, 0);
}

void sync_and_show(int fade)
{
    for (int i = 0; i < mgos_sys_config_get_leds_number(); i++)
    {
        LOG(LL_DEBUG, ("Syncing pixel %d (%d:%d:%d) to %d%%", i,
                      pixels[i].r, pixels[i].g, pixels[i].b, fade));
        mgos_neopixel_set(my_strip, i,
                          pixels[i].r * fade / 100,
                          pixels[i].g * fade / 100,
                          pixels[i].b * fade / 100);
    }
    LOG(LL_DEBUG, ("Showing new pixels"));
    mgos_neopixel_show(my_strip);
}


// Test the leds
void init_animation() {
    for (int i = 0; i < mgos_sys_config_get_leds_number(); i++)
    {
        mgos_neopixel_set(my_strip,i,64,0,0);
    }
    mgos_neopixel_show(my_strip);

    mgos_msleep(1000);
    for (int i = 0; i < mgos_sys_config_get_leds_number(); i++)
    {
        mgos_neopixel_set(my_strip,i,0,64,0);
    }
    mgos_neopixel_show(my_strip);
    mgos_msleep(1000);
    for (int i = 0; i < mgos_sys_config_get_leds_number(); i++)
    {
        mgos_neopixel_set(my_strip,i,0,0,64);
    }
    mgos_neopixel_show(my_strip);
    mgos_msleep(1000);
    // Reset back to stored state.
    sync_and_show(100);

}


void set_pixel(int number, char color, int value)
{
    switch (color)
    {
    case 'r':
        pixels[number].r = value;
        break;
    case 'g':
        pixels[number].g = value;
        break;
    case 'b':
        pixels[number].b = value;
        break;
    default:
        LOG(LL_ERROR, ("Internal brain damage - unknown color %c", color));
    }
}



/* 
  Callback hooked on timer to blink the RGB leds.

  Callback 50 timer per second.
  Every time increase it with 20. When 100 or above switch
*/

static void blink_cb(void *param)
{
    LOG(LL_DEBUG, ("Entering blink: %d / %s", blinkstate, blink_up ? "true" : "false"));
    blinkstate += mgos_sys_config_get_leds_blinkstep() * blink_up ? 1 : -1;
    if (blinkstate >= 100)
    {
        blink_up = false;
        blinkstate = 100;
    }
    else if (blinkstate <= 0)
    {
        blinkstate = 0;
        blink_up = true;
    }
    LOG(LL_DEBUG, ("Blink now: %d / %s", blinkstate, blink_up ? "true" : "false"));
    sync_and_show(blinkstate);
}


// When we're connected to the shadow, report our current state.
// This may generate shadow delta on the cloud, and the cloud will push
// it down to us.

static void connected_cb(int ev, void *ev_data, void *userdata)
{
    LOG(LL_INFO, ("Connected to our shadow device. Enabling blink."));
    blink_led();
    // Enable the RGB blinking.
    /*
    mgos_set_timer(mgos_sys_config_get_leds_blinkdelay(),
                   MGOS_TIMER_REPEAT, blink_cb, NULL);

    */
    // We might need to do something silly to trigger a update.
    // mgos_shadow_updatef(0, "{on: %B}", s_light_on); /* Report status */
    (void)ev;
    (void)ev_data;
    (void)userdata;
}



static void j_cb(void *callback_data,
                 const char *name, size_t name_len,
                 const char *path, const struct json_token *token)
{
    if (token->type == JSON_TYPE_NUMBER)
    {
        int pixelid = -1;
        char color;
        int value;
        // this might accept some whitespace here and there. Unix.
        int ret = sscanf(path, ".leds.%i.%c", &pixelid, &color);
        //color = tolower(color);
        // Only proceed if we got two values from sscanf and we're talking about a known color.
        if (ret == 2 && (color == 'r' || color == 'g' || color == 'b'))
        {
            value = atoi(token->ptr);
            // This is where the magix happens:
            LOG(LL_INFO, ("Setting LED %d (%c): %d", pixelid, color, value));
            set_pixel(pixelid, color, value);
        }
    }
}

// Handle shadow delta.

/* Example:
GOT DELTA: {"leds":{"1":{"r":0,"b":64,"g":64},"2":{"r":64,"b":0,"g":0},"3":{"r":0,"b":64,"g":64},"4":{"r":0,"b":64,"g":64}}}
*/
static void delta_cb(int ev, void *ev_data, void *userdata)
{
    struct mg_str *delta = (struct mg_str *)ev_data;
    blink_led();
    LOG(LL_INFO, ("delta callback: len: %d delta: '%s'", (int)delta->len, delta->p));
    int ret = json_walk(delta->p, (int)delta->len, j_cb, NULL);
    LOG(LL_INFO, ("Scanned delta and found %d entries", ret));
    sync_and_show(100);
    (void)ev;
    (void)userdata;
}

enum mgos_app_init_result mgos_app_init(void)
{
    LOG(LL_INFO, ("Starting up. Software "));
    LOG(LL_INFO, ("My device ID is: %s", mgos_sys_config_get_device_id())); // Get config param
    LOG(LL_INFO, ("Initializing. Using pin %i for blinking", mgos_sys_config_get_pins_statusLed()));

    pixels = (struct pixel *)calloc(mgos_sys_config_get_leds_number(), sizeof(struct pixel)); // Allocate pixel array

    mgos_gpio_set_mode(mgos_sys_config_get_pins_statusLed(), MGOS_GPIO_MODE_OUTPUT);
    LOG(LL_INFO, ("LED strip on PIN %i", mgos_sys_config_get_pins_ledStrip()));
    my_strip = mgos_neopixel_create(mgos_sys_config_get_pins_ledStrip(), mgos_sys_config_get_leds_number(), MGOS_NEOPIXEL_ORDER_GRB);

    mgos_event_add_handler(MGOS_SHADOW_UPDATE_DELTA, delta_cb, NULL);
    mgos_event_add_handler(MGOS_SHADOW_CONNECTED, connected_cb, NULL);
    LOG(LL_INFO, ("Dont with init. Blinking LEDs"));
    blink_led();
    init_animation();
    LOG(LL_INFO, ("Init hopefully successful"));

    return MGOS_APP_INIT_SUCCESS;
}
