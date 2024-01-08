#include <FlickerFreePrint.h> // https://github.com/KrisKasprzak/FlickerFreePrint
#include <VescUart.h> // https://github.com/SolidGeek/VescUart
#include <TFT_eSPI.h> // Hardware-specific library // https://github.com/Bodmer/TFT_eSPI
#include <SPI.h>
#include "EEPROMAnything.h"

float trip;
float startup_total_km; // read from eeprom 
float last_total_km_stored; 
float total_km;

float tacho;
float rpm;
float speed;
int maxspeed;

float MOTOR_POLES;
char fmt[10];


//******************************************************FONTS USED**********************************************************************************
//MAKE SURE YOU HAVE THEM IN Arduino\libraries\TFT_eSPI\Fonts\Custom AND USER_CONFIG
// AND Arduino\libraries\TFT_eSPI\User_Setups\User_Custom_Fonts.h IS MODIFIED ACCORDINGLY IF THE LIBRARY I PROVIDED IS NOT USED.

#define SPEEDFONT &JerseyM54_82pt7b // BIG NUMBERS FOR MAIN SPEED
#define DATAFONTSMALL2 &JerseyM54_14pt7b //Smaller numbers for other values
#define DATAFONTSMALL &JerseyM54_18pt7b //Small numbers 
#define DATAFONTSMALLTEXT &Blockletter8pt7b
//******************************************************FONTS USED**********************************************************************************

//******************************************************WHEEL SETTINGS START******************************************************************************
#define MOTOR_POLE_PAIRS 30 //motor pole PAIRS!
#define WHEEL_DIAMETER_MM 254 // diameter of wheel in mm
#define MOTOR_PULLEY_TEETH 1  // using a rear wheel hub gearing will be 1/1
#define WHEEL_PULLEY_TEETH 1  // using a rear wheel hub gearing will be 1/1
//******************************************************WHEEL SETTINGS END******************************************************************************

//******************************************************USER SETTINGS START*******************************************************************************

int EEPROM_MAGIC_VALUE = 0; // this was retained from davega/roxie to be able to store/reset odometer altho its not needed
#define EEPROM_UPDATE_EACH_KM 0.1 // to save every 0.1 km or 100 meters

int COLOR_WARNING_SPEED = TFT_RED; // color for the speed warning
#define HIGH_SPEED_WARNING 60 // kph

int COLOR_WARNING_TEMP_VESC = TFT_WHITE;
#define VESC_TEMP_WARNING1 50 // degrees
#define VESC_TEMP_WARNING2 80 // degrees

int COLOR_WARNING_TEMP_MOTOR = TFT_WHITE;
#define MOTOR_TEMP_WARNING1 50 // degrees
#define MOTOR_TEMP_WARNING2 80 // degrees

//#define BATTERY_CELL_SERIES 20 //comment this line if you want battery TOTAL to show up
int BATTERY_WARNING_COLOR = TFT_WHITE;
#define BATTERY_WARNING_HIGH 14.1 // voltage
#define BATTERY_WARNING_LOW 12.2 // voltage
#define DO_LOGO_DRAW // comment this line if you dont want logo to show at startup!
//******************************************************USER SETTINGS END*********************************************************************************

#ifdef DO_LOGO_DRAW
#include <PNGdec.h>
#include "Logo.h"
PNG png; // PNG decoder inatance
int16_t xpos = 0;
int16_t ypos = 0;
#define MAX_IMAGE_WDITH 320 // Adjust for your images
#endif

bool staticdrawn;
int Screen_refresh_delay = 100; // ms
VescUart UART;

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


FlickerFreePrint<TFT_eSPI> Data1t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data2t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data3t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data4t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data5t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data6t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data7t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data8t(&tft, TFT_WHITE, TFT_BLACK);
FlickerFreePrint<TFT_eSPI> Data9t(&tft, TFT_WHITE, TFT_BLACK);

