#include <string.h>

/* ----- Local Includes ----------------------------------------------------- */

#include "user_interface.h"

#include "configuration.h"

#include "app_task_ids.h"
#include "app_tasks.h"

#include "app_events.h"
#include "app_signals.h"
#include "app_times.h"
#include "app_version.h"
#include "event_subscribe.h"
#include "hal_uuid.h"
#include "hal_uart.h"

/* ----- Private Function Declaration --------------------------------------- */

PRIVATE void
user_interface_eui_callback( uint8_t link, eui_interface_t *interface, uint8_t message );

PRIVATE void user_interface_tx_put_external(uint8_t *c, uint16_t length );
PRIVATE void user_interface_eui_callback_external(uint8_t message );

PRIVATE void user_interface_tx_put_internal( uint8_t *c, uint16_t length );
PRIVATE void user_interface_eui_callback_internal( uint8_t message );

PRIVATE void user_interface_tx_put_module( uint8_t *c, uint16_t length );
PRIVATE void user_interface_eui_callback_module( uint8_t message );

/* ----- Private Function Declaration --------------------------------------- */

PRIVATE void start_mech_cb( void );
PRIVATE void stop_mech_cb( void );
PRIVATE void emergency_stop_cb( void );
PRIVATE void home_mech_cb( void );
PRIVATE void execute_motion_queue( void );
PRIVATE void clear_all_queue( void );
PRIVATE void tracked_position_event( void );
#ifdef EXPANSION_SERVO
PRIVATE void tracked_external_servo_request( void );
#endif

PRIVATE void rgb_manual_led_event( void );
PRIVATE void movement_generate_event( void );
PRIVATE void lighting_generate_event( void );
PRIVATE void sync_begin_queues( void );
PRIVATE void trigger_camera_capture( void );

/* ----- Defines ----------------------------------------------------------- */

SystemData_t     sys_stats;
Task_Info_t      task_info[TASK_MAX] = { 0 };
KinematicsInfo_t mechanical_info;

FanData_t  fan_stats;

uint8_t fan_manual_setpoint = 0;
uint8_t fan_manual_enable   = 0;


TempData_t temp_sensors;

SystemStates_t sys_states;
QueueDepths_t  queue_data;

MotionData_t motion_global;
#ifdef EXPANSION_SERVO
MotorData_t motion_servo[4];
float external_servo_angle_target;
#else
MotorData_t  motion_servo[3];

#endif

Movement_t       motion_inbound;
CartesianPoint_t current_position;    //global position of end effector in cartesian space
CartesianPoint_t target_position;

LedState_t    rgb_led_drive;
LedControl_t  rgb_manual_control;
Fade_t        light_fade_inbound;


char device_nickname[16] = "Zaphod Beeblebot";
char reset_cause[20]     = "No Reset Cause";



uint16_t     sync_id_val  = 0;
uint8_t      mode_request = 0;

uint32_t camera_shutter_duration_ms = 0;

