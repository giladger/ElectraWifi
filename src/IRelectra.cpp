/*
 * IRelectra
 * Version 0.8 
 * Copyrights 2014 Barak Weiss
 *
 * Many thanks to Chris from AnalysIR
 */

#include "IRelectra.h"
#include <stdint.h>
#include <Arduino.h>

#define UNIT 1000
#define NUM_BITS 34

IRelectra::IRelectra(uint8_t output_pin) : output_pin(output_pin)
{
    pinMode(output_pin, OUTPUT);
    power_setting = POWER_KEEP;
    power_real = false;
    mode = MODE_COOL;
    fan = FAN_LOW;
    temperature = 26;
    swing = SWING_OFF;
    sleep = SLEEP_OFF;
    ifeel_temperature = 0;
    ifeel = IFEEL_OFF;
}

// Add bit b to array p at index i
// This function is used to convert manchester encoding to MARKs and SPACEs
// p is the pointer to the start of the MARK, SPACE array
// i is the current index
// b is the bit we want to add to the MARK, SPACE array
// A zero bit is one unit MARK and one unit SPACE
// a one bit is one unit SPACE and one unit MARK
void IRelectra::addBit(unsigned int* p, int* i, char b) {
    if (((*i) & 1) == 1) {
        // current index is SPACE
        if ((b & 1) == 1) {
            // one is one unit low, then one unit up
            // since we're pointing at SPACE, we should increase it byte a unit
            // then add another MARK unit
            *(p+*i) += UNIT;
            (*i)++;
            *(p+*i) = UNIT;
        }
        if ((b & 1) == 0) {
            // we need a MARK unit, then SPACE unit
            (*i)++;
            *(p+*i) = UNIT;
            (*i)++;
            *(p+*i) = UNIT;
        }
    } else if (((*i) & 1) == 0) {
        // current index is MARK
        if ((b & 1) == 1) {
            (*i)++;
            *(p+*i) = UNIT;
            (*i)++;
            *(p+*i) = UNIT;
        }
        if ((b & 1) == 0) {
            *(p+*i) += UNIT;
            (*i)++;
            *(p+*i) = UNIT;
        }
    }

}

// Sends the specified configuration to the IR led using IRremote
// 1. Get the numeric value of the configuration
// 2. Convert to IRremote compatible array (MARKS and SPACES)
// 3. Send to GPIO
void IRelectra::SendElectra(bool notify) {
    uint data[200]; //~maximum size of the IR packet
    int i = 0;
 
    // get the data representing the configuration
    uint64_t code = EncodeElectra(notify);
    
    // The whole packet looks this:
    //  3 Times: 
    //    3000 usec MARK
    //    3000 used SPACE
    //    Maxchester encoding of the data, clock is ~1000usec
    // 4000 usec MARK
    for (int k = 0; k<3; k++) {
        data[i] = 3 * UNIT; //mark
        i++;
        data[i] = 3 * UNIT;
        for (int j = NUM_BITS - 1; j >= 0; j--) {
            addBit(data, &i, (code >> j) & 1);
        }
        i++;
    }
    data[i] = 4 * UNIT;

    SendRaw(data, i + 1);
}

void IRelectra::SendRaw(unsigned int *data, uint size) {
    for (uint i = 0; i < size; ++i) {
        digitalWrite(output_pin, (i % 2) ? LOW : HIGH);
        delayMicroseconds(data[i]);
    }
    digitalWrite(output_pin, LOW);
}


// Encodes specific A/C configuration to a number that describes
// That configuration has a total of 34 bits
//    33: Power bit, if this bit is ON, the A/C will toggle it's power.
// 32-30: Mode - Cool, heat etc.
// 29-28: Fan - Low, medium etc.
//    27: Temperature notification (used in I Feel)
//    26: Zero
//    25: Swing On/Off
//    24: Set I Feel
// 23-19: Temperature, 4 bits for set (where 15 is 00000, 30 is 01111), 5 bits for notify
//    18: Sleep mode On/Off
// 17- 2: Zeros
//     1: One
//     0: Zero
uint64_t IRelectra::EncodeElectra(bool notify) {
    uint64_t num = 0;
    uint32_t send_temp;
    power_t power;

    if (power_setting == power_real) {
        power = POWER_KEEP;
    } else {
        power = POWER_TOGGLE;
    }

    if (notify) {
        send_temp = ifeel_temperature - 5;
    } else {
        send_temp = temperature - 15;
    }
    num |= (((uint64_t)power     & 1)  << 33);
    num |= (((uint64_t)mode      & 7)  << 30);
    num |= (((uint64_t)fan       & 3)  << 28);
    num |= (((uint64_t)notify    & 1)  << 27);
    num |= (((uint64_t)swing     & 1)  << 25);
    num |= (((uint64_t)ifeel     & 1)  << 24);
    num |= (((uint64_t)send_temp & 31) << 19);
    num |= (((uint64_t)sleep     & 1)  << 18);
    num |= 2;
    
    return num;
}