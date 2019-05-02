
#include <DHT.h>
#include <TM1637Display.h>
#include <Wire.h>
#include <SeeedOLED.h>
#include <dmtimer.h>
#include "EmonLib.h"             // Include Emon Library

#define DEBUG 1

#define TEMPERATURE_MIN 20 // temperature the fan will start spinning
#define TEMPERATURE_MAX 70 // temperature the fan will get to it's max speed
#define FAN_SPEED_MIN 0.15f // under 15% it's not worth it

#define PIN_TEMP_DIGIT_CLK 2
#define PIN_TEMP_DIGIT_DIO 3

#define PIN_POW_DIGIT_CLK 4
#define PIN_POW_DIGIT_DIO 5

#define PIN_DHT 6
#define PIN_MOSFET 7
#define PIN_DUST 8

#define PIN_POWER1 0
#define PIN_POWER2 1

#define DHTTYPE AM2301

DHT dht(PIN_DHT, DHTTYPE);
TM1637Display tempDisplay(PIN_TEMP_DIGIT_CLK, PIN_TEMP_DIGIT_DIO);
TM1637Display powerDiplay(PIN_POW_DIGIT_CLK, PIN_POW_DIGIT_DIO);

DMTimer screenTimer(100000);
DMTimer dhtTimer(100000);
DMTimer dotTimer(500000);

EnergyMonitor emon;

#define DEG_CHAR (SEG_A | SEG_B | SEG_G | SEG_F)
#define A_CHAR 0b01110111
#define C_CHAR 0b00111001
#define D_CHAR 0b01011110

void updateDHT();
void displayNum(TM1637Display& display, int num, bool dot) ;
void displayLine(const char *msg);
void sprintFloat(char *buf, const char *format, float stat);

class DustSensor
{
 public:
   void update();
   void init();

   unsigned long duration;
   unsigned long starttime;
   unsigned long sampletime_ms = 30000; //sample 30s ;
   unsigned long lowpulseoccupancy = 0;

} dust;

struct
{
   float ratio = 0;
   float concentration = 0;
   float temperature;
   float humidity;
   float fanRatio = 1;

   float amps = 0;
   float volt = 0;
} state;

bool digitDot = false;
int line = 0;

void setup()
{
#ifdef DEBUG
   Serial.begin(9600);
#endif

   Serial.println("PI Active Cooling System");

   dht.begin();
   Wire.begin();
   SeeedOled.init();

   SeeedOled.clearDisplay();     //clear the screen and set start position to top left corner
   SeeedOled.setNormalDisplay(); //Set display to normal mode (i.e non-inverse mode)
   SeeedOled.setPageMode();      //Set addressing mode to Page Mode
   SeeedOled.deactivateScroll();

   tempDisplay.setBrightness(7, true);
   tempDisplay.clear();
   powerDiplay.setBrightness(7, true);
   powerDiplay.clear();

   pinMode(PIN_MOSFET, OUTPUT);

   emon.current(PIN_POWER1, 50);       // Current: input pin, calibration.
   // emon.voltage(PIN_POWER2, 234.26, 1.7);  // Voltage: input pin, calibration, phase_shift


   dust.init();
}

void loop()
{

   if (dhtTimer.isTimeReached())
   {
      updateDHT();
      analogWrite(PIN_MOSFET, int(state.fanRatio * 255) );

      int temp = int(state.temperature);
      displayNum(tempDisplay, temp, digitDot);

      Serial.print("temp update: ");
      Serial.print(temp);
      Serial.println();

      uint8_t data[] = { DEG_CHAR, C_CHAR };
      tempDisplay.setSegments(data, 2, 2);
   }

   float vcc = analogRead(PIN_POWER1) * 0.0049;
   double Irms = emon.calcIrms(1480);

   if (dotTimer.isTimeReached())
   {
      digitDot = !digitDot;

      int temp = int(state.temperature);
      displayNum(tempDisplay, temp, digitDot);

      // emon.serialprint();
   }

   if (screenTimer.isTimeReached())
   {
      char buf[32];
      line = 0;

      SeeedOled.setTextXY(0, 0);

      displayLine("   -= PACS =-");
      // line++;

      sprintFloat(buf, "Temp: %sC", state.temperature);
      displayLine(buf);

      sprintFloat(buf, "Humi: %s%%", state.humidity);
      displayLine(buf);

      sprintFloat(buf, "Dust LPO: %s", state.concentration);
      displayLine(buf);

      // sprintf(buf, "Fan Speed: %3d%%", int(state.fanRatio * 100));
      // displayLine(buf);

      sprintFloat(buf, "Pin: %sv ", vcc);
      displayLine(buf);

      sprintFloat(buf, "Irms: %s ", Irms);
      displayLine(buf);

      sprintf(buf, "Rms: %dw ", int(Irms*120));
      displayLine(buf);
   }

   // Serial.flush();
}

void displayLine(const char *msg)
{
   SeeedOled.setTextXY(line, 0);
   SeeedOled.putString(msg);
   line += 1;
}

void sprintFloat(char *buf, const char *format, float stat)
{
   char temp[6];
   /* 4 is mininum width, 2 is precision; float value is copied onto str_temp*/
   dtostrf(stat, 4, 2, temp);

   sprintf(buf, format, temp);
}

void displayNum(TM1637Display& display, int num, bool dot) 
{
   int dotbytes = 0;
   if (dot)
   {
      dotbytes = 0b01000000;
   }
   display.showNumberDecEx(num, dotbytes, true, 2, 0);
}

void updateDHT()
{
   float h = dht.readHumidity();
   float t = dht.readTemperature(false);

   float dust = 0;

   // Check if any reads failed and exit early (to try again).
   if (isnan(h) || isnan(t))
   {
      Serial.println(F("Failed to read from DHT sensor!"));
      return;
   }

   state.temperature = t;
   state.humidity = h;

   state.fanRatio = (t - TEMPERATURE_MIN) / TEMPERATURE_MAX;
   
   if (state.fanRatio > 1.0f) 
   {
      state.fanRatio = 1;
   }
   else if (state.fanRatio < FAN_SPEED_MIN)
   {
      state.fanRatio = 0;
   }
}

void DustSensor::init()
{
   pinMode(PIN_DUST, INPUT);
}

void DustSensor::update()
{
   duration = pulseIn(PIN_DUST, LOW);
   lowpulseoccupancy = lowpulseoccupancy + duration;

   if ((millis() - starttime) > sampletime_ms) //if the sample time == 30s
   {
      float ratio = 0;
      float concentration = 0;

      ratio = lowpulseoccupancy / (sampletime_ms * 10.0);                             // Integer percentage 0=>100
      concentration = 1.1 * pow(ratio, 3) - 3.8 * pow(ratio, 2) + 520 * ratio + 0.62; // using spec sheet curve
      // Serial.print(lowpulseoccupancy);
      // Serial.print(",");
      // Serial.print(ratio);
      // Serial.print(",");
      // Serial.println(concentration);
      Serial.print("concentration: ");
      Serial.println(state.concentration);
      lowpulseoccupancy = 0;
      starttime = millis();

      state.ratio = ratio;
      state.concentration = concentration;
   }
}
