#include <Homie.h>
#include <Arduino.h>
#include <WiFiUdp.h>
#include <string>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <ArduinoJson.h>
#include "IRelectra.h"


HomieNode temperatureNode("temperature", "temperature","temperature");
HomieNode modeNode("mode", "mode","mode");
HomieNode fanNode("fan", "fan","fan");
HomieNode swingNode("swing", "swing","swing");
HomieNode ifeelNode("ifeel", "ifeel","ifeel");
HomieNode ifeelTempNode("ifeel_temperature", "ifeel_temperature","ifeel_temperature");
HomieNode powerNode("power", "power","power");
HomieNode stateNode("state", "state","state");

#ifdef ARDUINO_ESP8266_ESP01
const uint8_t POWER_PIN = 2;
const uint8_t IR_PIN = 0;
#else
const uint8_t POWER_PIN = 5;
const uint8_t IR_PIN = 4;
const uint8_t GREEN_LED_PIN = 12;
const uint8_t RED_LED_PIN = 15;
#endif

#ifndef ELECTRAWIFI_NO_IR_RCV
const uint8_t IR_RECV_PIN = 14;
#endif

const uint8_t kTimeout = 10;
const uint16_t kCaptureBufferSize = 300;

WiFiUDP udpClient;
IRelectra ac(IR_PIN);

#ifndef ELECTRAWIFI_NO_IR_RCV
IRrecv irrecv(IR_RECV_PIN, kCaptureBufferSize, kTimeout, true);
decode_results ir_ticks;
#endif

const int IFEEL_INTERVAL = 2 * 60000; // 2 min
const int POWER_DEBOUNCE = 2000; // 2 sec
const int UPDATES_INTERVAL = 10 * 60000; // 10 min
ulong power_change_time = 0;
ulong ifeel_send_time = 0;
ulong updates_send_time = 0;


void send_updates() {
  String fan, mode, swing;
  if (ac.fan == FAN_LOW) {
    fan = "low";
  } else if (ac.fan == FAN_MED) {
    fan = "med";
  } else if (ac.fan == FAN_HIGH) {
    fan = "high";
  } else if (ac.fan == FAN_AUTO) {
    fan = "auto";
  } 
  
  ac.power_real = ac.power_setting;

  if (ac.power_real) {
    if (ac.mode == MODE_COOL) {
      mode = "cool";
    } else if (ac.mode == MODE_HEAT) {
      mode = "heat";
    } else if (ac.mode == MODE_DRY) {
      mode = "dry";
    } else if (ac.mode == MODE_AUTO) {
      mode = "auto";
    }
  }
  else {
    mode = "off";
  }

  if (ac.swing == SWING_ON && ac.swing_h == SWING_H_ON) {
    swing = "both";
  } else if (ac.swing == SWING_ON && ac.swing_h == SWING_H_OFF) {
    swing = "on";
  } else if (ac.swing == SWING_OFF && ac.swing_h == SWING_H_ON) {
    swing = "hor";
  } else {
    swing = "off";
  }
  //Serial << "AC swing: " << (ac.swing ? "on": "off") << "|" << (ac.swing_h  ? "on": "off") <<endl;
  //Serial << "Send Update swing: " << swing << endl;
  powerNode.setProperty("state").send(ac.power_real ? "on": "off");
  fanNode.setProperty("state").send(fan);
  modeNode.setProperty("state").send(mode);
  swingNode.setProperty("state").send(swing);
  temperatureNode.setProperty("state").send(String(ac.temperature));
  ifeelNode.setProperty("state").send(ac.ifeel == IFEEL_ON ? "on" : "off");
}

