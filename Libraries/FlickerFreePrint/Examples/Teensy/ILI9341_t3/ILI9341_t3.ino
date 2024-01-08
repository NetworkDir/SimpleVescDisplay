#include <ILI9341_t3.h>           // fast display driver lib
#include <FlickerFreePrint.h>     // library to draw w/o flicker
#include <font_Arial.h>           // custom fonts for the ILI9341_t3.h

// some defines for pins
#define TFT_CS        10
#define TFT_DC        9
#define TFT_LCD       A9

// setup some colors
#define C_BLACK      0x0000
#define C_BLUE        0x001F
#define C_RED         0xF800
#define C_GREEN       0x07E0
#define C_CYAN        0x07FF
#define C_MAGENTA     0xF81F
#define C_YELLOW      0xFFE0
#define C_WHITE       0xFFFF

// create the display object
ILI9341_t3 Display(TFT_CS, TFT_DC);

// create a flicker free pnject for each data to be printed with the flicker free option
// the library used template scheme so you need to pass the data type in <>
FlickerFreePrint<ILI9341_t3> Data1(&Display, C_WHITE, C_BLACK);
FlickerFreePrint<ILI9341_t3> Data2(&Display, C_WHITE, C_BLACK);

// setup some variables

float j;
unsigned long StartTime, EndTime;
char str[30];

void setup() {

  // you know the drill
  Serial.begin(9600);

  pinMode(TFT_LCD,   OUTPUT);

  // i typically put LCD to a PWM pin so i can control brightness via code
  digitalWrite(TFT_LCD, HIGH);

  // typicall display begin
  Display.begin();
  Display.setRotation(1);
  Display.fillScreen(C_BLACK);

  Display.fillRect(0, 0, 319, 35, C_RED);
  Display.fillRect(0, 130, 319, 35, C_BLUE);
  Display.setFont(Arial_18);
}

void loop() {

  // get some data to paint
  j += 0.0013;


  Display.setCursor(10, 10);
  Display.setTextColor(C_WHITE, C_RED);
  Display.print("fillRect(): ");

  Display.setCursor(180, 10);
  StartTime = millis();

  // this is a typical way to paint over old data
  // just draw a rectangle over the old then paint the new
  // note if you are using proportional fonts, just specify the
  // font background color and no need to paint over the old
  // Display.setTextColor(TextForeColor, TextBackColor)

  Display.fillRect(180, 10, 100, 25, C_RED);
  Display.print(j, 4);

  // let's see the time to paint the rect...
  EndTime = millis() - StartTime;

  sprintf(str, "Update time: %lu", EndTime);

  Display.setTextColor(C_WHITE, C_BLACK);
  Display.fillRect(160, 37, 100, 25, C_BLACK);
  Display.setCursor(10, 40);
  Display.print(str);

  Display.fillRect(5, 110, 300, 5, C_WHITE);


  // now let's see the display update with FlickerFree
  Display.setCursor(10, 140);
  Display.setTextColor(C_WHITE, C_BLUE);
  Display.print("FlickerFree: ");

  // set cursor as you would normall do
  Display.setCursor(180, 140);
  // you must call the text colors for the object
  Data1.setTextColor(C_WHITE, C_BLUE);
  StartTime = millis();

  // one call and flicker free paint is done
  Data1.print(j, 4);

  // lets see how fast
  EndTime = millis() - StartTime;

  Display.setTextColor(C_WHITE, C_BLACK);
  Display.setCursor(10, 170);

  sprintf(str, "Update time: %lu", EndTime);

  Display.setCursor(10, 170);
  Data2.setTextColor(C_WHITE, C_BLACK);
  Data2.print(str);

  delay(100);

}
