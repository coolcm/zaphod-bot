/* ----- System Includes ---------------------------------------------------- */

/* ----- Local Includes ----------------------------------------------------- */

#include "app_background.h"
#include "app_times.h"
#include "global.h"
#include "timer_ms.h"

#include "button.h"
#include "buzzer.h"
#include "clearpath.h"
#include "fan.h"
#include "hal_adc.h"
#include "hal_system_speed.h"
#include "led_interpolator.h"
#include "path_interpolator.h"
#include "sensors.h"
#include "shutter_release.h"
#include "status.h"

#include "user_interface.h"

/* -------------------------------------------------------------------------- */

PRIVATE timer_ms_t button_timer = 0;
PRIVATE timer_ms_t buzzer_timer = 0;
PRIVATE timer_ms_t fan_timer    = 0;
PRIVATE timer_ms_t adc_timer    = 0;

/* -------------------------------------------------------------------------- */

PUBLIC void
app_background_init( void )
{
    timer_ms_start( &button_timer, BACKGROUND_RATE_BUTTON_MS );
    timer_ms_start( &buzzer_timer, BACKGROUND_RATE_BUZZER_MS );
    timer_ms_start( &fan_timer, FAN_EVALUATE_TIME );
    timer_ms_start( &adc_timer, BACKGROUND_ADC_AVG_POLL_MS );    //refresh ADC readings
}

/* -------------------------------------------------------------------------- */

PUBLIC void
app_background( void )
{
    //rate limit less important background processes
    if( timer_ms_is_expired( &button_timer ) )
    {
        // Need to turn the E-Stop light on to power the pullup for the E-STOP button
        status_external_override( true );
        button_process();
        status_external_resume();

        timer_ms_start( &button_timer, BACKGROUND_RATE_BUTTON_MS );
    }

    if( timer_ms_is_expired( &buzzer_timer ) )
    {
        buzzer_process();
        timer_ms_start( &buzzer_timer, BACKGROUND_RATE_BUZZER_MS );
    }

    if( timer_ms_is_expired( &fan_timer ) )
    {
        fan_process();
        timer_ms_start( &fan_timer, FAN_EVALUATE_TIME );
    }

    if( timer_ms_is_expired( &adc_timer ) )
    {
        sensors_12v_regulator_C();
        sensors_ambient_C();
        sensors_expansion_C();
        sensors_microcontroller_C();
        sensors_input_V();

        user_interface_set_cpu_load( hal_system_speed_get_load() );
        user_interface_set_cpu_clock( hal_system_speed_get_speed() );    // todo only update this value if it changes
        user_interface_update_task_statistics();

        timer_ms_start( &adc_timer, BACKGROUND_ADC_AVG_POLL_MS );
    }

    shutter_process();
    led_interpolator_process();

    //process any running movements and allow servo drivers to process commands
    path_interpolator_process();

    for( ClearpathServoInstance_t servo = _CLEARPATH_1; servo < _NUMBER_CLEARPATH_SERVOS; servo++ )
    {
        servo_process( servo );
    }
}

/* ----- End ---------------------------------------------------------------- */
