// SPDX-License-Identifier: MIT
// Arduino Leonardo / Micro (ATmega32u4), MIDIUSB library.
// Buttons: pins 2..6 to GND. Potentiometer: A0, wiper; ends to GND / VCC.
#include <MIDIUSB.h>
const byte pins[] = {2, 3, 4, 5, 6};
bool stable[5] = {HIGH,HIGH,HIGH,HIGH,HIGH};
bool previous[5] = {HIGH,HIGH,HIGH,HIGH,HIGH};
unsigned long changed[5] = {};
int lastVolume = -1;
unsigned long lastPot = 0;
void cc(byte number, byte value) {
  midiEventPacket_t event = {0x0B, 0xB0, number, value};
  MidiUSB.sendMIDI(event);
  MidiUSB.flush();
}
void setup() { for (byte pin : pins) pinMode(pin, INPUT_PULLUP); }
void loop() {
  const unsigned long now = millis();
  for (byte i=0; i<5; i++) {
    bool raw = digitalRead(pins[i]);
    if (raw != previous[i]) { previous[i]=raw; changed[i]=now; }
    if (now-changed[i] >= 20 && raw != stable[i]) {
      stable[i]=raw;
      cc(20+i, raw == LOW ? 127 : 0); // Scenes 1–4, then transport toggle.
    }
  }
  if (now-lastPot >= 10) {
    lastPot=now;
    int value=analogRead(A0)/8;
    if (abs(value-lastVolume)>=2 || (value!=lastVolume && (value==0 || value==127))) {
      lastVolume=value; cc(7,value);
    }
  }
}