void loopHandler() {
  ulong now = millis();

  // Set power led and update the power state after it's stable for a while
  uint power_state = !digitalRead(POWER_PIN);
#ifndef ARDUINO_ESP8266_ESP01
  digitalWrite(GREEN_LED_PIN, power_state);
#endif
  if (power_state != ac.power_real) {
    if (power_change_time) {
      if (now - power_change_time > POWER_DEBOUNCE) {
        ac.power_real = power_state;
        ac.power_setting = power_state;
        powerNode.setProperty("state").send(power_state ? "on": "off");
        power_change_time = 0;
      }
    } else {
      power_change_time = now;
    }
  } else {
    power_change_time = 0;
  }

  // Send ifeel if relevant
  if (now - ifeel_send_time >= IFEEL_INTERVAL) {
    if (ac.ifeel == IFEEL_ON) {
      ac.SendElectra(true);
    }
    ifeel_send_time = now;
  }

  // Send updates
  /*
  if (now - updates_send_time >= UPDATES_INTERVAL) {
    send_updates();
    updates_send_time = now;
  }
  */

  // Handle IR recv
#ifndef ELECTRAWIFI_NO_IR_RCV
  if (irrecv.decode(&ir_ticks)) {
    uint64_t code = 0;
    code = DecodeElectraIR(ir_ticks);
    irrecv.resume();
    if (code) {
      ac.UpdateFromIR(code);
      ac.SendElectra(false);
      send_updates();
    }
  }
#endif
}


bool temperatureHandler(const HomieRange& range, const String& value) {
  uint8_t temp = value.toInt();
  if (temp < 15 || temp > 30) { // setpoint temp has only 4 bits where 15 == 0b0000 and 30 == 0b1111
    return false;
  }
  ac.temperature = temp;
  ac.SendElectra(false);
  send_updates();
  return true;
}

bool modeHandler(const HomieRange& range, const String& value) {
  if (value == "cool") {
    ac.mode = MODE_COOL;
  } else if (value == "heat") {
    ac.mode = MODE_HEAT;
  } else if (value == "auto") {
    ac.mode = MODE_AUTO;
  } else if (value == "dry") {
    ac.mode = MODE_DRY;
  } else if (value == "fan") {
    ac.mode = MODE_FAN;
  } else {
    return false;
  }
  ac.SendElectra(false);
  send_updates();
  return true;
}

bool fanHandler(const HomieRange& range, const String& value) {
  if (value == "low") {
    ac.fan = FAN_LOW;
  } else if (value == "med") {
    ac.fan = FAN_MED;
  } else if (value == "high") {
    ac.fan = FAN_HIGH;
  } else if (value == "auto") {
    ac.fan = FAN_AUTO;
  } else {
    return false;
  }
  ac.SendElectra(false);
  send_updates();
  return true;
}

bool ifeelHandler(const HomieRange& range, const String& value) {
  if (value == "on") {
    ac.ifeel = IFEEL_ON;
  } else if (value == "off") {
    ac.ifeel = IFEEL_OFF;
  } else {
    return false;
  }
  ac.SendElectra(false);
  send_updates();
  return true;
}

bool ifeelTempHandler(const HomieRange& range, const String& value) {
  uint8_t temp = value.toInt();
  if (temp < 5 || temp > 36) { // ifeel temp has only 5 bits where 0 == 0b00000 and 36 == 0b11111
    return false;
  }
  ac.ifeel_temperature = temp;
  send_updates();
  return true;
}

bool swingHandler(const HomieRange& range, const String& value) {
  //Serial << "Swing Handler value: " << value << endl;
  if (value == "on") {
    ac.swing = SWING_ON;
    ac.swing_h = SWING_H_OFF;
  } else if (value == "both") {
    ac.swing = SWING_ON;
    ac.swing_h = SWING_H_ON;
  } else if (value == "hor") {
    ac.swing = SWING_OFF;
    ac.swing_h = SWING_H_ON;
  }  else if (value == "off"){
    ac.swing = SWING_OFF;
    ac.swing_h = SWING_H_OFF;
  } else {
    return false;
  }
  ac.SendElectra(false);
  send_updates();
  return true;
}