//************************************************************* SETUP ****************************************************************************************
void setup(void) 
{
  Serial.begin(115200); // comms for debug
  Serial2.begin(115200);/** Setup UART port (FOR VESC) pins rx2 and tx2 */ 
  UART.setSerialPort(&Serial2);
  tft.begin();
  EEPROM.begin(100);
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK); 
  UART.getVescValues();
       // drawlines(); //graphics deactivated due to screenburn?!


       //for (int i = 0; i <100; i++){EEPROM_writeAnything(i,0);} // make eeprom clean or use 
      // EEPROM_writeAnything(EEPROM_MAGIC_VALUE,0.0); //--->>>> change X with your preffered mileage
      
EEPROM_readAnything(EEPROM_MAGIC_VALUE,startup_total_km);
Serial.print("am citit eeprom ");Serial.print("km = ");Serial.println(startup_total_km);
last_total_km_stored = startup_total_km;
tacho = (UART.data.tachometerAbs/(MOTOR_POLE_PAIRS*3)); 
trip = tacho / 1000; 
if (startup_total_km != 0) {startup_total_km =startup_total_km - trip;}


#ifdef DO_LOGO_DRAW
  int16_t rc = png.openFLASH((uint8_t *)svd, sizeof(svd), pngDraw);
  if (rc == PNG_SUCCESS) {
  tft.startWrite();
  rc = png.decode(NULL, 0);
  tft.endWrite();
  }
  tft.setCursor(40, 160);
  tft.setTextFont(4);
  tft.setTextDatum(MC_DATUM);
tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print("Simple Vesc Display");
delay(3000);
 tft.fillScreen(TFT_BLACK); 
