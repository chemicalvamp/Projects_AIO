#include <Arduino.h>
#include <U8g2lib.h>

// Remaining unused pin defines:
#define Unused0 A2
#define Unused1 A3
#define unused2 A6
#define unused3 A7

// https://learn.adafruit.com/assets/571
// Tempurature pin defines:
#define BoardThermisterPin A1
#define RemoteThermisterPin A0
#define ThermisterPin A0 // What will be read
#define SamplesCount 5
#define TemperatureNominal 18  // Tweaking #1
#define ThermisterNominal 52000 // Tweaking #2 my probe with leads came to about 23200 but higher gives a more accurate result.
#define SeriesResistor 9600  // Tweaking #3
#define BCoefficient 3950 // Scary maths don't touch
#define FanMOSFET 3 // PWM

// Relay pin defines:
#define RelayOutput 11 // this is immediatly set to HIGH and turned off by a switch on the protoboard going to ground
#define PowerButton 12 // When this pin is grounded it will toggle ShuttingDown to true, and digitalWrite(RelayOutput, LOW)
                       // disabling it's own power source

// Command pin defines:
#define RX 0 
#define TX 1
#define SDA A4
#define SCL A5

// Lighting pin defines
#define DrawerLightingMOSFET 5 // This is an exterior light, it will be Pulse Width Modulated as well as set HIGH when the microswitch is closed. PWM
#define CabinetLightingMOSFET 6 // the ground for the interior LED strip will be toggled on when this pin goes HIGH. PWM
#define AuxLightingMOSFET 9 // Not used but present in the breadboard. PWM
                            // there is a 10K resistor to ground on all MOSFETs
#define DrawerSwitch 8 // Hinge pin is ground and the (opposite) roller side pin is this pin
#define CabinetSwitch 7 // Unused, all switches are paralleled together
#define DopplerInput 2 // Unused, will try a module in the future.

// Debug variables:
bool DebugFirstPass = true; // only useful to the extent the Serial output might contain an offline message.
int ButtonMillisCooldown = 0;
bool DoLighting = false;
bool DoRelay = false;
bool DoTemperature = true;
bool DoSerial = true;
bool DoDebug = true;

// Command variables:
String inString = ""; // Will be assembled into a command string.

// https://www.arduino.cc/reference/en/language/functions/time/millis/
// Timing variables:
unsigned long LastMillis = 0; // Last millis(); will be stored into this at end of loop.
unsigned long DeltaTime = 0;
unsigned long PWMChangeDelay = 0; // Simple delay between PWM change actions

// https://learn.adafruit.com/thermistor/using-a-thermistor
// https://learn.adafruit.com/assets/571
// Tempurature variables:
uint8_t SampleIndex;
float Samples[SamplesCount];
float Steinhart; // Formula variable for temperature in °C
float Differential = 0.00f; // The difference in tempurature between Average and Desired.
float AverageTemperature; // The average temperature for the last 5 cycles
float DesiredAmbient = 30.5; // Desired tempurature for this local space °C. Can you see the broken? o.0
int FanPWM = 0; // The value to be written to FanMOSFET pin 3

// Relay Variables:
bool ShuttingDown = false; // The switch that determins which debug message is sent over serial.

// Lighting Variables:
bool DrawerIsIncrementing = true; // A switch to keep track of which + or - it is currently moving in.
bool CabinetIsIncrementing = true; // the same function as the previous, but with its own variable set.
int DrawerBreathingValue = 135; // The initial breathing value, about half but aligned to reach 255 exactly.
int CabinetBreathingValue = 135; // Not really used, the inside cabinet light is off unless a switch is grounded
int BreathingIncrement = 7; // When we are incrementing, by how much?
int BreathingDecrement = 14; // this will cause it to dim more rapidly

// https://www.arduino.cc/reference/en/language/functions/time/millis/
// Timing Debug/Constants:
String DebugZero = "Milliseconds last cycle (Δ Time): ";

