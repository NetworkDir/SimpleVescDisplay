#include <FlickerFreePrint.h> // Flicker-free printing to TFT
#include <ComEVesc.h> // Communication with VESC controller
#include <TFT_eSPI.h> // Hardware-specific library for TFT display
#include <SPI.h>
#include "EEPROMAnything.h" // Easy access to EEPROM storage

// Global variables for tracking speed, distance, and motor data
float trip;
float startup_total_km;
float last_total_km_stored;
float total_km;
float tacho;
float rpm;
float speed;
float watts;
float wheel_diameter;
int maxspeed;
int brightness = 255;
char fmt[10];


// Font settings for various display elements
#define SPEEDFONT &JerseyM54_82pt7b // Large font for displaying speed
#define DATAFONTSMALL2 &JerseyM54_14pt7b // Small font for other data values
#define DATAFONTSMALL &JerseyM54_18pt7b // Alternative small font
#define DATAFONTSMALLTEXT &Blockletter8pt7b // Font for smaller text labels

// Motor and wheel parameters for speed calculation
#define MOTOR_POLES 30 // Number of motor poles (30 for typical E-scooters)
#define WHEEL_DIAMETER_MM 246 // Diameter of the wheel in millimeters
#define GEAR_RAITO 1.0 // Motor pulley teeth count (1:1 gearing)
#define PI 3.141592 // Pi constant
#define SCONST 0.12 // Conversion factor from RPM to speed (km/h)

// Pin configs for the hardware

#define RXD2 22 // DISPLAY RX 22 TO VESC TX
#define TXD2 27 // DISPLAY TX 27 TO VESC RX

#define LDR_PIN 34 // Light Detecting Resistor that sits on top right of the display
#define LCD_BACK_LIGHT_PIN 21

// User-configurable settings for warnings and thresholds
int EEPROM_MAGIC_VALUE = 0; // EEPROM magic value to track saved data
#define EEPROM_UPDATE_EACH_KM 0.1 // Frequency of EEPROM updates (every 0.1 km)

int COLOR_WARNING_SPEED = TFT_RED; // Color for speed warning display
#define HIGH_SPEED_WARNING 60 // Speed threshold for warnings (60 km/h)

int COLOR_WARNING_TEMP_VESC = TFT_WHITE; // Color for VESC temperature warnings
#define VESC_TEMP_WARNING1 50 // First temperature warning threshold (50°C)
#define VESC_TEMP_WARNING2 80 // Second temperature warning threshold (80°C)

int COLOR_WARNING_TEMP_MOTOR = TFT_WHITE; // Color for motor temperature warnings
#define MOTOR_TEMP_WARNING1 80 // First motor temperature warning threshold (80°C)
#define MOTOR_TEMP_WARNING2 120 // Second motor temperature warning threshold (120°C)

int BATTERY_WARNING_COLOR = TFT_WHITE; // Color for battery voltage warnings
#define BATTERY_WARNING_HIGH 67.2 // High voltage warning threshold (67.2V)
#define BATTERY_WARNING_LOW 54.4 // Low voltage warning threshold (54.4V)
#define BATTERY_WARNING_0 48.0 // Voltage Cut warning treshold(48V)

int ERROR_WARNING_COLOR = TFT_WHITE; // Color for error warnings

#define DO_LOGO_DRAW // Uncomment if you want enable startup logo and background logo [Currently disbaled version doesn't work so don't disable!]
#define DEBUG_MODE

#ifdef DO_LOGO_DRAW
#include <PNGdec.h> // PNG decoder library
#include "startup_image.h"// PNG data for startup logo
#include "background_image.h" // PNG data for background image
PNG png; // PNG decoder instance
int16_t xpos = 0; // X position for image drawing
int16_t ypos = 0; // Y position for image drawing
#define MAX_IMAGE_WDITH 320 // Maximum image width for display
#endif

// Other settings
int Screen_refresh_delay = 50; // Delay between screen refreshes (ms)
ComEVesc UART; // VESC UART instance
HardwareSerial VescSerial(1);