eui_message_t ui_variables[] = {
        // Higher level system setup information
        EUI_CHAR_ARRAY_RO( "name", device_nickname ),
        EUI_CHAR_ARRAY_RO( "reset_type", reset_cause ),
        EUI_CUSTOM( "sys", sys_stats ),
        EUI_CUSTOM( "super", sys_states ),
//        EUI_CUSTOM( "fwb", fw_info ),
        EUI_CUSTOM( "tasks", task_info ),
        EUI_CUSTOM_RO( "kinematics", mechanical_info ),

        // Temperature and cooling system
        EUI_CUSTOM( "fan", fan_stats ),
//        EUI_CUSTOM( "curve", fan_curve ),
        EUI_CUSTOM( "temp", temp_sensors ),

        EUI_UINT8( "fan_man_speed", fan_manual_setpoint ),
        EUI_UINT8( "fan_manual_en", fan_manual_enable ),

        // motion related information
        EUI_CUSTOM_RO( "queue", queue_data ),

        EUI_CUSTOM_RO( "moStat", motion_global ),
        EUI_CUSTOM_RO( "servo", motion_servo ),

//        EUI_CUSTOM( "pwr_cal", power_trims ),
        EUI_CUSTOM_RO( "rgb", rgb_led_drive ),
        EUI_CUSTOM( "hsv", rgb_manual_control ),
//        EUI_CUSTOM( "ledset", rgb_led_settings ),

        //inbound movement buffer and 'add to queue' callback
        EUI_CUSTOM( "inlt", light_fade_inbound ),
        EUI_CUSTOM( "inmv", motion_inbound ),

        EUI_FUNC( "stmv", execute_motion_queue ),
        EUI_FUNC( "clmv", clear_all_queue ),
        EUI_FUNC( "sync", sync_begin_queues ),
        EUI_UINT16( "syncid", sync_id_val ),

        EUI_INT32_ARRAY( "tpos", target_position ),
        EUI_INT32_ARRAY_RO( "cpos", current_position ),

#ifdef EXPANSION_SERVO
        EUI_FLOAT( "exp_ang", external_servo_angle_target),
#endif

        // Event trigger callbacks
        EUI_FUNC( "estop", emergency_stop_cb ),
        EUI_FUNC( "arm", start_mech_cb ),
        EUI_FUNC( "disarm", stop_mech_cb ),
        EUI_FUNC( "home", home_mech_cb ),

        // UI requests a change of operating mode
        EUI_UINT8( "req_mode", mode_request ),

//        EUI_FLOAT( "rotZ", z_rotation ),
        EUI_UINT32( "capture", camera_shutter_duration_ms ),

};

enum
{
    LINK_MODULE = 0,
    LINK_INTERNAL,
    LINK_EXTERNAL,
} EUI_LINK_NAMES;

eui_interface_t communication_interface[] = {
        EUI_INTERFACE_CB( &user_interface_tx_put_module, &user_interface_eui_callback_module ),
        EUI_INTERFACE_CB( &user_interface_tx_put_internal, &user_interface_eui_callback_internal ),
        EUI_INTERFACE_CB(&user_interface_tx_put_external, &user_interface_eui_callback_external ),
};

/* ----- Public Functions --------------------------------------------------- */

PUBLIC void
user_interface_init( void )
{
    hal_uart_init( HAL_UART_PORT_MODULE );
    // TODO init other serial ports for UI use?

    EUI_LINK( communication_interface );
    EUI_TRACK( ui_variables );
    eui_setup_identifier( (char *)HAL_UUID, 12 );    //header byte is 96-bit, therefore 12-bytes
}

PUBLIC void
user_interface_handle_data( void )
{
    while( hal_uart_rx_data_available( HAL_UART_PORT_MODULE ) )
    {
        eui_parse( hal_uart_rx_get( HAL_UART_PORT_MODULE ), &communication_interface[LINK_MODULE] );
    }

    // TODO handle other communication link serial FIFO
}

/* -------------------------------------------------------------------------- */

PRIVATE void
user_interface_tx_put_external(uint8_t *c, uint16_t length )
{
    hal_uart_write( HAL_UART_PORT_EXTERNAL, c, length );
}

PRIVATE void
user_interface_eui_callback_external(uint8_t message )
{
    user_interface_eui_callback( LINK_EXTERNAL, &communication_interface[LINK_EXTERNAL], message );
}

/* -------------------------------------------------------------------------- */

PRIVATE void
user_interface_tx_put_internal( uint8_t *c, uint16_t length )
{
    hal_uart_write( HAL_UART_PORT_INTERNAL, c, length );
}

PRIVATE void
user_interface_eui_callback_internal( uint8_t message )
{
    user_interface_eui_callback( LINK_INTERNAL, &communication_interface[LINK_INTERNAL], message );
}

/* -------------------------------------------------------------------------- */

PRIVATE void
user_interface_tx_put_module( uint8_t *c, uint16_t length )
{
    hal_uart_write( HAL_UART_PORT_MODULE, c, length );
}