// https://learn.adafruit.com/thermistor/using-a-thermistor
// https://learn.adafruit.com/assets/571
// Tempurature Debug/Constants:
String DebugOne = "Temperature: ";
String DebugTwo = "°C ~ Desired: ";
String DebugThree = "°C ~ Differential: ";
String DebugFour = " ~ PWM Zero: ";

// Relay Debug/Constants:
String DebugFive = "Relay state: ";
String DebugSix = "Online";
String DebugSeven = "Shutting down";
String DebugEight = "Offline";

// Lighting Debug/Constants:
String DebugNine = "Drawer PWM: "; // This one should show the PWM strength as well as:
String DebugTen = " ~ Cabinet PWM: "; // Show 0 if the switch is open and 255 if the pin is grounded

// Debug Debug/Constants:
// https://www.arduino.cc/reference/en/language/structure/control-structure/goto/
String DebugEleven = "DebugFirstPass.";
String DebugTwelve = "DebugSecondPass.";

// This is supposed to facilitate pure software reset.
// https://www.instructables.com/id/two-ways-to-reset-arduino-in-software/
void(* resetFunc) (void) = 0;//declare reset function at address 0

void setup() 
{
  //pinMode(BoardThermisterPin, INPUT); // A1 (or A0) may be swapped in defines.
  //pinMode(RemoteThermisterPin, INPUT); // A0
  pinMode(FanMOSFET, OUTPUT); // Winter Cooler output.
  pinMode(RelayOutput, OUTPUT); // Relay output.
  digitalWrite(RelayOutput, HIGH); // Default is set to HIGH so it can power itself through the 12V supply barrel jack
  ShuttingDown = false; // Initial shutdown debug setting
  pinMode(PowerButton, INPUT_PULLUP); // Power button closes to ground
  pinMode(DrawerLightingMOSFET, OUTPUT); // Drawer/Exterior 12V LED strip
  pinMode(CabinetLightingMOSFET, OUTPUT); // Cabinet/Interior 12V LED strip
  pinMode(DrawerSwitch, INPUT_PULLUP); // All Drawer microswitches are parrallel and close to ground.
  //pinMode(CabinetSwitch, INPUT_PULLUP); // Unused, the system uses parrallel switches.
  //pinMode(DopplerInput, INPUT); // currently unused, will this setting be correct if it puts out a 3.3V logic?
  AverageTemperature = DesiredAmbient;
  analogReference(EXTERNAL);
  Serial.begin(9600);
  while (!Serial) 
  {
    delay(10); // wait for serial port to connect. Needed for native USB port only
  }
}

void loop() 
{
  DeltaTime = millis();
  DeltaTime -= LastMillis;
  if (ButtonMillisCooldown > 0)
  {
    ButtonMillisCooldown -= DeltaTime; // Reduce this cooldown by DeltaTime. DeltaTime: 150 = (DeltaTime: 150 - 100) = 50; // ms
  }
  if ((digitalRead(PowerButton) == LOW) && (ButtonMillisCooldown <= 0))// The power button has been closed to ground, and the cooldown has at least elapsed.
  {
    digitalWrite(RelayOutput, 0); // Write LOW to the relay's MOSFET
    ButtonMillisCooldown = 100; // The button grounded has been acted upon, set a 2 second delay before it can be again.
    ShuttingDown = true; // Enable shutdown debug output, the pin has already been written to.
  }
  DebugFirstPass = true; // Not a result of goto
SecondPass:              // the goto
  Serial.println(DebugZero + DeltaTime); // "Milliseconds last cycle (Δ Time): %value" *newline*
  if (DoSerial)
  {
    SerialFunction();
  }
  if (DoLighting)
  {
    LightingFunction();
    Serial.print(DebugNine + DrawerBreathingValue); // "Drawer PWM: %value"
    Serial.println(DebugTen + CabinetBreathingValue); // " ~ Cabinet PWM: %value" *newline*
  }
  if (DoTemperature)
  {
    TemperatureFunction();
    Serial.print(DebugOne + Steinhart); // "Temperature: %value"
    Serial.print(DebugTwo + DesiredAmbient); // "°C ~ Desired: %value"
    Serial.print(DebugThree + Differential); // "°C ~ Differential: %value"
    Serial.println(DebugFour + FanPWM); // " ~ PWM Zero: %value" *newline*
  }
  if (DoRelay)
  {
    if ((ShuttingDown == true) && (DebugFirstPass == true)) // Jump through the barely necissary hoops to display the relay's state.
    {
      Serial.println(DebugFive + DebugSeven); // "Relay state: " + "Shutting down" *newline*
      DebugFirstPass = false;
      Serial.println(DebugEleven); // "DebugFirstPass."
      delay(1000);
      goto SecondPass;
    }
    else if ((ShuttingDown == true) && (DebugFirstPass == false))
    {
      Serial.println(DebugFive + DebugEight); // "Relay state: " + "Offline" *newline*
      Serial.println(DebugTwelve); // "DebugSecondPass."
      DebugFirstPass = true;
    }
    else
    {
      Serial.println(DebugFive + DebugSix); // "Relay state: " + "Online" *newline*
    }
  }
  //delay(350); // For the ease of reading Serial
  LastMillis = millis(); // Finally save millis to LastMillis before ending this cycle.
}