#endif
} // end setup
void loop() {
  
  UART.getVescValues();
    MOTOR_POLES = MOTOR_POLE_PAIRS*2;
    tacho = (UART.data.tachometerAbs/(MOTOR_POLE_PAIRS*3));  
    rpm = (UART.data.rpm/MOTOR_POLE_PAIRS);
    trip = tacho / 1000;          
    speed = rpm/10;          
//*************************************************************SETUP END*************************************************************************************

    //*********************************************MAIN SPEED - BIG FONT TEXT********************************************************************************
         if (speed > HIGH_SPEED_WARNING) {COLOR_WARNING_SPEED=TFT_RED;} else {COLOR_WARNING_SPEED=TFT_WHITE;}
        tft.setFreeFont(SPEEDFONT); 
        tft.setCursor(35, 150);
        Data1.setTextColor(COLOR_WARNING_SPEED, TFT_BLACK);
        int speedINT = _max(speed,0);
        Data1.print(speedINT);



     //*********************************************VESC TEMPERATURE*****************************************************************************************

   if (UART.data.tempMosfet > VESC_TEMP_WARNING1) {COLOR_WARNING_TEMP_VESC=TFT_YELLOW;} 
    if ( UART.data.tempMosfet > VESC_TEMP_WARNING2) {COLOR_WARNING_TEMP_VESC=TFT_RED;} else {COLOR_WARNING_TEMP_VESC=TFT_GREEN;}
    

     tft.setCursor(20, 220);
     tft.setFreeFont(DATAFONTSMALL);
     Data2.setTextColor(COLOR_WARNING_TEMP_VESC, TFT_BLACK);
     dtostrf(UART.data.tempMosfet, 3, 0, fmt);
     Data2.print(fmt);
   
      tft.setCursor(30, 235);
      tft.setFreeFont(DATAFONTSMALLTEXT);
      Data2t.setTextColor(TFT_WHITE, TFT_BLACK);
      Data2t.print("VESC");




//*********************************************MOTOR TEMPERATURE****************************************************************************************
 bool sensorconnected = 1;
  if (UART.data.tempMotor > MOTOR_TEMP_WARNING1) {COLOR_WARNING_TEMP_MOTOR=TFT_YELLOW;} 
  if (UART.data.tempMotor < 0) {COLOR_WARNING_TEMP_MOTOR=TFT_CYAN;sensorconnected = 0;} 
 else if (UART.data.tempMotor > MOTOR_TEMP_WARNING2) {COLOR_WARNING_TEMP_MOTOR=TFT_RED;}
 else {COLOR_WARNING_TEMP_MOTOR=TFT_GREEN;} 
  
      
      tft.setCursor(125, 220);
      tft.setFreeFont(DATAFONTSMALL);
      Data3.setTextColor(COLOR_WARNING_TEMP_MOTOR, TFT_BLACK);
      int tempmotorINT = _max(UART.data.tempMotor,0);
      dtostrf(tempmotorINT, 2, 0, fmt);
    if (!sensorconnected){Data3.print("N.A");} else {Data3.print(fmt);}


        tft.setCursor(125, 235);
        tft.setFreeFont(DATAFONTSMALLTEXT);
        Data3t.setTextColor(TFT_WHITE, TFT_BLACK);
        Data3t.print("MOTOR");


//*********************************************BATTERY VOLTAGE*****************************************************************************************
  
   
  if (UART.data.inpVoltage > BATTERY_WARNING_HIGH) {BATTERY_WARNING_COLOR=TFT_RED;} 
 else if (UART.data.inpVoltage < BATTERY_WARNING_LOW) {BATTERY_WARNING_COLOR=TFT_YELLOW;} 
 else if (BATTERY_WARNING_LOW < UART.data.inpVoltage < BATTERY_WARNING_HIGH) {BATTERY_WARNING_COLOR=TFT_GREEN;} 
     
#ifdef BATTERY_CELL_SERIES
       
        
        tft.setFreeFont(DATAFONTSMALL2);
        Data4.setTextColor(BATTERY_WARNING_COLOR, TFT_BLACK);
        tft.setCursor(250, 25);dtostrf((UART.data.inpVoltage/BATTERY_CELL_SERIES), 3, 3, fmt);
        Data4.print(fmt);
  
        tft.setCursor(270, 30);
        tft.setTextFont(1);
        Data4t.setTextColor(TFT_WHITE, TFT_BLACK);
        Data4t.print("CELLS");
#else 
        ;
        tft.setFreeFont(DATAFONTSMALL2);
        Data4.setTextColor(BATTERY_WARNING_COLOR, TFT_BLACK);
        tft.setCursor(270, 25);dtostrf(UART.data.inpVoltage, 3, 1, fmt);
        Data4.print(fmt);
  
        tft.setCursor(270, 30);
        tft.setTextFont(1);
        Data4t.setTextColor(TFT_WHITE, TFT_BLACK);
        Data4t.print("BATTERY");
#endif
//*********************************************BATTERY VOLTAGE END*****************************************************************************************
//*********************************************MAX SPEED*****************************************************************************************
        tft.setCursor(270, 60);
        tft.setFreeFont(DATAFONTSMALL2);
        Data5.setTextColor(TFT_WHITE, TFT_BLACK);
        if (speedINT > maxspeed) {maxspeed = speedINT;}
        dtostrf(maxspeed, 2, 0, fmt);
        Data5.print(fmt);

        tft.setCursor(265, 65);
        tft.setTextFont(1);
        Data5t.setTextColor(TFT_WHITE, TFT_BLACK);
        Data5t.print("MAX SPD");

//*********************************************MAX SPEED END*****************************************************************************************
//*********************************************MOTOR CURRENT*****************************************************************************************
        tft.setCursor(270, 95);
        tft.setFreeFont(DATAFONTSMALL2);
        Data6.setTextColor(TFT_WHITE, TFT_BLACK);
        dtostrf(UART.data.avgMotorCurrent, 2, 0, fmt);
        Data6.print(fmt);

        tft.setCursor(265, 100);
        tft.setTextFont(1);
        Data6t.setTextColor(TFT_WHITE, TFT_BLACK);
        Data6t.print("mCURRENT");
//*********************************************MOTOR CURRENT END*****************************************************************************************
//*********************************************BATTERY CURRENT*****************************************************************************************
        tft.setCursor(270, 130);
        tft.setFreeFont(DATAFONTSMALL2);
        Data7.setTextColor(TFT_WHITE, TFT_BLACK);
        dtostrf(UART.data.avgInputCurrent, 2, 0, fmt);
        Data7.print(fmt);

        tft.setCursor(265, 135);
        tft.setTextFont(1);
        Data7t.setTextColor(TFT_WHITE, TFT_BLACK);
        Data7t.print("bCURRENT");
//*********************************************BATTERY CURRENT END*****************************************************************************************

//*********************************************TRIP DISPLAY*****************************************************************************************

        tft.setCursor(265, 165);
        tft.setFreeFont(DATAFONTSMALL2);
        Data8.setTextColor(TFT_WHITE, TFT_BLACK);
        dtostrf(trip, 3, 1, fmt);
        Data8.print(fmt);

        tft.setCursor(270, 170);
        tft.setTextFont(1);
        Data8t.setTextColor(TFT_WHITE, TFT_BLACK);
        Data8t.print("TRIP");



//*********************************************ODOMETER DISPLAY*****************************************************************************************


        tft.setCursor(230, 220);
        tft.setFreeFont(DATAFONTSMALL);
        
        Data9.setTextColor(TFT_WHITE, TFT_BLACK);
        dtostrf(total_km, 3, 1, fmt);
        Data9.print(fmt);

 tft.setCursor(230, 235);
        tft.setFreeFont(DATAFONTSMALLTEXT);
        Data9t.setTextColor(TFT_WHITE, TFT_BLACK);
        Data9t.print("ODOMETER");
//*********************************************ODOMETER DISPLAY END*****************************************************************************************



 // Serial.print(km_vesc_trip_parcursiINT);
 // Serial.print(" ");
  //Serial.println(init_data);
 // Serial.println(" ");
  //Serial.print(km_vesc_trip_parcursi);
  /*
  Serial.print(" ");Serial.print(tacho);
  Serial.print(" ");
  Serial.println(rpm);
*/
delay(Screen_refresh_delay);
checkvalues();
} // end loop