PRIVATE void
user_interface_eui_callback_module( uint8_t message )
{
    user_interface_eui_callback( LINK_MODULE, &communication_interface[LINK_MODULE], message );
}

/* -------------------------------------------------------------------------- */

PRIVATE void
user_interface_eui_callback( uint8_t link, eui_interface_t *interface, uint8_t message )
{
    // Provided the callbacks - use this to fire callbacks when a variable changes etc
    switch( message )
    {
        case EUI_CB_TRACKED: {
            // UI received a tracked message ID and has completed processing
            eui_header_t header  = interface->packet.header;
            void *       payload = interface->packet.data_in;
            uint8_t *    name_rx = interface->packet.id_in;

            // See if the inbound packet name matches our intended variable
            if( strcmp( (char *)name_rx, "req_mode" ) == 0 )
            {

                // Fire an event to the supervisor to change mode
                switch( mode_request )
                {
                    case CONTROL_NONE:
                        // TODO allow UI to request a no-mode setting?
                        break;
                    case CONTROL_MANUAL:
                        eventPublish( EVENT_NEW( StateEvent, MODE_MANUAL ) );
                        break;
                    case CONTROL_EVENT:
                        eventPublish( EVENT_NEW( StateEvent, MODE_EVENT ) );
                        break;
                    case CONTROL_DEMO:
                        eventPublish( EVENT_NEW( StateEvent, MODE_DEMO ) );
                        break;
                    case CONTROL_TRACK:
                        eventPublish( EVENT_NEW( StateEvent, MODE_TRACK ) );
                        break;

                    default:
                        // Punish an incorrect attempt at mode changes with E-STOP
                        eventPublish( EVENT_NEW( StateEvent, MOTION_EMERGENCY ) );
                        break;
                }
            }

            if( strcmp( (char *)name_rx, "inmv" ) == 0 && header.data_len )
            {
                movement_generate_event();
            }

            if( strcmp( (char *)name_rx, "inlt" ) == 0 && header.data_len )
            {
                lighting_generate_event();
            }

            if( strcmp( (char *)name_rx, "tpos" ) == 0 && header.data_len )
            {
                tracked_position_event();
            }

#ifdef EXPANSION_SERVO
            if( strcmp( (char *)name_rx, "exp_ang" ) == 0 && header.data_len )
            {
                tracked_external_servo_request();
            }
#endif

            if( ( strcmp( (char *)name_rx, "hsv" ) == 0 || strcmp( (char *)name_rx, "ledset" ) == 0 )
                && header.data_len )
            {
                rgb_manual_led_event();
            }

            if( strcmp( (char *)name_rx, "capture" ) == 0 && header.data_len )
            {
                trigger_camera_capture();
            }

            break;
        }

        case EUI_CB_UNTRACKED:
            // UI passed in an untracked message ID

            break;

        case EUI_CB_PARSE_FAIL:
            // Inbound message parsing failed, this callback help while debugging

            break;

        default:

            break;
    }
}

/* -------------------------------------------------------------------------- */

PUBLIC void
user_interface_set_reset_cause( const char *reset_description )
{
    memset( &reset_cause, 0, sizeof( reset_cause ) );
    strcpy( (char *)&reset_cause, reset_description );
}

/* -------------------------------------------------------------------------- */

PUBLIC void
user_interface_report_error( char *error_string )
{
    // Send the text to the UI for display to user
    eui_message_t err_message = { .id   = "err",
            .type = TYPE_CHAR,
            .size = strlen( error_string ),
            { .data = error_string } };

    eui_send_untracked( &err_message );
}

/* -------------------------------------------------------------------------- */

// System Statistics and Settings

PUBLIC void
user_interface_set_cpu_load( uint8_t percent )
{
    sys_stats.cpu_load = percent;
}

PUBLIC void
user_interface_set_cpu_clock( uint32_t clock )
{
    sys_stats.cpu_clock = clock / 1000000;    //convert to Mhz
}