// https://learn.adafruit.com/thermistor/using-a-thermistor
// https://learn.adafruit.com/assets/571
void TemperatureFunction()
{
  for (SampleIndex = 0; SampleIndex < SamplesCount; SampleIndex++) 
  {
    Samples[SampleIndex] = analogRead(ThermisterPin);
    delay(10);
  }
  // average all the samples out
  AverageTemperature = 0;
  for (SampleIndex = 0; SampleIndex < SamplesCount; SampleIndex++) 
  {
     AverageTemperature += Samples[SampleIndex];
  }
  AverageTemperature /= SamplesCount;
 
  // convert the value to resistance
  AverageTemperature = 1023 / AverageTemperature - 1;
  AverageTemperature = SeriesResistor / AverageTemperature;
 
  Steinhart = AverageTemperature / ThermisterNominal; // (R/Ro)
  Steinhart = log(Steinhart);                         // ln(R/Ro)
  Steinhart /= BCoefficient;                          // 1/B * ln(R/Ro)
  Steinhart += 1.0 / (TemperatureNominal + 273.15);   // + (1/To)
  Steinhart = 1.0 / Steinhart;                        // Invert
  Steinhart -= 273.15;                                // convert to °C

  Differential = constrain((Steinhart - DesiredAmbient), 0, 5); // optimized by @FactoryFactory#4847 
  // Differential = EliminateNegative(Steinhart - DesiredAmbient); // store the unsigned short difference in tempurature
  
  FanPWM = PWMClamp(map(Differential, 0, 5, 0, 255)); // 0°C differnace, no power. +5°C differance, full power.
  // digitalWrite(FanMOSFET, FanPWM);
  analogWrite(FanMOSFET, FanPWM); // Changed back to analogWrite by @FactoryFactory#4847 request. pin 3
}

void LightingFunction()
{
  if(digitalRead(DrawerSwitch) == LOW) // The switch has been released grounding the DrawerSwitch.
  {
    digitalWrite(CabinetLightingMOSFET, HIGH); // Turn the interior cabinet LED strip on. pin 6
    CabinetBreathingValue = 255; // Set the value unused by PWM to match the write, because it does not get modulated.
    digitalWrite(DrawerLightingMOSFET, HIGH); // Write the maximum value directly to pin 5
    DrawerBreathingValue = 255; // Set the value used by PWM to match the write, and
                                // when the PWM resumes DrawerIsIncrementing will be set to false and dim out
    delay(PWMChangeDelay);
  }
  else // Do the modulating of the breathing nightlight. and turn off the interior light.
  {
    analogWrite(DrawerLightingMOSFET, DrawerLightingClamp()); // Write the oscilating DrawerBreathingValue retrieved 
                                                               // from its clamp. pin 5
    analogWrite(CabinetLightingMOSFET, LOW); // Turn the strip off. pin 6
    CabinetBreathingValue = 0; // Set the value used by PWM to match the write
    delay(PWMChangeDelay);
  }

  // if(digitalRead(DrawerSwitch) != LOW) // CabinetSwitch is unused, all switches are paralleled together
}

