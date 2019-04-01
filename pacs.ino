
#include <DHT.h>
#include <TM1637.h>
#include <Wire.h>
#include <SeeedOLED.h>

#define DEBUG 1

#define PIN_DUST 6

#define PIN_DIGIT_CLK 2
#define PIN_DIGIT_DIO 3

#define PIN_DHT 4
#define DHTTYPE DHT22

DHT dht(PIN_DHT, DHTTYPE);
TM1637 digits(PIN_DIGIT_CLK, PIN_DIGIT_DIO);

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
   int fanRatio = 100;
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

   digits.clearDisplay();
   digits.set();

   dust.init();
}

void loop()
{
   delay(500);

   updateDHT();
   dust.update();

   digitDot = !digitDot;

   digits.point(digitDot);
   digits.displayNum(state.temperature, 2, false);

   char buf[32];
   line = 0;

   // SeeedOled.clearDisplay();
   SeeedOled.setTextXY(0, 0);

   displayLine("   -= PACS =-");
   line++;

   sprintFloat(buf, "Temp: %sC", state.temperature);
   displayLine(buf);

   sprintFloat(buf, "Humi: %s%%", state.humidity);
   displayLine(buf);

   sprintFloat(buf, "Dust LPO: %s", state.concentration);
   displayLine(buf);

   int speed = 100;
   sprintf(buf, "Fan Speed: %d%%", 0);
   displayLine(buf);

   Serial.flush();
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

void updateDHT()
{
   float h = dht.readHumidity();
   float t = dht.readTemperature();

   float dust = 0;

   // Check if any reads failed and exit early (to try again).
   if (isnan(h) || isnan(t))
   {
      Serial.println(F("Failed to read from DHT sensor!"));
   }

   state.temperature = t;
   state.humidity = h;
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
      Serial.print(lowpulseoccupancy);
      Serial.print(",");
      Serial.print(ratio);
      Serial.print(",");
      Serial.println(concentration);
      lowpulseoccupancy = 0;
      starttime = millis();

      state.ratio = ratio;
      state.concentration = concentration;
   }
}