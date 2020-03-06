#include <Eigen.h>
#include <Eigen/Core>
using namespace Eigen;

#include "src/MPU9250/MPU9250.h"

// IMU full scale ranges, DLPF bandwidth, interrupt SRD, and interrupt pin
const uint8_t MPU9250_SRD = 9;  // Data Output Rate = 1000 / (1 + SRD)

MPU9250 IMU;

static float gx_calib = 0.0;
static float gy_calib = 0.0;
static float gz_calib = 0.0;

// Setup imu defaults:
// Marmot v1 has mpu9250 on SPI CS line 24
// Aura v2 has mpu9250 on I2C Addr 0x68
static void imu_setup_defaults() {
    config_imu.interface = 0;       // SPI
    config_imu.pin_or_address = 24; // CS pin
    float ident[] = { 1.0, 0.0, 0.0,
                      0.0, 1.0, 0.0,
                      0.0, 0.0, 1.0};
    for ( int i = 0; i < 9; i++ ) {
        config_imu.orientation[i] = ident[i];
    }
}

// configure the IMU settings and setup the ISR to aquire the data
void imu_setup() {
    if ( config_imu.interface == 0 ) {
        // SPI
        Serial.print("MPU9250 @ SPI pin: ");
        Serial.println(config_imu.pin_or_address);
        IMU.configure(config_imu.pin_or_address);
    } else if ( config_imu.interface == 1 ) {
        Serial.print("MPU9250 @ I2C Addr: 0x");
        Serial.println(config_imu.pin_or_address, HEX);
        IMU.configure(config_imu.pin_or_address, &Wire);
    } else {
        Serial.println("Error: problem with MPU9250 (IMU) configuration");
    }
    
    // initialize the IMU, specify accelerometer and gyro ranges
    int beginStatus = IMU.begin(ACCEL_RANGE_4G, GYRO_RANGE_500DPS);
    if ( beginStatus < 0 ) {
        Serial.println("\nIMU initialization unsuccessful");
        Serial.println("Check IMU wiring or try cycling power");
        Serial.println();
        delay(1000);
        return;
    }

    // set the DLPF and interrupts
    int setFiltStatus = IMU.setFilt(DLPF_BANDWIDTH_41HZ, MPU9250_SRD);
    if ( setFiltStatus < 0 ) {
        Serial.println("Filter initialization unsuccessful");
        delay(1000);
        return;
    }

    Serial.println("MPU-9250 ready.");
    for ( int i = 0; i < 9; i++ ) {
        Serial.print(config_imu.orientation[i], 2);
        Serial.print(" ");
        if ( i == 2 or i == 5 or i == 8 ) {
            Serial.println();
        }
    }
}

void imu_rotate(float v0, float v1, float v2,
                float *r0, float *r1, float *r2)
{
    *r0 = v0*config_imu.orientation[0] + v1*config_imu.orientation[1] + v2*config_imu.orientation[2];
    *r1 = v0*config_imu.orientation[3] + v1*config_imu.orientation[4] + v2*config_imu.orientation[5];
    *r2 = v0*config_imu.orientation[6] + v1*config_imu.orientation[7] + v2*config_imu.orientation[8];
}

// query the imu and update the structures
void imu_update() {
    unsigned long imu_micros = micros();
    float ax_raw, ay_raw, az_raw;
    float gx_raw, gy_raw, gz_raw;
    float hx_raw, hy_raw, hz_raw;
    float t;
    IMU.getMotion10(&ax_raw, &ay_raw, &az_raw,
                    &gx_raw, &gy_raw, &gz_raw,
                    &hx_raw, &hy_raw, &hz_raw, &t);
    
    // rotate into aircraft body frame
    float ax, ay, az, gx, gy, gz, hx, hy, hz;
    imu_rotate(ax_raw, ay_raw, az_raw, &ax, &ay, &az);
    imu_rotate(gx_raw, gy_raw, gz_raw, &gx, &gy, &gz);
    imu_rotate(hx_raw, hy_raw, hz_raw, &hx, &hy, &hz);
    
    if ( gyros_calibrated < 2 ) {
        calibrate_gyros(gx, gy, gz);
    } else {
        gx -= gx_calib;
        gy -= gy_calib;
        gz -= gz_calib;
    }

    // publish
    micros_node->setLongLong(imu_micros);
    ax_node->setFloat(ax);
    ay_node->setFloat(ay);
    az_node->setFloat(az);
    p_node->setFloat(gx);
    q_node->setFloat(gy);
    r_node->setFloat(gz);
    hx_node->setFloat(hx);
    hy_node->setFloat(hy);
    hz_node->setFloat(hz);
    temp_node->setFloat(t);
}


// stay alive for up to 15 seconds looking for agreement between a 1
// second low pass filter and a 0.1 second low pass filter.  If these
// agree (close enough) for 4 consecutive seconds, then we calibrate
// with the 1 sec low pass filter value.  If time expires, the
// calibration fails and we run with raw gyro values.
void calibrate_gyros(float gx, float gy, float gz) {
    static const float cutoff = 0.005;
    static float gxs = 0.0;
    static float gys = 0.0;
    static float gzs = 0.0;
    static float gxf = 0.0;
    static float gyf = 0.0;
    static float gzf = 0.0;
    static elapsedMillis total_timer = 0;
    static elapsedMillis good_timer = 0;
    static elapsedMillis output_timer = 0;

    if ( gyros_calibrated == 0 ) {
        Serial.print("Initialize gyro calibration: ");
        gxs = gx;
        gys = gy;
        gzs = gz;
        gxf = gx;
        gyf = gy;
        gzf = gz;
        total_timer = 0;
        good_timer = 0;
        output_timer = 0;
        gyros_calibrated = 1;
    }
    
    gxf = 0.95 * gxf + 0.05 * gx;
    gyf = 0.95 * gyf + 0.05 * gy;
    gzf = 0.95 * gzf + 0.05 * gz;
    gxs = 0.995 * gxs + 0.005 * gx;
    gys = 0.995 * gys + 0.005 * gy;
    gzs = 0.995 * gzs + 0.005 * gz;
    
    // use 'slow' filter value for calibration while calibrating
    gx_calib = gxs;
    gy_calib = gys;
    gz_calib = gzs;

    float dx = fabs(gxs - gxf);
    float dy = fabs(gys - gyf);
    float dz = fabs(gzs - gzf);
    if ( dx > cutoff || dy > cutoff || dz > cutoff ) {
        good_timer = 0;
    }
    if ( output_timer >= 1000 ) {
        output_timer = 0;
        if ( good_timer < 1000 ) {
            Serial.print("x");
        } else {
            Serial.print("*");
        }
    }
    if ( good_timer > 4100 || total_timer > 15000 ) {
        Serial.println();
        // set gyro zero points from the 'slow' filter.
        gx_calib = gxs;
        gy_calib = gys;
        gz_calib = gzs;
        gyros_calibrated = 2;
        imu_update(); // update imu_calib values before anything else get's a chance to read them
        Serial.print("Average gyros: ");
        Serial.print(gx_calib, 4);
        Serial.print(" ");
        Serial.print(gy_calib, 4);
        Serial.print(" ");
        Serial.print(gz_calib, 4);
        Serial.println();
        if ( total_timer > 15000 ) {
            Serial.println("gyro init: too much motion, using best average guess.");
        } else {
            Serial.println("gyro init: success.");
        }
    }
}
