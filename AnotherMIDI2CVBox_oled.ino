/*  Another MIDI2CV Box
 *  MIDI interface for Hz/V and V/oct synths. Connect your favourite keyboard or DAW to the box via MIDI; 
 *  the box will in turn control your synth gate On/Off (straight +5V/0V or S-Trig, reversed, 
 *  0V/+5V), pitch (variable voltage via DAC output), velocity (0V to +5V, RC filtered) and a MIDI Control 
 *  Change variable at your will (0V to +5V, RC filtered).
 *  
 *  Connections:
 * (1) MIDI connector: see general online reference 
 * (2) DAC MCP4725:
 * SDA pin to A4/SDA (Arduino UNO adn nano)
 * SCL pin to A5/SCL (Arduino UNO adn nano)
 * GND to GND
 * VCC to +5V
 * (3) Outputs: 
 * Arduino gate OUT pin (pin 12 by default) to synth gate/trigger IN via 1K Ohm resistor
 * Arduino velocity OUT pin (pin 10 by default) to synth VCA IN via RC filter (1K Ohm, 100uF)
 * Arduino MIDI CC OUT pin (pin 9 by default) to synth VCF IN via RC filter (1K Ohm, 100uF)
 * DAC OUT to synth VCO IN
 * 
 * MIDI messages table:
 *    Message                      Status    Data 1               Data 2
 *    Note Off                     8n        Note Number          Velocity
 *    Note On                      9n        Note Number          Velocity
 *    Polyphonic Aftertouch        An        Note Number          Pressure
 *    Control Change               Bn        Controller Number    Data
 *    Program Change               Cn        Program Number       Unused
 *    Channel Aftertouch           Dn        Pressure             Unused
 *    Pitch Wheel                  En        LSB                  MSB    
 *    
 * Key
 * n is the MIDI Channel Number (0-F)
 * LSB is the Least Significant Byte
 * MSB is the Least Significant Byte
 * There are several different types of controller messages. 
 * 
 * Hz/V note simplified chart: 
 * Note          A1    A2    A3    B3    C4    D4    E4    A4    A5
 * Frequency, Hz 55    110   220   247   261   294   330   440   880
 * Hz/volt, V    1.000 2.000 4.000 4.491 4.745 5.345 6.000 8.000 16.000
 * 
 * useful links, random order:
 *  https://en.wikipedia.org/wiki/CV/gate
 *  https://www.instructables.com/id/Send-and-Receive-MIDI-with-Arduino/
 *  http://www.songstuff.com/recording/article/midi_message_format
 *  https://espace-lab.org/activites/projets/en-arduino-processing-midi-data/
 *  https://learn.sparkfun.com/tutorials/midi-shield-hookup-guide/example-2-midi-to-control-voltage
 *  https://provideyourown.com/2011/analogwrite-convert-pwm-to-voltage/
 *  https://www.midi.org/specifications/item/table-3-control-change-messages-data-bytes-2
 *  https://arduino-info.wikispaces.com/Arduino-PWM-Frequency
 *  
 *  by Barito, 2017
 */

#include <Adafruit_MCP4725.h>
#include <MIDI.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

//set at your will ...

#define REVERSE_GATE 0 //V-Trig = 0, S-Trig = 1
#define CC_NUMBER 1 //MIDI CC number
#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);
bool HZV = 0; //set to "0" for V/oct

Adafruit_MCP4725 dac;

int MIDI_CHANNEL; //the MIDI channel you want your box to listen to (1-16)
byte gatePin = 7;
byte velocityPin = 9; //pwm frequency is going to be increased for this in the setup
byte CCPin = 10; //pwm frequency is going to be increased for this in the setup

float outVolt1000;
int velocityOut;
int CCOut;
uint16_t dacValue;


// midi chan select
enum PinAssignments {
  encoderPinA = 2,   // right (labeled DT on our decoder, yellow wire)
  encoderPinB = 3,   // left (labeled CLK on our decoder, green wire)
  clearButton = 5,   // switch (labeled SW on our decoder, orange wire)
// connect the +5v and gnd appropriately
};

volatile unsigned int encoderPos;  // a counter for the dial
unsigned int lastReportedPos = 1;   // change management
static boolean rotating=false;      // debounce management

// interrupt service routine vars
boolean A_set = false;              
boolean B_set = false;
//

//midi to cv
MIDI_CREATE_DEFAULT_INSTANCE();

