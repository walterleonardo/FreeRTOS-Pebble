/* backlight.c
 * routines for [...]
 * RebbleOS
 *
 * Author: Barry Carter <barry.carter@gmail.com>
 */

#include "rebbleos.h"

static TaskHandle_t _backlight_task;
static xQueueHandle _backlight_queue;
static void _backlight_thread(void *pvParameters);

struct backlight_message_t
{
    uint8_t cmd;
    uint16_t val1;
    uint16_t val2;
} backlight_message;

uint16_t _backlight_brightness;
uint8_t _backlight_is_on;

/*
 * Backlight is a go
 */
void backlight_init(void)
{
    hw_backlight_init();
    
    xTaskCreate(_backlight_thread, "Bl", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2UL, &_backlight_task);    
    
    _backlight_queue = xQueueCreate( 2, sizeof(struct backlight_message_t *));
        
    KERN_LOG("backl", APP_LOG_LEVEL_INFO, "Backlight Tasks Created");
    _backlight_is_on = 0;
    _backlight_brightness = 0;

    backlight_on(100, 3000);
}

// In here goes the functions to dim the backlight
// on a timer

// use the backlight as additional alert by flashing it
void backlight_on(uint16_t brightness_pct, uint16_t time)
{
    struct backlight_message_t *message;
    //  send the queue the backlight on task
    message = &backlight_message;
    message->cmd = BACKLIGHT_ON;
    message->val1 = brightness_pct;
    message->val2 = time;
    xQueueSendToBack(_backlight_queue, &message, 0);
}


void backlight_set(uint16_t brightness_pct)
{
    uint16_t brightness;
    
    brightness = 8499 / (100 / brightness_pct);
    KERN_LOG("backl", APP_LOG_LEVEL_DEBUG, "Brightness %d", brightness);

    backlight_set_raw(brightness);
}

/*
 * Set the backlight. At the moment this is scaled to be 4000 - mid brightness
 */
void backlight_set_raw(uint16_t brightness)
{
    _backlight_brightness = brightness;

    // set the display pwm value
    hw_backlight_set(brightness);
}

void backlight_set_from_ambient(void)
{
    uint16_t amb, bri;
    bri = _backlight_brightness;
    
    backlight_set_raw(0);
    // give the led in the backlight time to de-energise
    delay_us(10);
    amb = ambient_get();
    
    // hacky brightness control here...
    // if amb is near 0, it is dark.
    // amb is 3500ish at about max brightness
    if (amb > 0)
    {
        amb = (8499 / (2 * amb)) + (8499 / 2);
        // restore the backlight
        backlight_set_raw(amb);
        return;
    }
    // restore the backlight
    backlight_set_raw(bri);
}

/*
 * Will take care of dimmng and time light is on etc
 */
void _backlight_thread(void *pvParameters)
{
    struct backlight_message_t *message;
//     const TickType_t xMaxBlockTime = pdMS_TO_TICKS(1000);
    uint8_t wait = 0;
    TickType_t on_expiry_time;
    uint8_t backlight_status;
    uint16_t bri_scale;
    uint16_t bri_it;
    
    while(1)
    {
        if (backlight_status == BACKLIGHT_FADE)
        {
            uint16_t newbri = (_backlight_brightness - bri_scale);
            wait = 10;
            backlight_set_raw(newbri);

            bri_it--;
            
            if (bri_it == 0)
            {
                backlight_status = BACKLIGHT_OFF;
                backlight_set_raw(0);
            }
        }
        else if (backlight_status == BACKLIGHT_ON)
        {
            // set the queue reader to immediately return
            wait = 50;
            
//             backlight_set_from_ambient();
            backlight_set(bri_scale);
            
            if (xTaskGetTickCount() > on_expiry_time)
            {
                backlight_status = BACKLIGHT_FADE;
                bri_scale = _backlight_brightness / 50;
                bri_it = 50; // number of steps
            }
        }
        else
        {
            // We are idle so we can sleep for a bit
            wait = 1000 / portTICK_RATE_MS;
        }
        
        if (xQueueReceive(_backlight_queue, &message, wait))
        {
            switch(message->cmd)
            {
                case BACKLIGHT_FADE:
                    break;
                case BACKLIGHT_OFF:
                    break;
                case BACKLIGHT_ON:
                    KERN_LOG("backl", APP_LOG_LEVEL_DEBUG, "Backlight ON");
                    backlight_status = BACKLIGHT_ON;
                    // timestamp the tick counter so we can stay on for
                    // the right amount of time
                    bri_scale = message->val1;
                    on_expiry_time = xTaskGetTickCount() + (message->val2 / portTICK_RATE_MS);
                    backlight_set(message->val1);
                    break;
            }
        }
    }
}
