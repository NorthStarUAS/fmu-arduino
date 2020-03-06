/* Binary I/O section: generial info ...
 * Packets start with two bytes ... START_OF_MSG0 and START_OF_MSG1
 * Following that is the packet ID
 * Following that is the packet data size (not including start bytes or check sum, just the data)
 * Following that is the actual data packet
 * Following that is a two byte check sum.  The check sum includes the packet id and size as well as the data.
 */

#include "src/util/serial_link.h"
#include "aura3_messages.h"


bool parse_message_bin( byte id, byte *buf, byte message_size )
{
    bool result = false;

    // Serial.print("message id = "); Serial.print(id); Serial.print(" len = "); Serial.println(message_size);
    
    if ( id == message::command_inceptors_id ) {
        static message::command_inceptors_t inceptors;
        inceptors.unpack(buf, message_size);
        if ( message_size == inceptors.len ) {
            // autopilot_norm uses the same channel mapping as sbus_norm,
            // so map ap_tmp values to their correct places in
            // autopilot_norm
            autopilot_norm[0] = receiver_norm[0]; // auto/manual switch
            autopilot_norm[1] = receiver_norm[1]; // throttle enable
            autopilot_norm[2] = inceptors.channel[0];        // throttle
            autopilot_norm[3] = inceptors.channel[1];
            autopilot_norm[4] = inceptors.channel[2];
            autopilot_norm[5] = inceptors.channel[3];
            autopilot_norm[6] = inceptors.channel[4];
            autopilot_norm[7] = inceptors.channel[5];

            if ( receiver_norm[0] > 0.0 ) {
                // autopilot mode active (determined elsewhere when each
                // new receiver frame is ready) mix the inputs and write
                // the actuator outputs now
                sas_update( autopilot_norm );
                mixing_update( autopilot_norm );
                pwm_update();
            } else {
                // manual mode, do nothing with actuator commands from the
                // autopilot
            }
            result = true;
        }
    } else if ( id == message::config_master_id ) {
        config_master.unpack(buf, message_size);
        if ( message_size == config_master.len ) {
            Serial.println("received master config");
            config_write_eeprom();
            write_ack_bin( id, 0 );
            result = true;
        }
    } else if ( id == message::config_imu_id ) {
        config_imu.unpack(buf, message_size);
        if ( message_size == config_imu.len ) {
            Serial.println("received imu config");
            config_write_eeprom();
            write_ack_bin( id, 0 );
            result = true;
        }
    } else if ( id == message::config_actuators_id ) {
        config_actuators.unpack(buf, message_size);
        if ( message_size == config_actuators.len ) {
            Serial.println("received new actuator config");
            pwm_setup();  // update pwm config in case it has been changed.
            config_write_eeprom();
            write_ack_bin( id, 0 );
            result = true;
        }
    } else if ( id == message::config_airdata_id ) {
        config_airdata.unpack(buf, message_size);
        if ( message_size == config_airdata.len ) {
            Serial.println("received new airdata config");
            config_write_eeprom();
            write_ack_bin( id, 0 );
            result = true;
        }
    } else if ( id == message::config_power_id ) {
        config_power.unpack(buf, message_size);
        if ( message_size == config_power.len ) {
            Serial.println("received new power config");
            config_write_eeprom();
            write_ack_bin( id, 0 );
            result = true;
        }
    } else if ( id == message::config_led_id ) {
        config_led.unpack(buf, message_size);
        if ( message_size == config_led.len ) {
            Serial.println("received new led config");
            config_write_eeprom();
            write_ack_bin( id, 0 );
            result = true;
        }
    } else if ( id == message::command_zero_gyros_id && message_size == 0 ) {
        Serial.println("received zero gyros command");
        gyros_calibrated = 0;   // start state
        write_ack_bin( id, 0 );
        result = true;
    } else {
        // Serial.print("unknown message id = "); Serial.print(id); Serial.print(" len = "); Serial.println(message_size);
    }
    return result;
}


/* output an acknowledgement of a message received */
int write_ack_bin( uint8_t command_id, uint8_t subcommand_id )
{
    static message::command_ack_t ack;
    ack.command_id = command_id;
    ack.subcommand_id = subcommand_id;
    ack.pack();
    return serial.write_packet( ack.id, ack.payload, ack.len);
}


