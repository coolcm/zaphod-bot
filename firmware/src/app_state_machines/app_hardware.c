/* ----- System Includes ---------------------------------------------------- */

/* ----- Local Includes ----------------------------------------------------- */

#include "hal_adc.h"
#include "hal_flashmem.h"
#include "hal_gpio.h"
#include "hal_hard_ic.h"
#include "hal_reset.h"
#include "hal_system_speed.h"
#include "hal_systick.h"
#include "hal_uart.h"
#include "hal_watchdog.h"

#include "configuration.h"
#include "user_interface.h"
#include "sensors.h"
#include "buzzer.h"
#include "clearpath.h"
#include "fan.h"
#include "shutter_release.h"
#include "status.h"

/* -------------------------------------------------------------------------- */

PUBLIC void
app_hardware_init( void )
{
    // Configure GPIO pins
    hal_gpio_configure_defaults();

    // Start the watchdog
    hal_watchdog_init( 5000 );

    // Initialise the CPU manager DWT
    hal_system_speed_init();

    // Continue basic I/O setup
    status_green( true );
    status_yellow( true );
    status_red( true );

    hal_systick_init();

    // We need to do a full reset of usart clocks/peripherals on boot, as the DMA setup seems
    // to cling onto error flags across firmware flashing - making debugging hard
    hal_uart_global_deinit();

    hal_flashmem_init();

    hal_adc_init();
    hal_hard_ic_init();

    configuration_init();
    user_interface_init();

    // Check for the cause of the microcontroller booting (errors vs normal power up)
    user_interface_set_reset_cause( hal_reset_cause_description( hal_reset_cause() ) );

    buzzer_init();
    fan_init();
    sensors_init();
    shutter_init();

    //delta main servo motor handlers
    servo_init( _CLEARPATH_1 );
    servo_init( _CLEARPATH_2 );
    servo_init( _CLEARPATH_3 );

#ifdef EXPANSION_SERVO
    servo_init( _CLEARPATH_4 );
#endif
}

/* ----- End ---------------------------------------------------------------- */