void setup() {  
//For Arduino Uno, Nano, and any other board using ATmega 8, 168 or 328
//TCCR0B = TCCR0B & B11111000 | B00000001;    // D5, D6: set timer 0 divisor to 1 for PWM frequency of 62500.00 Hz
TCCR1B = TCCR1B & B11111000 | B00000001;    // D9, D10: set timer 1 divisor to 1 for PWM frequency of 31372.55 Hz
//TCCR2B = TCCR2B & B11111000 | B00000001;    // D3, D11: set timer 2 divisor to 1 for PWM frequency of 31372.55 Hz
pinMode(gatePin, OUTPUT);
pinMode(velocityPin, OUTPUT);
pinMode(CCPin, OUTPUT);
if(REVERSE_GATE==1){digitalWrite(gatePin, HIGH);}
else {digitalWrite(gatePin, LOW);}
MIDI.setHandleNoteOn(handleNoteOn);
MIDI.setHandleNoteOff(handleNoteOff);
MIDI.setHandleControlChange(handleControlChange);
MIDI.begin(MIDI_CHANNEL);// start MIDI and listen to channel "MIDI_CHANNEL"
dac.begin(0x62);

display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x32)

//midi chan select
  pinMode(encoderPinA, INPUT_PULLUP); // new method of enabling pullups
  pinMode(encoderPinB, INPUT_PULLUP); 
  pinMode(clearButton, INPUT_PULLUP);

// encoder pin on interrupt 0 (pin 2)
  attachInterrupt(0, doEncoderA, CHANGE);
// encoder pin on interrupt 1 (pin 3)
  attachInterrupt(1, doEncoderB, CHANGE);

  encoderPos = 1; 
  MIDI_CHANNEL = encoderPos;

  display.setTextSize(2);
  display.setTextColor(WHITE);
  displayOled(MIDI_CHANNEL, true);
}

void loop() {
MIDI.read(MIDI_CHANNEL);

// midi chan select
  rotating = true;  // reset the debouncer
  
  if (lastReportedPos != encoderPos) {
    displayOled(encoderPos, false); 
    lastReportedPos = encoderPos;
    if (displayOled(encoderPos, false) != encoderPos){
      displayOled(encoderPos, false); 
    }
  }
  if (digitalRead(clearButton) == HIGH )  {
    MIDI_CHANNEL = encoderPos;
    displayOled(MIDI_CHANNEL, true);
  }
}

void handleNoteOn(byte channel, byte note, byte velocity){
  //Hz/V; x 1000 because map truncates decimals
  if (HZV){outVolt1000 = 148.7*exp(0.0578*note);}//0.1487*1000
  //V/oct; x 1000 because map truncates decimals
  else{outVolt1000 = note*1000.0/12.0;} 
  dacValue = constrain(map(outVolt1000, 0, 5000, 0, 4095), 0, 4095);
  dac.setVoltage(dacValue, false);
  if(REVERSE_GATE == 1) {digitalWrite(gatePin, LOW);}
  else {digitalWrite(gatePin, HIGH);}
  velocityOut = map(velocity, 0, 127, 0, 255);
  analogWrite(velocityPin, velocityOut);
}

void handleNoteOff(byte channel, byte note, byte velocity){
  //dac.setVoltage(0, false);
  if(REVERSE_GATE == 1) {digitalWrite(gatePin, HIGH);}
  else {digitalWrite(gatePin, LOW);}
  analogWrite(velocityPin, 0);
}

void handleControlChange(byte channel, byte number, byte value){
   if(number == CC_NUMBER){
   CCOut = map(value, 0, 127, 0, 255);
   analogWrite(CCPin, CCOut);}
} 


// Midi chan select

// Interrupt on A changing state
void doEncoderA(){
  // debounce
  if ( rotating ) delay (5);  // wait a little until the bouncing is done

  // Test transition, did things really change? 
  if( digitalRead(encoderPinA) != A_set ) {  // debounce once more
    A_set = !A_set;

    // adjust counter + if A leads B
    if ( A_set && !B_set ) 
      encoderPos += 1;
      encoderPos = min(encoderPos, 16);
      
    rotating = false;  // no more debouncing until loop() hits again
  }
}

// Interrupt on B changing state, same as A above
void doEncoderB(){
  if ( rotating ) delay (5);
  if( digitalRead(encoderPinB) != B_set ) {
    B_set = !B_set;
    //  adjust counter - 1 if B leads A
    if( B_set && !A_set ) 
      encoderPos -= 1;
      encoderPos = max(encoderPos, 1);

    rotating = false;
  }
}


int displayOled(int data, bool saved){
    display.setCursor(1,0);
    display.clearDisplay();
    display.println("Midi Chan:");
    display.setCursor(1,18);
    String padZero = "";
    String callToAction = "";
    if (saved == false){
      callToAction = "  save?";
    }
    if (data < 10){
      padZero = "0";
    }
    display.println(padZero + String(data) + callToAction);
    display.display();
    return data;
}