/* output a binary representation of the pilot (rc receiver) data */
int write_pilot_in_bin()
{
    static message::pilot_t pilot;

    if (message::sbus_channels > SBUS_CHANNELS) {
        return 0;
    }
    
    // receiver data
    for ( int i = 0; i < message::sbus_channels; i++ ) {
        pilot.channel[i] = receiver_norm[i];
    }

    // flags
    pilot.flags = receiver_flags;
    
    pilot.pack();
    return serial.write_packet( pilot.id, pilot.payload, pilot.len);
}

void write_pilot_in_ascii()
{
    // pilot (receiver) input data
    if ( receiver_flags & SBUS_FAILSAFE ) {
        Serial.print("FAILSAFE! ");
    }
    if ( receiver_norm[0] < 0 ) {
        Serial.print("(Manual) ");
    } else {
        Serial.print("(Auto) ");
    }
    if ( receiver_norm[1] < 0 ) {
        Serial.print("(Throttle safety) ");
    } else {
        Serial.print("(Throttle enable) ");
    }
    for ( int i = 0; i < 8; i++ ) {
        Serial.print(receiver_norm[i], 3);
        Serial.print(" ");
    }
    Serial.println();
}

void write_actuator_out_ascii()
{
    // actuator output
    Serial.print("RCOUT:");
    for ( int i = 0; i < PWM_CHANNELS; i++ ) {
        Serial.print(actuator_pwm[i]);
        Serial.print(" ");
    }
    Serial.println();
}

/* output a binary representation of the IMU data (note: scaled to 16bit values) */
int write_imu_bin()
{
    const float _pi = 3.14159265358979323846;
    const float _g = 9.807;
    const float _d2r = _pi / 180.0;
    
    const float _gyro_lsb_per_dps = 32767.5 / 500;  // -500 to +500 spread across 65535
    const float gyroScale = _d2r / _gyro_lsb_per_dps;
    
    const float _accel_lsb_per_dps = 32767.5 / 8;   // -4g to +4g spread across 65535
    const float accelScale = _g / _accel_lsb_per_dps;

    const float magScale = 0.01;
    const float tempScale = 0.01;
    
    static message::imu_raw_t imu;
    imu.micros = micros_node->getLongLong();
    imu.channel[0] = ax_node->getFloat() / accelScale;
    imu.channel[1] = ay_node->getFloat() / accelScale;
    imu.channel[2] = az_node->getFloat() / accelScale;
    imu.channel[3] = p_node->getFloat() / gyroScale;
    imu.channel[4] = q_node->getFloat() / gyroScale;
    imu.channel[5] = r_node->getFloat() / gyroScale;
    imu.channel[6] = hx_node->getFloat() / magScale;
    imu.channel[7] = hy_node->getFloat() / magScale;
    imu.channel[8] = hz_node->getFloat() / magScale;
    imu.channel[0] = temp_node->getFloat() / tempScale;
    imu.pack();
    return serial.write_packet( imu.id, imu.payload, imu.len );
}

void write_imu_ascii()
{
    // output imu data
    Serial.print("IMU: ");
    Serial.print(micros_node->getLong()); Serial.print(" ");
    Serial.print(p_node->getFloat(), 3); Serial.print(" ");
    Serial.print(q_node->getFloat(), 3); Serial.print(" ");
    Serial.print(r_node->getFloat(), 3); Serial.print(" ");
    Serial.print(ax_node->getFloat(), 3); Serial.print(" ");
    Serial.print(ay_node->getFloat(), 3); Serial.print(" ");
    Serial.print(az_node->getFloat(), 3); Serial.print(" ");
    Serial.print(temp_node->getFloat(), 3);
    Serial.println();
}

/* output a binary representation of the GPS data */
int write_gps_bin()
{
    byte size = sizeof(gps_data);

    if ( !new_gps_data ) {
        return 0;
    } else {
        new_gps_data = false;
    }

    return serial.write_packet( message::aura_nav_pvt_id, (uint8_t *)&gps_data, size );
}