int DrawerLightingClamp() // Steps through incrementing, or decrementing as well as clamping the value for use with PWM
{
  if(DrawerIsIncrementing == true) // Move positivly.
  {
    DrawerBreathingValue += BreathingIncrement; // DrawerBreathingValue = (DrawerBreathingValue + BreathingIncrement);
    
    if(DrawerBreathingValue >= 255)
    {
      DrawerIsIncrementing = false; // Value was 255 or over, The next move will be decrementing.
      DrawerBreathingValue = 255; // Clamp it to 255 maximum.
    }
  }
  else if(DrawerIsIncrementing == false) // Move negatively.
  {
    DrawerBreathingValue -= BreathingDecrement; // DrawerBreathingValue = (DrawerBreathingValue - BreathingDecrement);
    
    if(DrawerBreathingValue <= 0)
    {
      DrawerIsIncrementing = true; // Value was 0 or negative, The next move will be incrementing.
      DrawerBreathingValue = 0; // Clamp it to 0 minimum.
    }
  }

  return DrawerBreathingValue; // return the processed value.
}

int CabinetLightingClamp() // Is not used, but functions the exact same way as DrawerLightingClamp()
{
  if(CabinetIsIncrementing == true) // Move positivly.
  {
    CabinetBreathingValue += BreathingIncrement;
    
    if(CabinetBreathingValue >= 255)
    {
      CabinetIsIncrementing = false; // Value was 255 or over, The next move will be decrementing.
      CabinetBreathingValue = 255; // Clamp it to 255 maximum.
    }
  }
  else if(CabinetIsIncrementing == false) // Move negatively.
  {
    CabinetBreathingValue -= BreathingDecrement;
    
    if(CabinetBreathingValue <= 0)
    {
     CabinetIsIncrementing = true; // Value was 0 or negative, The next move will be incrementing.
     CabinetBreathingValue = 0; // Clamp it to 0 minimum.
    }
  }
  
  return CabinetBreathingValue; // return the processed value.
}

void SerialFunction() // I'm honestly not sure if this is working the HC-06 Serial Slave Modules 
                // are on the messy desk still in their protective bag ;)
{
  while (Serial.available() > 0) 
  {
    int inChar = Serial.read();
    if (isDigit(inChar)) 
    {
      // convert the incoming byte to a char and add it to the string:
      inString += (char)inChar;
    }
    
    // if you get a newline, print the string, then the string's value:
    if (inChar == '\n')
    {
      if (inString.equals("warmer")) // mock ups, this will eventually handle a long string and pull variables out of it. 
      {
        DesiredAmbient += 0.40; // Plus 1
      }
      if (inString.equals("colder")) 
      {
        DesiredAmbient -= 0.10f; // Minus 2 Equals 2... ;)
      }
      if (inString.equals("reset"))
      {
        Serial.println("Resetting");
        resetFunc(); //call reset 
      }
    }
  }
  inString = ""; // clear the string for new input:
}

int PWMClamp(float PWM)
{
  if(PWM >= 255)
    return 255;
   else if (PWM <= 46) // the minimum 48,64 worked alright 32 had a greater effect if the 
                       // fan was already spinning, but still rotated the blades
    return 0;
   else
    return PWM;
}

/* Serial command code workspace:
  
// Commands constants:
static char    cmd[CMDSIZE];              // command string buffer
static char *  bufp;                      // buffer pointer
static char *  bufe = cmd + CMDSIZE - 1;  // buffer end
  Command(??)
    int value = 0;
    for (char * cmdp = cmd; *cmdp != '\0'; ++cmdp) 
    {
      if ((*cmdp >= '0') && (*cmdp <= '9')) 
      {
        value *= 10;
        value += *cmdp - '0';
      }
      else 
      {
        // optionally return error
        return;
      }
    }
    DesiredAmbient = value;
  }
// Chopping block:
  float EliminateNegative(float temp) // Just clamps a float to positive number. unsigned long/short gave useless numbers i.e. 65531
  {
    if (temp <= 0)
    {
      return 0;
    }
    else 
    {
      return temp;
    }
  }
  
*/