PUBLIC void
user_interface_update_task_statistics( void )
{
    for( uint8_t id = ( TASK_MAX - 1 ); id > 0; id-- )
    {
        StateTask *t = app_task_by_id( id );
        if( t )
        {
            task_info[id].id          = t->id;
            task_info[id].ready       = t->ready;
            task_info[id].queue_used  = t->eventQueue.used;
            task_info[id].queue_max   = t->eventQueue.max;
            task_info[id].waiting_max = t->waiting_max;
            task_info[id].burst_max   = t->burst_max;

            memset( &task_info[id].name, 0, sizeof( task_info[0].name ) );
            strcpy( (char *)&task_info[id].name, t->name );
        }
    }
    //app_task_clear_statistics();
}

/* -------------------------------------------------------------------------- */

PUBLIC void
user_interface_set_sensors_enabled( bool enable )
{
    sys_stats.sensors_enable = enable;
}

void user_interface_set_module_enable( bool enabled )
{
    sys_stats.module_enable = enabled;
}

PUBLIC void
user_interface_set_input_voltage( float voltage )
{
    sys_stats.input_voltage = voltage;
}

/* -------------------------------------------------------------------------- */

PUBLIC void
user_interface_set_main_state( uint8_t state )
{
    sys_states.supervisor = state;
    sys_states.motors     = motion_servo[0].enabled || motion_servo[1].enabled || motion_servo[2].enabled;
    eui_send_tracked( "super" );
}

PUBLIC void
user_interface_set_control_mode( uint8_t mode )
{
    sys_states.control_mode = mode;
    eui_send_tracked( "super" );
}

/* -------------------------------------------------------------------------- */

PUBLIC void
user_interface_set_kinematics_mechanism_info( float shoulder_radius, float bicep_len, float forearm_len, float effector_radius )
{
    mechanical_info.shoulder_radius_micron = shoulder_radius;
    mechanical_info.bicep_length_micron    = bicep_len;
    mechanical_info.forearm_length_micron  = forearm_len;
    mechanical_info.effector_radius_micron = effector_radius;
}

PUBLIC void
user_interface_set_kinematics_limits( int32_t radius, int32_t zmin, int32_t zmax )
{
    mechanical_info.limit_radius = radius;
    mechanical_info.limit_z_min  = zmin;
    mechanical_info.limit_z_max  = zmax;
}

PUBLIC void
user_interface_set_kinematics_flips( int8_t x, int8_t y, int8_t z )
{
    mechanical_info.flip_x = x;
    mechanical_info.flip_y = y;
    mechanical_info.flip_z = z;
}

/* -------------------------------------------------------------------------- */

PUBLIC bool
user_interface_get_fan_manual_control()
{
    return ( fan_manual_enable > 0 );
}

PUBLIC uint8_t
user_interface_get_fan_target( void )
{
    return fan_manual_setpoint;
}

PUBLIC void
user_interface_set_fan_percentage( uint8_t percent )
{
    fan_stats.setpoint_percentage = percent;
}

PUBLIC void
user_interface_set_fan_rpm( uint16_t rpm )
{
    fan_stats.speed_rpm = rpm;
}

PUBLIC void
user_interface_set_fan_state( uint8_t state )
{
    fan_stats.state = state;
}

PUBLIC FanCurve_t *
user_interface_get_fan_curve_ptr( void )
{
    // TODO fix fan curve UI control/viewing
    //    if( DIM( fan_curve ) == NUM_FAN_CURVE_POINTS )
//    {
//        return &fan_curve[0];
//    }

    return 0;
}

/* -------------------------------------------------------------------------- */

PUBLIC void
user_interface_set_temp_ambient( float temp )
{
    temp_sensors.pcb_ambient = temp;
}

PUBLIC void
user_interface_set_temp_regulator( float temp )
{
    temp_sensors.pcb_regulator = temp;
}