TFT_eSPI tft = TFT_eSPI(); 
FlickerFreePrint<TFT_eSPI> Data1(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data2(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data3(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data4(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data5(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data6(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data7(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data8(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data9(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data10(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data11(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data1t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data2t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data3t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data4t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data5t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data6t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data7t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data8t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data9t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data10t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data11t(&tft, TFT_WHITE, TFT_BLACK);

void pngDraw(PNGDRAW * pDraw) {
  uint16_t lineBuffer[MAX_IMAGE_WDITH];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  tft.pushImage(xpos, ypos + pDraw -> y, pDraw -> iWidth, 1, lineBuffer);
}

void checkvalues() {
  total_km = startup_total_km + trip;
  bool traveled_enough_distance = (total_km - last_total_km_stored >= EEPROM_UPDATE_EACH_KM);
  if (traveled_enough_distance) {
    last_total_km_stored = total_km;
    EEPROM_writeAnything(EEPROM_MAGIC_VALUE, total_km);
  }
}

void setup(void) {
  VescSerial.begin(115200, SERIAL_8N1, RXD2, TXD2); // VESC RX TX
  // Wait for the Serial port to be ready
  while (!VescSerial) {
    delay(10);
  }
  UART.setSerialPort(&VescSerial);
  #ifdef DEBUG_MODE
  Serial.begin(115200); // Debug MicroUSB ?
  UART.setDebugPort(&Serial);
  #endif
  tft.begin();
  EEPROM.begin(100);
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  UART.getVescValues();
  EEPROM_readAnything(EEPROM_MAGIC_VALUE, startup_total_km);
  if (isnan(startup_total_km)) {
    tft.setCursor(40, 160);
    tft.setTextFont(4);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.print("SETUP EPROM...");
    for (int i = 0; i <100; i++){
	    EEPROM_writeAnything(i,0);
    }
    EEPROM_writeAnything(EEPROM_MAGIC_VALUE,0.0);
    delay(1500);
    ESP.restart();
  }
  last_total_km_stored = startup_total_km;
  tacho = (UART.data.tachometerAbs / (MOTOR_POLES * 3));
  trip = tacho / 1000;
  if (startup_total_km != 0) {
    startup_total_km = startup_total_km - trip;
  }

  #ifdef DO_LOGO_DRAW
  int16_t rc_bg = png.openFLASH((uint8_t * ) startup_image, sizeof(startup_image), pngDraw);
  if (rc_bg == PNG_SUCCESS) {
    tft.startWrite();
    rc_bg = png.decode(NULL, 0);
    tft.endWrite();
  }
  png.close();
  delay(3000);
  tft.fillScreen(TFT_BLACK);
  int16_t rc_mainbg = png.openFLASH((uint8_t * ) background_image, sizeof(background_image), pngDraw);
  if (rc_mainbg == PNG_SUCCESS) {
    tft.startWrite();
    rc_mainbg = png.decode(NULL, 0);
    tft.endWrite();
  }
  png.close();
  #endif
}
void loop() {
  UART.getVescValues();
  tacho = (UART.data.tachometerAbs / (MOTOR_POLES * 3));
  rpm = (UART.data.rpm / (MOTOR_POLES / 2));
  trip = tacho / 1000;
  wheel_diameter = (PI * WHEEL_DIAMETER_MM / 1000);
  speed = ((rpm * wheel_diameter * GEAR_RAITO) / 1000) * 60;
  watts = UART.data.inpVoltage*UART.data.avgInputCurrent;
  int sensorValue = analogRead(LDR_PIN);
  if(sensorValue <= 200){sensorValue = 200;}
  brightness = map(sensorValue, 200, 1000, 255, 25);
  if(brightness <= 25){brightness=25;} //stupid map function gets destroyed when input value goes over specified 
  analogWrite(LCD_BACK_LIGHT_PIN, brightness);

  //Main Speed --------------------------------------------------------------------------

  int speedINT = _max(speed, 0);
  if (speedINT > HIGH_SPEED_WARNING) {
    COLOR_WARNING_SPEED = TFT_RED;
  } else {
    COLOR_WARNING_SPEED = TFT_WHITE;
  }
  Data1.setTextColor(TFT_BLACK, TFT_BLACK);
  
  tft.setFreeFont(SPEEDFONT);
  tft.setCursor(83, 162);
  Data1.setTextColor(COLOR_WARNING_SPEED, TFT_BLACK);
  Data1.print(speedINT);

  //Vesc Temp --------------------------------------------------------------------------

  if (UART.data.tempMosfet > VESC_TEMP_WARNING1) {
    COLOR_WARNING_TEMP_VESC = TFT_YELLOW;
  }
  if (UART.data.tempMosfet > VESC_TEMP_WARNING2) {
    COLOR_WARNING_TEMP_VESC = TFT_RED;
  } else {
    COLOR_WARNING_TEMP_VESC = TFT_GREEN;
  }

    tft.setCursor(5, 220);
    tft.setFreeFont(DATAFONTSMALL);
    Data2.setTextColor(COLOR_WARNING_TEMP_VESC, TFT_BLACK);
    dtostrf(UART.data.tempMosfet, 3, 0, fmt);
    Data2.print(fmt);

    tft.setCursor(15, 235);
    tft.setFreeFont(DATAFONTSMALLTEXT);
    Data2t.setTextColor(TFT_WHITE, TFT_BLACK);
    Data2t.print("VescT"); 
  
  //Motor Temp --------------------------------------------------------------------------

  if (UART.data.tempMotor > MOTOR_TEMP_WARNING1) {
    COLOR_WARNING_TEMP_MOTOR = TFT_YELLOW;
  }
  if (UART.data.tempMotor > MOTOR_TEMP_WARNING2) {
    COLOR_WARNING_TEMP_MOTOR = TFT_RED;
  } else {
    COLOR_WARNING_TEMP_MOTOR = TFT_GREEN;
  }
  
    tft.setCursor(65, 220);
    tft.setFreeFont(DATAFONTSMALL);
    Data11.setTextColor(COLOR_WARNING_TEMP_MOTOR, TFT_BLACK);
    dtostrf(UART.data.tempMotor, 3, 0, fmt);
    Data11.print(fmt);

    tft.setCursor(65, 235);
    tft.setFreeFont(DATAFONTSMALLTEXT);
    Data11t.setTextColor(TFT_WHITE, TFT_BLACK);
    Data11t.print("MotorT"); 

  //Battery Voltage --------------------------------------------------------------------------

  if (UART.data.inpVoltage > BATTERY_WARNING_HIGH) {
    BATTERY_WARNING_COLOR = TFT_RED;
  } else if (UART.data.inpVoltage < BATTERY_WARNING_0) {
    BATTERY_WARNING_COLOR = TFT_RED;
  } else if (UART.data.inpVoltage < BATTERY_WARNING_LOW) {
    BATTERY_WARNING_COLOR = TFT_YELLOW;
  } else if (BATTERY_WARNING_LOW < UART.data.inpVoltage < BATTERY_WARNING_HIGH) {
    BATTERY_WARNING_COLOR = TFT_GREEN;
  }

  tft.setFreeFont(DATAFONTSMALL2);
  Data4.setTextColor(BATTERY_WARNING_COLOR, TFT_BLACK);
  tft.setCursor(10, 25);
  dtostrf(UART.data.inpVoltage, 3, 1, fmt);
  Data4.print(fmt);

  tft.setCursor(10, 30);
  tft.setTextFont(1);
  Data4t.setTextColor(TFT_WHITE, TFT_BLACK);
  Data4t.print("Battery");

    //Watt/Error display --------------------------------------------------------------------------

  if (UART.data.error == 0){ // Display Watts when no error(0)
	  tft.setFreeFont(DATAFONTSMALL2);
	  Data10.setTextColor(TFT_WHITE, TFT_BLACK);
	  tft.setCursor(260, 25);
	  dtostrf(watts, 5, 0, fmt);
	  Data10.print(fmt);
	
	  tft.setCursor	(270, 30);
	  tft.setTextFont(1);
	  Data10t.setTextColor(TFT_WHITE, TFT_BLACK);
	  Data10t.print("Watts");
  }else{ //	Error display when present
	  if (UART.data.error > 0) {
	  	ERROR_WARNING_COLOR = TFT_RED;
	  }else{
	    ERROR_WARNING_COLOR = TFT_GREEN;
	  }
	  tft.setFreeFont(DATAFONTSMALL2);
	  Data10.setTextColor(ERROR_WARNING_COLOR, TFT_BLACK);
	  tft.setCursor(270, 25);
	  dtostrf(UART.data.error, 2, 0, fmt);
	  Data10.print(fmt);
				
	  tft.setCursor(270, 30);
	  tft.setTextFont(1);
	  Data10t.setTextColor(TFT_WHITE, TFT_BLACK);
	  Data10t.print("Error");
  }

  //Motor-Phase Current --------------------------------------------------------------------------

  tft.setCursor(270, 220);
  tft.setFreeFont(DATAFONTSMALL);
  Data6.setTextColor(TFT_GREEN, TFT_BLACK);
  dtostrf(UART.data.avgMotorCurrent, 3, 0, fmt);
  Data6.print(fmt);

  tft.setCursor(265, 235);
  tft.setFreeFont(DATAFONTSMALLTEXT);
  Data6t.setTextColor(TFT_WHITE, TFT_BLACK);
  Data6t.print("PHASE A");
  
  //Battery Current --------------------------------------------------------------------------

  tft.setCursor(220, 220);
  tft.setFreeFont(DATAFONTSMALL);
  Data7.setTextColor(TFT_GREEN, TFT_BLACK);
  dtostrf(UART.data.avgInputCurrent, 3, 0, fmt);
  Data7.print(fmt);

  tft.setCursor(215, 235);
  tft.setFreeFont(DATAFONTSMALLTEXT);
  Data7t.setTextColor(TFT_WHITE, TFT_BLACK);
  Data7t.print("BATT A");

  //Odometer Text (TRIP) --------------------------------------------------------------------------

  tft.setCursor(145, 220);
  tft.setFreeFont(DATAFONTSMALL);

  Data9.setTextColor(TFT_WHITE, TFT_BLACK);
  dtostrf(total_km, 4, 0, fmt);
  Data9.print(fmt);

  tft.setCursor(135, 235);
  tft.setFreeFont(DATAFONTSMALLTEXT);
  Data9t.setTextColor(TFT_WHITE, TFT_BLACK);
  Data9t.print("ODOMETER");
  
  delay(Screen_refresh_delay);
  checkvalues();
}
