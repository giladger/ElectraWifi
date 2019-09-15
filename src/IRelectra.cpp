/*
 * Based on IRelectra (https://github.com/barakwei/IRelectra) by Barak Weiss
 */

#include "IRelectra.h"
#include <stdint.h>
#include <Arduino.h>
#include <IRutils.h>

#define UNIT 1000
#define TICKS_TO_UNITS(x) round((float)(x) * kRawTick / UNIT)

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
    uint64_t code = 0;
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
    code |= (((uint64_t)power     & 1)  << 33);
    code |= (((uint64_t)mode      & 7)  << 30);
    code |= (((uint64_t)fan       & 3)  << 28);
    code |= (((uint64_t)notify    & 1)  << 27);
    code |= (((uint64_t)swing     & 1)  << 25);
    code |= (((uint64_t)ifeel     & 1)  << 24);
    code |= (((uint64_t)send_temp & 31) << 19);
    code |= (((uint64_t)sleep     & 1)  << 18);
    code |= 2;
    
    return code;
}

void IRelectra::UpdateFromIR(uint64_t code) {
    uint32_t send_temp;
    power_t power;
    bool notify;

    power = (power_t)((code >> 33) & 1);
    mode = (ac_mode_t)((code >> 30) & 7);
    fan = (fan_t)((code >> 28) & 3);
    notify = (bool)((code >> 27) & 1);
    swing = (swing_t)((code >> 25) & 1);
    ifeel = (ifeel_t)((code >> 24) & 1);
    send_temp = (ifeel_t)((code >> 19) & 31);
    sleep = (sleep_t)((code >> 18) & 1);

    if (power == POWER_TOGGLE) {
        power_setting = !power_real;
    } 

    if (notify) {
        ifeel_temperature = send_temp + 5;
    } else {
        temperature = send_temp + 15;
    }
}


uint64_t manchester_to_bits(volatile uint16_t *data, uint8_t &i, uint8_t size, bool is_space) {
    uint64_t result = 0;
    while (i < size - 1) {
        if (TICKS_TO_UNITS(data[i]) == 1) {
            if (TICKS_TO_UNITS(data[i + 1]) > 0) {
                result = result << 1 | (is_space ? 1 : 0);
                if (TICKS_TO_UNITS(data[i + 1]) > 1) {
                    data[i + 1] -= UNIT / kRawTick;
                    is_space = !is_space;
                    i += 1;
                    continue;
                }
                i += 2;
                continue;
            }
            return 0;
        }
        if (TICKS_TO_UNITS(data[i]) == 3) {  // the beginning to the next repetition
            return result;
        }
        return 0;
    }
    if (TICKS_TO_UNITS(data[i]) == 4) { // the end of the last repetition
        return result;
    }

    return 0;
}

uint64_t DecodeElectraIR(decode_results &ir_ticks) {
    uint64_t results[3] = {0, 0, 0};
    uint8_t result_index = 0;
    uint8_t i = 0;

    while (i < ir_ticks.rawlen - 1 && result_index < 3) {
        if (TICKS_TO_UNITS(ir_ticks.rawbuf[i]) == 3) {
            if (TICKS_TO_UNITS(ir_ticks.rawbuf[i + 1]) == 3) {
                i += 2;
                results[result_index] = manchester_to_bits(ir_ticks.rawbuf, i, ir_ticks.rawlen, false);
                result_index++;
                continue;
            }
            if (TICKS_TO_UNITS(ir_ticks.rawbuf[i + 1]) == 4) {
                ir_ticks.rawbuf[i + 1] -= 3 * UNIT / kRawTick;
                i += 1;
                results[result_index] = manchester_to_bits(ir_ticks.rawbuf, i, ir_ticks.rawlen, true);
                result_index++;
                continue;
            }
        }
        i++;
    }
    if (TICKS_TO_UNITS(ir_ticks.rawbuf[i]) == 4) {
        if (results[0] == results[1] || results[0] == results[2]) {
            return results[0];
        }
        if (results[1] == results[2]) {
            return results[1];
        }
    }
    return 0;
}