PUBLIC void
user_interface_set_temp_external( float temp )
{
    temp_sensors.external_probe = temp;
}

PUBLIC void
user_interface_set_temp_cpu( float temp )
{
    temp_sensors.cpu_temp = temp;
}

/* -------------------------------------------------------------------------- */

PUBLIC void
user_interface_set_position( int32_t x, int32_t y, int32_t z )
{
    current_position.x = x;
    current_position.y = y;
    current_position.z = z;
}

PUBLIC CartesianPoint_t
user_interface_get_tracking_target()
{
    return target_position;
}

PUBLIC void
user_interface_reset_tracking_target()
{
    target_position.x = 0;
    target_position.y = 0;
    target_position.z = 0;

    eui_send_tracked( "tpos" );    // tell the UI that the value has changed
}

PUBLIC void
user_interface_set_movement_data( uint8_t move_id, uint8_t move_type, uint8_t progress )
{
    motion_global.movement_identifier = move_id;
    motion_global.profile_type        = move_type;
    motion_global.move_progress       = progress;
}

PUBLIC void
user_interface_set_pathing_status( uint8_t status )
{
    motion_global.pathing_state = status;
}

PUBLIC void
user_interface_set_motion_state( uint8_t status )
{
    motion_global.motion_state = status;
}

PUBLIC void
user_interface_set_motion_queue_depth( uint8_t utilisation )
{
    queue_data.movements = utilisation;
    //    eui_send_tracked("queue");
}

/* -------------------------------------------------------------------------- */

PUBLIC void
user_interface_motor_enable( uint8_t servo, bool enable )
{
    motion_servo[servo].enabled = enable;
}

PUBLIC void
user_interface_motor_state( uint8_t servo, uint8_t state )
{
    motion_servo[servo].state = state;
}

PUBLIC void
user_interface_motor_feedback( uint8_t servo, float percentage )
{
    motion_servo[servo].feedback = percentage * 10;
}

PUBLIC void
user_interface_motor_power( uint8_t servo, float watts )
{
    motion_servo[servo].power = watts;
}

PUBLIC void
user_interface_motor_target_angle( uint8_t servo, float angle )
{
    motion_servo[servo].target_angle = angle;
}

/* -------------------------------------------------------------------------- */

PUBLIC void
user_interface_set_led_status( uint8_t enabled )
{
    rgb_led_drive.enable = enabled;
}

PUBLIC void
user_interface_set_led_values( uint16_t red, uint16_t green, uint16_t blue )
{
    rgb_led_drive.red   = red;
    rgb_led_drive.green = green;
    rgb_led_drive.blue  = blue;
}

PUBLIC void
user_interface_set_led_queue_depth( uint8_t utilisation )
{
    queue_data.lighting = utilisation;
    //    eui_send_tracked("queue");
}

PUBLIC void
user_interface_get_led_manual( float *h, float *s, float *l, uint8_t *en )
{
    *h  = rgb_manual_control.hue;
    *s  = rgb_manual_control.saturation;
    *l  = rgb_manual_control.lightness;
    *en = rgb_manual_control.enable;
}

PRIVATE void
rgb_manual_led_event()
{
    LightingManualEvent *colour_request = EVENT_NEW( LightingManualEvent, LED_MANUAL_SET );

    if( colour_request )
    {
        colour_request->colour.hue        = rgb_manual_control.hue;
        colour_request->colour.saturation = rgb_manual_control.saturation;
        colour_request->colour.intensity  = rgb_manual_control.lightness;
        colour_request->enabled           = rgb_manual_control.enable;

        eventPublish( (StateEvent *)colour_request );
    }
}

/* ----- Private Functions -------------------------------------------------- */

PRIVATE void start_mech_cb( void )
{
    eventPublish( EVENT_NEW( StateEvent, MECHANISM_START ) );
}

/* -------------------------------------------------------------------------- */

PRIVATE void stop_mech_cb( void )
{
    eventPublish( EVENT_NEW( StateEvent, MECHANISM_STOP ) );
}

