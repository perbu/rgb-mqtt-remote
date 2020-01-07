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

bool redOn = 1;
int offset = 0;

struct pixel
{
    int r;
    int g;
    int b;
};

struct pixel *pixels;           // My array of pixels.
struct mgos_neopixel *my_strip; // Low-level led strip.

// When we're connected to the shadow, report our current state.
// This may generate shadow delta on the cloud, and the cloud will push
// it down to us.
static void connected_cb(int ev, void *ev_data, void *userdata)
{
    LOG(LL_INFO, ("Connected to our shadow device..."));

    // We might need to do something silly to trigger a update.
    // mgos_shadow_updatef(0, "{on: %B}", s_light_on); /* Report status */
    (void)ev;
    (void)ev_data;
    (void)userdata;
}

void sync_and_show()
{
    for (int i = 0; i < mgos_sys_config_get_leds_number(); i++)
    {
        LOG(LL_INFO, ("Syncing pixel %d (%d:%d:%d)", i, pixels[i].r, pixels[i].g, pixels[i].b));
        mgos_neopixel_set(my_strip, i, pixels[i].r, pixels[i].g, pixels[i].b);
    }
    LOG(LL_INFO, ("Showing new pixels"));
    mgos_neopixel_show(my_strip);
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
        color = tolower(color);
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

    LOG(LL_INFO, ("delta callback: len: %d delta: '%s'", (int)delta->len, delta->p));
    int ret = json_walk(delta->p, (int)delta->len, j_cb, NULL);
    LOG(LL_INFO, ("Scanned delta and found %d entries", ret));
    sync_and_show();
    (void)ev;
    (void)userdata;
}

enum mgos_app_init_result mgos_app_init(void)
{
    LOG(LL_INFO, ("My device ID is: %s", mgos_sys_config_get_device_id())); // Get config param
    LOG(LL_INFO, ("Initializing. Using pin %i for blinking", mgos_sys_config_get_pins_statusLed()));

    pixels = (struct pixel *)calloc(mgos_sys_config_get_leds_number(), sizeof(pixel)); // Allocate pixel array

    mgos_gpio_set_mode(mgos_sys_config_get_pins_statusLed(), MGOS_GPIO_MODE_OUTPUT);
    LOG(LL_INFO, ("LED strip on PIN %i", mgos_sys_config_get_pins_ledStrip()));
    my_strip = mgos_neopixel_create(mgos_sys_config_get_pins_ledStrip(), mgos_sys_config_get_leds_number(), MGOS_NEOPIXEL_ORDER_GRB);

    mgos_event_add_handler(MGOS_SHADOW_UPDATE_DELTA, delta_cb, NULL);
    mgos_event_add_handler(MGOS_SHADOW_CONNECTED, connected_cb, NULL);

    return MGOS_APP_INIT_SUCCESS;
}