void checkvalues(){
total_km = startup_total_km + trip;
bool traveled_enough_distance = (total_km - last_total_km_stored >= EEPROM_UPDATE_EACH_KM);
if (traveled_enough_distance){
last_total_km_stored = total_km;
EEPROM_writeAnything(EEPROM_MAGIC_VALUE,total_km);
Serial.print("am scris eeprom");Serial.print("km = ");Serial.println(total_km);}

}//end check 


void drawlines(){
if (!staticdrawn){
  tft.fillScreen(TFT_BLACK); 
tft.drawFastHLine(0, 191, 320, TFT_PURPLE);
tft.drawFastHLine(0, 190, 320, TFT_PURPLE);
tft.drawFastHLine(0, 189, 320, TFT_YELLOW);
tft.drawFastHLine(0, 188, 320, TFT_PURPLE);
tft.drawFastHLine(0, 187, 320, TFT_PURPLE);

tft.drawFastVLine(0, 190, 50, TFT_PURPLE);
tft.drawFastVLine(90, 190, 50, TFT_PURPLE);
tft.drawFastVLine(200, 190, 50, TFT_PURPLE);

tft.drawFastVLine(250, 0, 189, TFT_PURPLE);
tft.drawFastVLine(249, 0, 189, TFT_YELLOW);
tft.drawFastVLine(248, 0, 189, TFT_PURPLE);

staticdrawn = 1;}
} // end drawlines
#ifdef DO_LOGO_DRAW
void pngDraw(PNGDRAW *pDraw) {
  uint16_t lineBuffer[MAX_IMAGE_WDITH];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  tft.pushImage(xpos, ypos + pDraw->y, pDraw->iWidth, 1, lineBuffer);}
  #endif