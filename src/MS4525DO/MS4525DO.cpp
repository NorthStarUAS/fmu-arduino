/*
MS4525DO.cpp
Brian R Taylor
brian.taylor@bolderflight.com
2016-11-03

Copyright (c) 2016 Bolder Flight Systems

Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
and associated documentation files (the "Software"), to deal in the Software without restriction, 
including without limitation the rights to use, copy, modify, merge, publish, distribute, 
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is 
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or 
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING 
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// Teensy 3.0 || Teensy 3.1/3.2 || Teensy 3.5 || Teensy 3.6 || Teensy LC 
#if defined(__MK20DX128__) || defined(__MK20DX256__) || defined(__MK64FX512__) || \
	defined(__MK66FX1M0__) || defined(__MKL26Z64__)

#include "Arduino.h"
#include "MS4525DO.h"
//#include <i2c_t3.h>  // I2C library

/* Default constructor */
MS4525DO::MS4525DO(){
  _address = 0x28; // I2C address
  _bus = NULL; // I2C bus
}

/* MS4525DO object, input the I2C address and enumerated chip name (i.e. MS4525DO_1200_B) */
MS4525DO::MS4525DO(uint8_t address, TwoWire *bus){
    _address = address; // I2C address
    _bus = bus; // I2C bus
}

/* starts the I2C communication and sets the pressure and temperature ranges using getTransducer */
void MS4525DO::begin(){
    // starting the I2C bus
    _bus->begin();
    _bus->setClock(_i2cRate);

    _bus->beginTransmission(_address);
    _bus->endTransmission();
    delay(100);
}

/* reads pressure and temperature and returns values in counts */
void MS4525DO::read(float* pressure, float* temperature) {
    uint8_t b[4]; // buffer
    const uint8_t numBytes = 4;

    // 4 bytes from address
    _bus->requestFrom(_address, numBytes);
  
    // put the data in buffer
    int counter = 0;
    while ( _bus->available() && counter < numBytes ) {
        b[counter] = _bus->read();
        counter++;
    }
    // _bus->endTransmission();

    if ( counter < numBytes ) {
        Serial.println("Error, fewer than expected bytes available on i2c read");
    } else {
        uint8_t status = (b[0] & 0xC0) >> 6;
        b[0] = b[0] & 0x3f;
        uint16_t dp_raw = (b[0] << 8) + b[1];

        uint16_t T_dat = (b[2] << 8) + b[3];
	T_dat = (0xFFE0 & T_dat) >> 5;
        //b[3] = (b[3] >> 5);
        //uint16_t T_dat = ((b[2]) << 8) | b[3];

        // PR = (double)((P_dat-819.15)/(14744.7)) ;
        // PR = (PR - 0.49060678) ;
        // PR = abs(PR);
        // V = ((PR*13789.5144)/1.225);
        // VV = (sqrt((V)));

        // Calculate differential pressure. As its centered around 8000
	// and can go positive or negative
	const float P_min = -1.0f;
	const float P_max = 1.0f;
	const float PSI_to_Pa = 6894.757f;
	/*
	  this equation is an inversion of the equation in the
	  pressure transfer function figure on page 4 of the datasheet
	  We negate the result so that positive differential pressures
	  are generated when the bottom port is used as the static
	  port on the pitot and top port is used as the dynamic port
	 */
	float diff_press_PSI = -((dp_raw - 0.1f * 16383) * (P_max - P_min) / (0.8f * 16383) + P_min);
	float diff_press_pa_raw = diff_press_PSI * PSI_to_Pa;

        const float T_factor = 200.0 / 2047.0;
        float TR = (float)T_dat * T_factor - 50.0;
    
        Serial.print(status); Serial.print("\t");
        Serial.print(dp_raw); Serial.print("\t");
        Serial.print(diff_press_pa_raw,2); Serial.print("\t");
        Serial.print(T_dat); Serial.print("\t");
        Serial.print(TR,1); Serial.print("\t");
        Serial.println();
    }
}

#endif