/* -------------------------------------------------------------------------- */

PRIVATE void emergency_stop_cb( void )
{
    eventPublish( EVENT_NEW( StateEvent, MOTION_EMERGENCY ) );
}

/* -------------------------------------------------------------------------- */

PRIVATE void home_mech_cb( void )
{
    eventPublish( EVENT_NEW( StateEvent, MECHANISM_REHOME ) );
}

/* -------------------------------------------------------------------------- */

PRIVATE void movement_generate_event( void )
{
    MotionPlannerEvent *motion_request = EVENT_NEW( MotionPlannerEvent, MOVEMENT_REQUEST );

    if( motion_request )
    {
        memcpy( &motion_request->move, &motion_inbound, sizeof( motion_inbound ) );
        eventPublish( (StateEvent *)motion_request );
        memset( &motion_inbound, 0, sizeof( motion_inbound ) );
    }
}

PRIVATE void execute_motion_queue( void )
{
    eventPublish( EVENT_NEW( StateEvent, MOTION_QUEUE_START ) );
}

PRIVATE void clear_all_queue( void )
{
    eventPublish( EVENT_NEW( StateEvent, MOTION_QUEUE_CLEAR ) );
    eventPublish( EVENT_NEW( StateEvent, LED_CLEAR_QUEUE ) );
}

PRIVATE void tracked_position_event( void )
{
    TrackedPositionRequestEvent *position_request = EVENT_NEW( TrackedPositionRequestEvent, TRACKED_TARGET_REQUEST );

    if( position_request )
    {
        memcpy( &position_request->target, &target_position, sizeof( target_position ) );
        eventPublish( (StateEvent *)position_request );
        memset( &target_position, 0, sizeof( target_position ) );
    }
}

#ifdef EXPANSION_SERVO
PRIVATE void tracked_external_servo_request( void )
{
    ExpansionServoRequestEvent *angle_request = EVENT_NEW( ExpansionServoRequestEvent, TRACKED_EXTERNAL_SERVO_REQUEST );

    if( angle_request )
    {
        memcpy( &angle_request->target, &external_servo_angle_target, sizeof( external_servo_angle_target ) );
        eventPublish( (StateEvent *)angle_request );
        memset( &external_servo_angle_target, 0, sizeof( external_servo_angle_target ) );
    }
}
#endif

/* -------------------------------------------------------------------------- */

PRIVATE void lighting_generate_event( void )
{
    LightingPlannerEvent *lighting_request = EVENT_NEW( LightingPlannerEvent, LED_QUEUE_ADD );

    if( lighting_request )
    {
        memcpy( &lighting_request->animation, &light_fade_inbound, sizeof( light_fade_inbound ) );
        eventPublish( (StateEvent *)lighting_request );
        memset( &light_fade_inbound, 0, sizeof( light_fade_inbound ) );
    }
}

/* -------------------------------------------------------------------------- */

PRIVATE void sync_begin_queues( void )
{
    BarrierSyncEvent *barrier_ev = EVENT_NEW( BarrierSyncEvent, START_QUEUE_SYNC );

    if( barrier_ev )
    {
        memcpy( &barrier_ev->id, &sync_id_val, sizeof( sync_id_val ) );
        eventPublish( (StateEvent *)barrier_ev );
        memset( &sync_id_val, 0, sizeof( sync_id_val ) );
    }
}

/* -------------------------------------------------------------------------- */
PRIVATE void
trigger_camera_capture( void )
{
    CameraShutterEvent *trigger = EVENT_NEW( CameraShutterEvent, CAMERA_CAPTURE );

    if( trigger )
    {
        memcpy( &trigger->exposure_time, &camera_shutter_duration_ms, sizeof( camera_shutter_duration_ms ) );
        eventPublish( (StateEvent *)trigger );
        memset( &camera_shutter_duration_ms, 0, sizeof( camera_shutter_duration_ms ) );
    }
}


/* ----- End ---------------------------------------------------------------- */