void write_gps_ascii() {
    Serial.print("GPS:");
    Serial.print(" Lat:");
    Serial.print((double)gps_data.lat / 10000000.0, 7);
    //Serial.print(gps_data.lat);
    Serial.print(" Lon:");
    Serial.print((double)gps_data.lon / 10000000.0, 7);
    //Serial.print(gps_data.lon);
    Serial.print(" Alt:");
    Serial.print((float)gps_data.hMSL / 1000.0);
    Serial.print(" Vel:");
    Serial.print(gps_data.velN / 1000.0);
    Serial.print(", ");
    Serial.print(gps_data.velE / 1000.0);
    Serial.print(", ");
    Serial.print(gps_data.velD / 1000.0);
    Serial.print(" GSP:");
    Serial.print(gps_data.gSpeed, DEC);
    Serial.print(" COG:");
    Serial.print(gps_data.heading, DEC);
    Serial.print(" SAT:");
    Serial.print(gps_data.numSV, DEC);
    Serial.print(" FIX:");
    Serial.print(gps_data.fixType, DEC);
    Serial.print(" TIM:");
    Serial.print(gps_data.hour); Serial.print(':');
    Serial.print(gps_data.min); Serial.print(':');
    Serial.print(gps_data.sec);
    Serial.print(" DATE:");
    Serial.print(gps_data.month); Serial.print('/');
    Serial.print(gps_data.day); Serial.print('/');
    Serial.print(gps_data.year);
    Serial.println();
}

/* output a binary representation of the barometer data */
int write_airdata_bin()
{
    static message::airdata_t airdata;
    airdata.baro_press_pa = baro_press;
    airdata.baro_temp_C = baro_temp;
    airdata.baro_hum = baro_hum;
    airdata.ext_diff_press_pa = airdata_diffPress_pa;
    airdata.ext_static_press_pa = 0.0; // fixme!
    airdata.ext_temp_C = airdata_temp_C;
    airdata.error_count = airdata_error_count;
    airdata.pack();
    return serial.write_packet( airdata.id, airdata.payload, airdata.len );
}

void write_airdata_ascii()
{
    Serial.print("Barometer: ");
    Serial.print(baro_press, 2); Serial.print(" (st pa) ");
    Serial.print(baro_temp, 2); Serial.print(" (C) ");
    Serial.print(baro_hum, 1); Serial.print(" (%RH) ");
    Serial.print("Pitot: ");
    Serial.print(airdata_diffPress_pa, 4); Serial.print(" (diff pa) ");
    Serial.print(airdata_temp_C, 2); Serial.print(" (C) ");
    Serial.print(airdata_error_count); Serial.print(" (errors) ");
    Serial.println();
}

/* output a binary representation of various volt/amp sensors */
int write_power_bin()
{
    static message::power_t power;
    power.int_main_v = pwr1_v;
    power.avionics_v = avionics_v;
    power.ext_main_v = pwr2_v;
    power.ext_main_amp = pwr_a;
    power.pack();
    return serial.write_packet( power.id, power.payload, power.len );
}

void write_power_ascii()
{
    // This info is static so we don't need to send it at a high rate ... once every 10 seconds (?)
    // with an immediate message at the start.
    Serial.print("SN: ");
    Serial.println(read_serial_number());
    Serial.print("Firmware: ");
    Serial.println(FIRMWARE_REV);
    Serial.print("Main loop hz: ");
    Serial.println( MASTER_HZ);
    Serial.print("Baud: ");Serial.println(DEFAULT_BAUD);
    Serial.print("Main v: "); Serial.print(pwr1_v, 2);
    Serial.print(" av: "); Serial.println(avionics_v, 2);
}

/* output a binary representation of various status and config information */
int write_status_info_bin()
{
    static uint32_t write_millis = millis();
    static message::status_t status;

    // This info is static or slow changing so we don't need to send
    // it at a high rate.
    static int counter = 0;
    if ( counter > 0 ) {
        counter--;
        return 0;
    } else {
        counter = MASTER_HZ * 1 - 1; // a message every 1 seconds (-1 so we aren't off by one frame) 
    }

    status.serial_number = serial_number;
    status.firmware_rev = FIRMWARE_REV;
    status.master_hz = MASTER_HZ;
    status.baud = DEFAULT_BAUD;

    // estimate sensor output byte rate
    unsigned long current_time = millis();
    unsigned long elapsed_millis = current_time - write_millis;
    unsigned long byte_rate = output_counter * 1000 / elapsed_millis;
    write_millis = current_time;
    output_counter = 0;
    status.byte_rate = byte_rate;

    status.pack();
    return serial.write_packet( status.id, status.payload, status.len );
}

void write_status_info_ascii()
{
    // This info is static so we don't need to send it at a high rate ... once every 10 seconds (?)
    // with an immediate message at the start.
    Serial.print("SN: ");
    Serial.println(read_serial_number());
    Serial.print("Firmware: ");
    Serial.println(FIRMWARE_REV);
    Serial.print("Main loop hz: ");
    Serial.println( MASTER_HZ);
    Serial.print("Baud: ");Serial.println(DEFAULT_BAUD);
}