bool powerHandler(const HomieRange& range, const String& value) {
  if (value == "on") {
    ac.power_setting = true;
  } else if (value == "off") {
    ac.power_setting = false;
  } else {
    return false;
  }
  ac.SendElectra(false);
  ac.power_real = ac.power_setting;
  powerNode.setProperty("state").send(ac.power_real ? "on": "off");
  send_updates();
  return true;
}

bool jsonHandler(const HomieRange& range, const String& value) {
  StaticJsonDocument<200> parsed;
  auto error = deserializeJson(parsed,value);
  if (error) {
    return false;
  }

  String fan = parsed["fan"];
  String mode = parsed["mode"];
  String power = parsed["power"];
  String ifeel = parsed["ifeel"];
  String temp_str = parsed["temperature"];
  String swing = parsed["swing"];
  uint8_t temp = temp_str.toInt();

  if (mode == "cool") {
    ac.mode = MODE_COOL;
  } else if (mode == "heat") {
    ac.mode = MODE_HEAT;
  } else if (mode == "auto") {
    ac.mode = MODE_AUTO;
  } else if (mode == "dry") {
    ac.mode = MODE_DRY;
  } else if (mode == "fan") {
    ac.mode = MODE_FAN;
  } else {
    return false;
  }

  if (fan == "low") {
    ac.fan = FAN_LOW;
  } else if (fan == "med") {
    ac.fan = FAN_MED;
  } else if (fan == "high") {
    ac.fan = FAN_HIGH;
  } else if (fan == "auto") {
    ac.fan = FAN_AUTO;
  } else {
    return false;
  }

  if (power == "on") {
    ac.power_setting = true;
  } else if (power == "off") {
    ac.power_setting = false;
  } else {
    return false;
  }

  if (swing == "on") {
    ac.swing = SWING_ON;
    ac.swing_h = SWING_H_OFF;
  } else if (swing == "both") {
    ac.swing = SWING_ON;
    ac.swing_h = SWING_H_ON;
  } else if (swing == "hor") {
    ac.swing = SWING_OFF;
    ac.swing_h = SWING_H_ON;
  }  else if (swing == "off") {
    ac.swing = SWING_OFF;
    ac.swing_h = SWING_H_OFF;
  } else {
    return false;
  }

  if (ifeel == "on") {
    ac.ifeel = IFEEL_ON;
  } else if (ifeel == "off") {
    ac.ifeel = IFEEL_OFF;
  } else {
    return false;
  }

  if (temp < 15 || temp > 30) { // setpoint temp has only 4 bits where 15 == 0b0000 and 30 == 0b1111
    return false;
  }
  ac.temperature = temp;

  ac.SendElectra(false);
  ac.power_real = ac.power_setting;

  send_updates();
  return true;
}

void setup() {
  Serial.begin(115200);
  Serial << endl << endl;
  //Homie.disableLogging();
#ifdef ARDUINO_ESP8266_ESP01
  Homie.disableLedFeedback();
#endif
  Homie_setFirmware("ElectraWifi", "1.0.0");
  Homie.setLoopFunction(loopHandler);
  temperatureNode.advertise("state").settable(temperatureHandler);
  fanNode.advertise("state").settable(fanHandler);
  modeNode.advertise("state").settable(modeHandler);
  ifeelNode.advertise("state").settable(ifeelHandler);
  ifeelTempNode.advertise("state").settable(ifeelTempHandler);
  powerNode.advertise("state").settable(powerHandler);
  swingNode.advertise("state").settable(swingHandler);
  stateNode.advertise("json").settable(jsonHandler);
  
  pinMode(POWER_PIN, INPUT_PULLUP);
#ifndef ARDUINO_ESP8266_ESP01
  pinMode(GREEN_LED_PIN, OUTPUT);
  Homie.setLedPin(RED_LED_PIN, HIGH);
#endif
#ifndef ELECTRAWIFI_NO_IR_RCV
  irrecv.setUnknownThreshold(100); 
  irrecv.enableIRIn();
#endif
  Homie.setup();
}

void loop() {
  Homie.loop();
}