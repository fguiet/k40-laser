/*
* K40 laser cutter - Motorized bed
* Frédéric Guiet - 01/2021
* 
* I wrote this piece of software but it is inspired from the one wrote by Jean-Philippe Civade : https://www.civade.com/post/2020/08/23/D%C3%A9coupe-laser-CO2-K40-%3A-R%C3%A9alisation-d-un-lit-motoris%C3%A9
*
*/

// ****** External librairies used ******

#include <SPI.h>            // needed by TFT_ILI9163C
#include <Adafruit_GFX.h>   // Adafruit graphic library needed by TFT_ILI9163C
#include <Wire.h>           // needed by TFT_ILI9163C
#include <ezButton.h>       // Button management
#include <Encoder.h>        // Panel encoder management from Paul Stoffregen
#include <AccelStepper.h>   // To manager stepper with acceleration
#include <TFT_ILI9163C.h>   //TFT ILI9163C 128x128 Screen management

// ****** DEBUG *************************

#define DEBUG 1

// ***** FIRMWARE ***********************

#define FIRMWARE "v1.0 - 2021-01-31"

// **************************************

// ****** NEMA17 MOTOR SETTINGS *********

//* Motor NEMA 17 (https://www.amazon.fr/gp/product/B06XQWMDWT/ref=ppx_yo_dt_b_asin_title_o06_s01?ie=UTF8&psc=1)
//*
//* NEMA 17, Peak current 1.7A, 200 steps per revolution, 1.8° per step
//*
//* Driver TB6600 (https://www.amazon.fr/gp/product/B0811GSPJK/ref=ppx_yo_dt_b_asin_title_o01_s00?ie=UTF8&psc=1)
//*
//* Microstep settings
//* Stepper motors typically have a step size of 1.8° or 200 steps per revolution, this refers to full steps. A microstepping driver such as the TB6600 allows higher resolutions by allowing intermediate step locations. This is achieved by energizing the coils with intermediate current levels.
//* For instance, driving a motor in 1/2 step mode will give the 200-steps-per-revolution motor 400 microsteps per revolution.
//* You can change the TB6600 microstep settings by switching the dip switches on the driver on or off. See the table below for details. Make sure that the driver is not connected to power when you adjust the dip switches!
//*
//* Steps (1,2,3) = 0ff, On, Off = 1/8 step
//* Power (4,5,6) = On,On, Off) = 1.5A nominal / 1.7 Peak
//*
//* Wire Blue = B+
//* Wire Red  = B-
//* Wire Green = A+
//* Wire Black = A-
//
// So...
//
// See : https://fr.wikipedia.org/wiki/Filetage_m%C3%A9trique
//
// * 200 * 8 = 1600 micro-step / turn.
// * Screw M8 x 1.25 (1.25 mm per thread)
// * 1 turn = 1.25 mm = 1600 steps
// * 1mm = (1600/1.25)*1 = 1280 steps
// * 0.1mm = 128 steps

// Pin configuration Arduino to TP6600 Driver
// Arduino Digital pin 4 to PUL-(PUL)
// Arduino Digital pin 5 to DIR-(DIR)
// Arduino Digital pin 6 to ENA-(ENA)

// Stepper connections
#define  STEP_PIN 4             // Step/Pulse interface ()
#define  DIRECTION_PIN 5        // Dir interface (Nema 17 Direction)
#define  ENABLE_PIN 6           // Enable stepper motor (Enable / disable current on Nema 17)

// Stepper constants
#define STEPS_PER_MM 1280      // Number of step needed for 1 mm

// Library objects instantiation
AccelStepper bed(AccelStepper::DRIVER, STEP_PIN, DIRECTION_PIN);

#define MAX_BED_OFFSET_FROM_ORIGIN 18 //max offset from switch to bed focal zero (mm)
#define MAX_MATERIAL_THICKNESS 50  //max material thickness (mm)

#define DEFAULT_BED_OFFSET_FROM_ORIGIN 18 //Default offset from switch to bed focal zero (mm)
#define DEFAULT_MATERIAL_THICKNESS 5  //Default material thickness (mm)

#define RESET_FLASH_MEMORY_PIN 8

// ********** BED STATUS ************

enum bedStatus { FROM_ORIGIN_TO_BED_FOCAL_POINT_LEVEL, FROM_ORIGIN_TO_MATERIAL_ENGRAVE_FOCAL_POINT_LEVEL, FROM_ORIGIN_TO_MATERIAL_CUT_FOCAL_POINT_LEVEL };

// bed settings vars
typedef struct {    
    int bedOffsetFromOriginToFocalPoint_mm = DEFAULT_BED_OFFSET_FROM_ORIGIN; //Default offset from switch to bed focal zero (mm)
    int materialThickness_mm = DEFAULT_MATERIAL_THICKNESS; //Default material thickness    
    //int materialOffsetFromOriginToFocalPoint_mm = DEFAULT_BED_OFFSET_FROM_ORIGIN - DEFAULT_MATERIAL_THICKNESS;  


  //float fBedOffset;       // Default offset from switch to bed focal zero (mm)
  //float fMaterialOffset;  // Default offset from bed to surface to cut's bottom (mm)  
  //bool  bCut;             // Cut y/n (boolean)
  //float fTickness;        // Material tickness (mm). Used to compute real offset. 
                          // For engraving (bCut=0), focal point is set to material surface
                          //      Focal point = fBedOffset + fMaterialOffset + fTickness
                          // For cutting (bCut=1), focal point is is in the middle of the material
                          //      Focal point = fBedOffset + fMaterialOffset + fTickness/2 
} bedSettingsType;
bedSettingsType bedSettings;

typedef struct {    
    float materialOffsetFromOriginToEngraveFocalPoint_mm;
    float materialOffsetFromOriginToCutFocalPoint_mm;
    long materialOffsetFromOriginToBedFocalPoint_steps;
    long materialOffsetFromOriginToEngraveFocalPoint_steps;
    long materialOffsetFromOriginToCutFocalPoint_steps;
    bedStatus bed_level_status = FROM_ORIGIN_TO_MATERIAL_ENGRAVE_FOCAL_POINT_LEVEL;
    
} bedComputedSettingsType;
bedComputedSettingsType bedComputedSettings;


// ****** HOME SWITCH BUTTON SETTINGS **********

#define  HOME_SWITCH_PIN 7            // home switch (set the 0 level of the bed)

// ****** ROTARY ENCODER SETTINGS REF : HW-040 **************

// Encoder connections
// work best with interrupts enabled pin (2,3 with Arduino board)
#define ENCODER_BUTTON_PIN A0         //A0 can be used as digital pin
#define ENCODER_1_PIN 2       
#define ENCODER_2_PIN 3      
#define LONG_PRESS_TIME 500 // 500 milliseconds

// HW040 - ENCODER 
//SW = ENCODER_BUTTON_PIN
//CLK = PIN 3
//DT =  PIN 2
ezButton encoderButton(ENCODER_BUTTON_PIN);  // create ezButton object that attach to pin A0;
Encoder encoder(ENCODER_1_PIN, ENCODER_2_PIN);

// ****** DISPLAY SETTINGS **************

//
//DID YOU HAVE A RED PCB, BLACk PCB or WHAT DISPLAY TYPE???????????? 
//GOT THE BLACK ONE so I needed to edit file below otherwise screen will display glitches
//
// See https://github.com/sumotoy/TFT_ILI9163C 
//
// See TFT_ILI9163C_settings.h file
//  ---> SELECT HERE <----
//#define __144_RED_PCB__//128x128
//#define __144_BLACK_PCB__//128x128
//#define __22_RED_PCB__//240x320

//PIN Configuration

//TFT => Microcontroller

//VCC = +5v (check whether you have a voltage regulator first)
//GND = GND
//CS = D10 (See contructor)
//RESET = +5v (if you do not use RESET PIN)
//A0 = DC or RS pin = D9
//SDA = Mosi = D11 (See Arduino Datasheet)
//SCK = Clock = D13 (See Arduino Datasheet)
//LED = +5v or +3.3v

#define DISPLAY             // define to support TFT_ILI9163C display

#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0  
#define WHITE   0xFFFF

#define __CS 10
#define __DC 9 //A0

#ifdef DISPLAY
//Setup TFT Display
TFT_ILI9163C tft = TFT_ILI9163C(__CS, __DC);
#endif

// ********** MENU SETTINGS *********

enum availableOptions { MOVE_TO_BED_ORIGIN, MOVE_FROM_ORIGIN_TO_BED_FOCAL_OPTION, MOVE_FROM_ORIGIN_TO_MATERIAL_FOCAL_ENGRAVE_OPTION,
                        MOVE_FROM_ORIGIN_TO_MATERIAL_FOCAL_CUT_OPTION, MATERIAL_THICKNESS_SET_OPTION, 
                        THICKNESS_SETTINGS_OPTION, BACK_OPTION, BED_OFFSET_FROM_ORIGIN_TO_FOCAL_SET_OPTION, 
                        SAVE_SETTINGS_OPTION, SETTINGS_OPTION, CONTROL_OPTION, BED_OFFSET_FROM_ORIGIN_TO_FOCAL_OPTION };
availableOptions currentOptionSelected;

// ********** VARIABLES *************

unsigned long pressedTime  = 0; // to save when button is pressed
bool isPressing = false;        // button pressed? or not?
bool buttonPressedHandled = false; //button pressed handled ?

long absoluteBedPosition = 0;    // absolute bed position in step
long currentMaterialThickness = 0;
long currentFromOriginToBedFocalPoint = 0;

long newTicks = 0;              // handle encoder value

enum screenToDisplay { CONTROL_SCREEN, MATERIAL_THICKNESS_SCREEN, SAVING_SETTINGS_SCREEN, SPLASH_SCREEN, ORIGIN_SCREEN, 
                       DEFAULT_OFFSET_SCREEN, MAIN_MENU_SCREEN, SETTINGS_SCREEN, BED_OFFSET_FROM_ORIGIN_TO_FOCAL_SCREEN };
screenToDisplay currentScreen;
screenToDisplay oldScreen;

/*
* Debug function
*/
void debug_message(String message, bool doReturnLine) {
  if (DEBUG) {
    if (doReturnLine)
      Serial.println(message);
    else
      Serial.print(message);
    
    Serial.flush();
    delay(10);
  }
}

void writeSettings()
// be aware of eeprom longevity : only 10000 write cycles..
{
    //debug_message(F("Writing settings in flash memory..."), true);
    eeprom_write_block((const void*)&bedSettings, (void*)0, sizeof(bedSettingsType));
}

//Only writes settings if something has changed...avoid write cyvles...
void updateSettings()
// be aware of eeprom longevity : only 10000 write cycles..
{
    //debug_message(F("Updating settings in flash memory..."), true);
    eeprom_update_block((const void*)&bedSettings, (void*)0, sizeof(bedSettingsType));
}

void readSettings()
{
    int val = digitalRead(RESET_FLASH_MEMORY_PIN);
    if (val == LOW) {
        //debug_message(F("Resetting settings in flash memory..."), true);
        writeSettings();
    }
    
    //debug_message(F("Reading settings in flash memory..."), true);
    eeprom_read_block((void*)&bedSettings, (void*)0, sizeof(bedSettingsType));
}

/*
* Gets X coordinate depending on message to display so it is centered on screen
*/
int getCenterX(int msgLength, int charWidthPixelSize) {
    return (tft.width() / 2) - ((msgLength * charWidthPixelSize) / 2);
}

/*
*  Gets Y coordinate depending on message to display so it is centered on screen
*/
int getCenterY(int totalLineToDisplay, int lineNumber, int charHeightPixelSize) {
    return (tft.height() / 2 - ((charHeightPixelSize * totalLineToDisplay) / 2)) + ((lineNumber - 1) *  charHeightPixelSize);
}

/*
* Display splash screen
*/
void displaySplashScreen() {

    int totalLineToDisplay = 5;     

    String msg = "K40";
    tft.setTextSize(2);  
    int x = getCenterX(msg.length(), 12);
    int y = 10;
    tft.setCursor(x, y);
    tft.setTextColor(RED);            
    tft.print(msg);

    //Text Size 1 - 6x8
    //Text Size 2 - 12x16
    tft.setTextSize(1);         
    int charWidthPixelSize = 6;
    int charHeightPixelSize = 8;

    //#1 line
    msg = "Motorized Bed System";
    int lineNumber = 1;
    x = getCenterX(msg.length(), charWidthPixelSize);
    y = getCenterY(totalLineToDisplay, lineNumber, charHeightPixelSize);
    tft.setCursor(x, y -10 );
    tft.setTextColor(BLUE);            
    tft.print(msg);

    //#2 line
    msg = "By";
    lineNumber = 2;
    x = getCenterX(msg.length(), charWidthPixelSize);
    y = getCenterY(totalLineToDisplay, lineNumber, charHeightPixelSize);
    tft.setCursor(x, y);
    tft.setTextColor(BLUE);            
    tft.print(msg);

    //#3 line
    msg = "Frederic Guiet";
    lineNumber = 3;
    x = getCenterX(msg.length(), charWidthPixelSize);
    y = getCenterY(totalLineToDisplay, lineNumber, charHeightPixelSize);
    tft.setCursor(x, y + 10);
    tft.setTextColor(RED);            
    tft.print(msg);

    //#4 line
    msg = "Firmware : ";
    lineNumber = 4;
    x = getCenterX(msg.length(), charWidthPixelSize);
    y = getCenterY(totalLineToDisplay, lineNumber, charHeightPixelSize);
    tft.setCursor(x, y + 20);
    tft.setTextColor(CYAN);            
    tft.print(msg);

    //#5 line
    msg = FIRMWARE;
    lineNumber = 5;
    x = getCenterX(msg.length(), charWidthPixelSize);
    y = getCenterY(totalLineToDisplay, lineNumber, charHeightPixelSize);
    tft.setCursor(x, y + 30);
    tft.setTextColor(GREEN);            
    tft.print(msg);

    /*debug_message("Screen Height : " + String(tft.height()), true);
    debug_message("Screen Width : " + String(tft.width()), true);
    debug_message("X : " + String(x), true);
    debug_message("Y : " + String(y), true);*/
}

void displayDefaultOffsetScreen() {

    int totalLineToDisplay = 2;     

    //Text Size 1 - 6x8
    //Text Size 2 - 12x16
    tft.setTextSize(1);         
    int charWidthPixelSize = 6;
    int charHeightPixelSize = 8;

    //#1 line
    String msg = "Going to";
    int lineNumber = 1;
    int x = getCenterX(msg.length(), charWidthPixelSize);
    int y = getCenterY(totalLineToDisplay, lineNumber, charHeightPixelSize);
    tft.setCursor(x, y);
    tft.setTextColor(RED);            
    tft.print(msg);

    //#2 line
    msg = "default offset...";
    lineNumber = 2;
    x = getCenterX(msg.length(), charWidthPixelSize);
    y = getCenterY(totalLineToDisplay, lineNumber, charHeightPixelSize);
    tft.setCursor(x, y);
    tft.setTextColor(RED);            
    tft.print(msg);
}

void displaySavingSettingsScreen() {

    int totalLineToDisplay = 2;     

    //Text Size 1 - 6x8
    //Text Size 2 - 12x16
    tft.setTextSize(1);         
    int charWidthPixelSize = 6;
    int charHeightPixelSize = 8;

    //#1 line
    String msg = "Saving";
    int lineNumber = 1;
    int x = getCenterX(msg.length(), charWidthPixelSize);
    int y = getCenterY(totalLineToDisplay, lineNumber, charHeightPixelSize);
    tft.setCursor(x, y);
    tft.setTextColor(RED);            
    tft.print(msg);

    //#2 line
    msg = "settings...";
    lineNumber = 2;
    x = getCenterX(msg.length(), charWidthPixelSize);
    y = getCenterY(totalLineToDisplay, lineNumber, charHeightPixelSize);
    tft.setCursor(x, y);
    tft.setTextColor(RED);            
    tft.print(msg);
}

void displayOriginScreen() {

    int totalLineToDisplay = 2;     

    //Text Size 1 - 6x8
    //Text Size 2 - 12x16
    tft.setTextSize(1);         
    int charWidthPixelSize = 6;
    int charHeightPixelSize = 8;

    //#1 line
    String msg = "Retrieving";
    int lineNumber = 1;
    int x = getCenterX(msg.length(), charWidthPixelSize);
    int y = getCenterY(totalLineToDisplay, lineNumber, charHeightPixelSize);
    tft.setCursor(x, y);
    tft.setTextColor(RED);            
    tft.print(msg);

    //#2 line
    msg = "bed origin...";
    lineNumber = 2;
    x = getCenterX(msg.length(), charWidthPixelSize);
    y = getCenterY(totalLineToDisplay, lineNumber, charHeightPixelSize);
    tft.setCursor(x, y);
    tft.setTextColor(RED);            
    tft.print(msg);
}

void displayMainMenuScreen() {

    //Text Size 1 - 6x8
    //Text Size 2 - 12x16
    tft.setTextSize(2);  
    tft.setCursor(0,0);
    tft.setTextColor(BLUE);     
    tft.print("MAIN MENU");

    tft.setTextSize(1);  
    tft.setCursor(0,21); //16 + 5

    if (currentOptionSelected == SETTINGS_OPTION) {
        tft.setTextColor(BLACK, WHITE);    
    }    
    else {
        tft.setTextColor(WHITE, BLACK);    
    }

    tft.print("Settings     ->");

    //tft.setTextSize(1);  
    tft.setCursor(0,32); //21 + 8 + 3
    
    if (currentOptionSelected == CONTROL_OPTION) {
        tft.setTextColor(BLACK, WHITE);    
    }    
    else {
        tft.setTextColor(WHITE, BLACK);    
    }

    tft.print("Control      ->");

    //Current settings
    //tft.setTextSize(1);  
    tft.setCursor(0,53); 
    tft.setTextColor(BLUE);            
    tft.print("Current settings : ");    

    //tft.setTextSize(1);  
    tft.setCursor(0,64); 
    tft.setTextColor(GREEN);            
    tft.print("Mat. thickness : ");
    tft.setTextColor(RED);    
    tft.print(String(bedSettings.materialThickness_mm));
    tft.setTextColor(GREEN);    
    tft.print(" mm");

    //tft.setTextSize(1);  
    tft.setCursor(0,77);  //64 + 8 + 3
    tft.setTextColor(GREEN);            
    tft.print("Bed ori from focal pt : ");
    tft.setTextColor(RED);    
    tft.print(String(bedSettings.bedOffsetFromOriginToFocalPoint_mm));
    tft.setTextColor(GREEN);    
    tft.print(" mm");

    //tft.setTextSize(1);  
    tft.setCursor(0,96);  //77 + 8 + 8 + 3
    tft.setTextColor(GREEN);            
    tft.print("Bed level : ");
    tft.setTextColor(RED);    

    switch (bedComputedSettings.bed_level_status)
    {        
        case FROM_ORIGIN_TO_BED_FOCAL_POINT_LEVEL:
        tft.print("bed focal");
        break;

        case FROM_ORIGIN_TO_MATERIAL_ENGRAVE_FOCAL_POINT_LEVEL:
        tft.print("mat. focal (engrave)");
        break;

        case FROM_ORIGIN_TO_MATERIAL_CUT_FOCAL_POINT_LEVEL:
        tft.print("mat. focal (cut)");
        break;
    }
}

void displayControlScreen() {

    //Text Size 1 - 6x8
    //Text Size 2 - 12x16
    tft.setTextSize(1);  
    tft.setCursor(0,0);
    tft.setTextColor(BLUE);     
    tft.print("CONTROL MENU");

    //tft.setTextSize(1);  
    tft.setCursor(0,13); //8 + 5

    if (currentOptionSelected == MOVE_FROM_ORIGIN_TO_BED_FOCAL_OPTION) {
        tft.setTextColor(BLACK, WHITE);    
    }    
    else {
        tft.setTextColor(WHITE, BLACK);    
    }

    tft.print("Move from origin to bed focal"); 

    //tft.setTextSize(1);  
    tft.setCursor(0,34); //13 + 8 + 8 + 5

    if (currentOptionSelected == MOVE_FROM_ORIGIN_TO_MATERIAL_FOCAL_ENGRAVE_OPTION) {
        tft.setTextColor(BLACK, WHITE);    
    }    
    else {
        tft.setTextColor(WHITE, BLACK);    
    }    
    tft.print("Move from origin to material focal (engrave)") ; 

    //tft.setTextSize(1);  
    tft.setCursor(0,63); //34 + 8 + 8 + 8 + 5

    if (currentOptionSelected == MOVE_FROM_ORIGIN_TO_MATERIAL_FOCAL_CUT_OPTION) {
        tft.setTextColor(BLACK, WHITE);    
    }    
    else {
        tft.setTextColor(WHITE, BLACK);    
    }
    
    tft.print("Move from origin to material focal (cut)") ; 

    //tft.setTextSize(1);  
    tft.setCursor(0,84); //63 + 8 + 8 +5 

    if (currentOptionSelected == MOVE_TO_BED_ORIGIN) {
        tft.setTextColor(BLACK, WHITE);    
    }    
    else {
        tft.setTextColor(WHITE, BLACK);    
    }
    
    tft.print("Move to bed origin") ;

    //tft.setTextSize(1);  
    tft.setCursor(0,105); //84 + 8 + 8 + 5 

    if (currentOptionSelected == BACK_OPTION) {
        tft.setTextColor(BLACK, WHITE);    
    }    
    else {
        tft.setTextColor(WHITE, BLACK);    
    }
    
    tft.print("Back <-") ;

    

}

void displaySettingsScreen() {

    //Text Size 1 - 6x8
    //Text Size 2 - 12x16
    tft.setTextSize(1);  
    tft.setCursor(0,0);
    tft.setTextColor(BLUE);     
    tft.print("SETTINGS MENU");

    tft.setTextSize(1);  
    tft.setCursor(0,13); //8 + 5

    if (currentOptionSelected == BED_OFFSET_FROM_ORIGIN_TO_FOCAL_OPTION) {
        tft.setTextColor(BLACK, WHITE);    
    }    
    else {
        tft.setTextColor(WHITE, BLACK);    
    }

    tft.print("Bed offset from origin to focal ("+String(bedSettings.bedOffsetFromOriginToFocalPoint_mm)+" mm) ->"); 

    tft.setTextSize(1);  
    tft.setCursor(0,34); //13 + 8 + 8 + 5

    if (currentOptionSelected == THICKNESS_SETTINGS_OPTION) {
        tft.setTextColor(BLACK, WHITE);    
    }    
    else {
        tft.setTextColor(WHITE, BLACK);    
    }    
    tft.print("Material thickness ("+String(bedSettings.materialThickness_mm)+" mm) ->") ; 

    tft.setTextSize(1);  
    tft.setCursor(0,55); //34 + 8 + 8 + 5

    if (currentOptionSelected == SAVE_SETTINGS_OPTION) {
        tft.setTextColor(BLACK, WHITE);    
    }    
    else {
        tft.setTextColor(WHITE, BLACK);    
    }
    
    tft.print("Save settings") ; 

    tft.setTextSize(1);  
    tft.setCursor(0,68); //55 + 8 +5 

    if (currentOptionSelected == BACK_OPTION) {
        tft.setTextColor(BLACK, WHITE);    
    }    
    else {
        tft.setTextColor(WHITE, BLACK);    
    }
    
    tft.print("Back <-") ; 
}

void displayBedOffsetFromOriginToFocalScreen() {

    tft.setTextSize(1);  
    tft.setCursor(0,13); //8 + 5

    tft.setTextColor(WHITE, BLACK);    

    tft.print("Bed offset from origin to focal : " + String(bedSettings.bedOffsetFromOriginToFocalPoint_mm) + "   "); //Space required otherwise 10 will become 90 when decreasing for instance 
}

void displayMaterialThicknessScreen() {

    tft.setTextSize(1);  
    tft.setCursor(0,13); //8 + 5

    tft.setTextColor(WHITE, BLACK);    

    tft.print("Materiel thickness : " + String(bedSettings.materialThickness_mm) + "   "); //Space required otherwise 10 will become 90 when decreasing for instance 
}

/*
* Display screen
*/
void refreshDisplay() {    

    #ifdef DISPLAY

    if (oldScreen != currentScreen)
        //Clear screen !
        tft.clearScreen();

    switch(currentScreen) {
        case SPLASH_SCREEN:
        displaySplashScreen();        
        break;

        case ORIGIN_SCREEN:
        displayOriginScreen();
        break;

        case DEFAULT_OFFSET_SCREEN:
        displayDefaultOffsetScreen();
        break;

        case MAIN_MENU_SCREEN:
        displayMainMenuScreen();
        break;

        case SETTINGS_SCREEN:
        displaySettingsScreen();
        break;

        case BED_OFFSET_FROM_ORIGIN_TO_FOCAL_SCREEN:
        displayBedOffsetFromOriginToFocalScreen();
        break;

        case SAVING_SETTINGS_SCREEN:
        displaySavingSettingsScreen();
        break;

        case MATERIAL_THICKNESS_SCREEN:
        displayMaterialThicknessScreen();
        break;

        case CONTROL_SCREEN:
        displayControlScreen();
        break;
    }

    oldScreen = currentScreen;

    #endif
}

void moveToComputedBedPosition(long absoluteBedPosition) {

    bed.enableOutputs();    
    bed.setMaxSpeed(3500);
    bed.setSpeed(3500);
    bed.setAcceleration(10000);    
    
    // Move to new coordinates
    bed.moveTo((long)absoluteBedPosition);
    bed.runToPosition(); //use acceleration

    bed.disableOutputs();    

    //debug_message("Current Bed position : " + String(bed.currentPosition()), true);    
}

void moveCurrentPositionToBedFocal() {

    //debug_message("Current Bed position : " + String(bed.currentPosition()), true);
    //debug_message("materialOffsetFromOriginToBedFocalPoint_steps : " + String(bedComputedSettings.materialOffsetFromOriginToBedFocalPoint_steps), true);

    absoluteBedPosition = bedComputedSettings.materialOffsetFromOriginToBedFocalPoint_steps;
    /*if (bed.currentPosition() < bedComputedSettings.materialOffsetFromOriginToBedFocalPoint_steps) {
        absoluteBedPosition = bedComputedSettings.materialOffsetFromOriginToBedFocalPoint_steps;
    }

    if (bed.currentPosition() > bedComputedSettings.materialOffsetFromOriginToBedFocalPoint_steps) {
        absoluteBedPosition = - bedComputedSettings.materialOffsetFromOriginToBedFocalPoint_steps;
    }*/
    
    //debug_message("Steps to do : " + String(absoluteBedPosition), true);
    
    moveToComputedBedPosition(absoluteBedPosition);
}

void moveCurrentPositionToMaterialEngraveFocal() {

    //debug_message("Current Bed position : " + String(bed.currentPosition()), true);
    //debug_message("materialOffsetFromOriginToEngraveFocalPoint_steps : " + String(bedComputedSettings.materialOffsetFromOriginToEngraveFocalPoint_steps), true);

    absoluteBedPosition = bedComputedSettings.materialOffsetFromOriginToEngraveFocalPoint_steps;        

/*    if (bed.currentPosition() < bedComputedSettings.materialOffsetFromOriginToEngraveFocalPoint_steps) {
        absoluteBedPosition = bedComputedSettings.materialOffsetFromOriginToEngraveFocalPoint_steps;        
    }

    if (bed.currentPosition() > bedComputedSettings.materialOffsetFromOriginToEngraveFocalPoint_steps) {
        absoluteBedPosition = - bedComputedSettings.materialOffsetFromOriginToEngraveFocalPoint_steps;
    }*/

    //debug_message("Steps to do : " + String(absoluteBedPosition), true);
    
    moveToComputedBedPosition(absoluteBedPosition);

    //debug_message("Current position : " + String(bed.currentPosition()), true);
}

void moveCurrentPositionToMaterialCutFocal() {

    /*if (bed.currentPosition() < bedComputedSettings.materialOffsetFromOriginToCutFocalPoint_steps) {
        stepsToDo = bedComputedSettings.materialOffsetFromOriginToCutFocalPoint_steps - bed.currentPosition();        
    }

    if (bed.currentPosition() > bedComputedSettings.materialOffsetFromOriginToCutFocalPoint_steps) {
        stepsToDo = - (bed.currentPosition() - bedComputedSettings.materialOffsetFromOriginToCutFocalPoint_steps);
    }*/

    absoluteBedPosition = bedComputedSettings.materialOffsetFromOriginToCutFocalPoint_steps;
    
    moveToComputedBedPosition(absoluteBedPosition);
}

void goToOrigin() {

    // keep slowly
    bed.setMaxSpeed(2000);
    bed.setSpeed(2000);
    bed.setAcceleration(10000);

    bed.enableOutputs();

    // Loop going to zero (each step is 0.01mm)
    while (digitalRead(HOME_SWITCH_PIN)) {

        //debug_message("HOME_SWITCH_PIN : " +  String(digitalRead(HOME_SWITCH_PIN)),true);

        bed.moveTo(absoluteBedPosition);
        bed.runSpeedToPosition();  //https://www.airspayce.com/mikem/arduino/AccelStepper/classAccelStepper.html#a9270d20336e76ac1fd5bcd5b9c34f301
        
        absoluteBedPosition -= STEPS_PER_MM / 10;  // (precision: 0.1mm) - Go down 0.1 mm per 0.1 mm
    }

    //debug_message(F("Home position found !"), true);

    // homed ! 
    bed.setCurrentPosition(0);                

    // Shutdown motor
    bed.disableOutputs();
}

void initMenuSettings() {
    currentOptionSelected = SETTINGS_OPTION;
}

void resetEncoder() {
    // reset encoder position
    encoder.write(0);    
}

void computeBedSettings() {

    //debug_message(F("computeBedSettings - BEGIN"), true);

    //debug_message("bedSettings.bedOffsetFromOriginToFocalPoint_mm : ", false);
    //debug_message(String(bedSettings.bedOffsetFromOriginToFocalPoint_mm),true);

    bedComputedSettings.materialOffsetFromOriginToEngraveFocalPoint_mm = bedSettings.bedOffsetFromOriginToFocalPoint_mm - bedSettings.materialThickness_mm;

    //debug_message("bedSettings.materialOffsetFromOriginToEngraveFocalPoint_mm : ", false);
    //debug_message(String(bedComputedSettings.materialOffsetFromOriginToEngraveFocalPoint_mm),true);

    bedComputedSettings.materialOffsetFromOriginToBedFocalPoint_steps = bedSettings.bedOffsetFromOriginToFocalPoint_mm * STEPS_PER_MM;

    //debug_message("bedComputedSettings.materialOffsetFromOriginToBedFocalPoint_steps : ", false);
    //debug_message(String(bedComputedSettings.materialOffsetFromOriginToBedFocalPoint_steps),true);

    bedComputedSettings.materialOffsetFromOriginToEngraveFocalPoint_steps = bedComputedSettings.materialOffsetFromOriginToEngraveFocalPoint_mm * STEPS_PER_MM;

    //debug_message(F("bedComputedSettings.materialOffsetFromOriginToEngraveFocalPoint_steps : "), false);
    //debug_message(String(bedComputedSettings.materialOffsetFromOriginToEngraveFocalPoint_steps),true);

    //debug_message(F("bedSettings.materialThickness_mm : "), false);
    //debug_message(String(bedSettings.materialThickness_mm ),true);

    bedComputedSettings.materialOffsetFromOriginToCutFocalPoint_mm = bedSettings.bedOffsetFromOriginToFocalPoint_mm - bedSettings.materialThickness_mm / 2.0;    

    //debug_message(F("bedComputedSettings.materialOffsetFromOriginToCutFocalPoint_mm : "), false);
    //debug_message(String(bedComputedSettings.materialOffsetFromOriginToCutFocalPoint_mm),true);

    bedComputedSettings.materialOffsetFromOriginToCutFocalPoint_steps = (float)bedComputedSettings.materialOffsetFromOriginToCutFocalPoint_mm * STEPS_PER_MM;

    //debug_message("bedComputedSettings.materialOffsetFromOriginToCutFocalPoint_steps : ", false);
    //debug_message(String(bedComputedSettings.materialOffsetFromOriginToCutFocalPoint_steps),true);
    
    //debug_message(F("computeBedSettings - END"), true);
}

void moveBedToCurrentStatus() {

    //In case user has changed something...
    computeBedSettings();  

    switch (bedComputedSettings.bed_level_status)
    {
        case FROM_ORIGIN_TO_BED_FOCAL_POINT_LEVEL:
        moveCurrentPositionToBedFocal();
        break;

        case FROM_ORIGIN_TO_MATERIAL_ENGRAVE_FOCAL_POINT_LEVEL:
        moveCurrentPositionToMaterialEngraveFocal();
        break;

        case FROM_ORIGIN_TO_MATERIAL_CUT_FOCAL_POINT_LEVEL:
        moveCurrentPositionToMaterialCutFocal();
        break;
    }

}

void setup() {

    if (DEBUG)
        //Setup Serial
        Serial.begin(9600);

    //Init Home switch pin    
    pinMode(HOME_SWITCH_PIN, INPUT_PULLUP);

    //Reset flash memory pin
    pinMode(RESET_FLASH_MEMORY_PIN, INPUT_PULLUP);

    readSettings(); // read settings inflash  

    //computeBedSettings();  

    //Init menu setting
    initMenuSettings();

    //Init encoder button
    encoderButton.setDebounceTime(50); // set debounce time to 50 milliseconds

    //Initialize bed
    bed.setEnablePin(ENABLE_PIN);             // Set enable pin
    bed.setPinsInverted(false,false,false); 
    bed.disableOutputs();

    #ifdef DISPLAY
    //Initialize display
    tft.begin();
    #endif

    //Init current screen
    currentScreen = SPLASH_SCREEN;
    //Show Splash Screen
    refreshDisplay();    
    //for 2 sec
    //TODO : Uncomment
    delay(1000);    

    currentScreen = ORIGIN_SCREEN;
    refreshDisplay();

    //First thing first !    
    //TODO : Uncomment
    goToOrigin();

    currentScreen = DEFAULT_OFFSET_SCREEN;
    refreshDisplay();

    //Moving bed to default level
    moveBedToCurrentStatus();    

    //Show main menu
    currentScreen = MAIN_MENU_SCREEN;    
    refreshDisplay();

    //Reset encoder
    resetEncoder();
}

void handleSelectionOptionManagement() {
    
    newTicks =  encoder.read();
    newTicks =  newTicks / 2; //when user turns a little bit counter is updated by 2

    //1 means user turn button right
    //-1 means user turn button left
    if (abs(newTicks) == 1) {

        //TODO : Remove that
        //bed.enableOutputs();
        
        /*if (newTicks == 1)
            bedPosition = bed.currentPosition() + (0.1 * STEPS_PER_MM);
        else
            bedPosition = bed.currentPosition() + (-0.1 * STEPS_PER_MM);
        
        // Move to new coordinates
        bed.moveTo((long)bedPosition);*/

        //Here we know that user has turned the rotary button!
        //Depending on the screen where we are some action must be taken
        switch (currentScreen) { 
            case CONTROL_SCREEN:
                switch (currentOptionSelected)            
                {            
                    case MOVE_FROM_ORIGIN_TO_BED_FOCAL_OPTION:
                    if (newTicks == 1) {
                        currentOptionSelected = MOVE_FROM_ORIGIN_TO_MATERIAL_FOCAL_ENGRAVE_OPTION;
                        refreshDisplay();                    
                    }
                    break;

                    case MOVE_FROM_ORIGIN_TO_MATERIAL_FOCAL_ENGRAVE_OPTION:
                    if (newTicks == 1) {
                        currentOptionSelected = MOVE_FROM_ORIGIN_TO_MATERIAL_FOCAL_CUT_OPTION;
                        refreshDisplay();                    
                    }
                    if (newTicks == -1) {
                        currentOptionSelected = MOVE_FROM_ORIGIN_TO_BED_FOCAL_OPTION;
                        refreshDisplay();                    
                    }
                    break;
                    
                    case MOVE_FROM_ORIGIN_TO_MATERIAL_FOCAL_CUT_OPTION:
                    if (newTicks == 1) {
                        currentOptionSelected = MOVE_TO_BED_ORIGIN;
                        refreshDisplay();                    
                    }
                    if (newTicks == -1) {
                        currentOptionSelected = MOVE_FROM_ORIGIN_TO_MATERIAL_FOCAL_ENGRAVE_OPTION;
                        refreshDisplay();                    
                    }
                    break;

                    case MOVE_TO_BED_ORIGIN:
                    if (newTicks == 1) {
                        currentOptionSelected = BACK_OPTION;
                        refreshDisplay();                    
                    }
                    if (newTicks == -1) {
                        currentOptionSelected = MOVE_FROM_ORIGIN_TO_MATERIAL_FOCAL_CUT_OPTION;
                        refreshDisplay();                    
                    }
                    break;

                    case BACK_OPTION:
                    if (newTicks == -1) {
                        currentOptionSelected = MOVE_TO_BED_ORIGIN;
                        refreshDisplay();                    
                    }
                    break;                                
                }
                break;

            case SETTINGS_SCREEN:
                switch (currentOptionSelected)            
                {            
                    case BED_OFFSET_FROM_ORIGIN_TO_FOCAL_OPTION:
                    if (newTicks == 1) {
                        currentOptionSelected = THICKNESS_SETTINGS_OPTION;
                        refreshDisplay();                    
                    }
                    break;

                    case THICKNESS_SETTINGS_OPTION:
                    if (newTicks == 1) {
                        currentOptionSelected = SAVE_SETTINGS_OPTION;
                        refreshDisplay();                    
                    }
                    if (newTicks == -1) {
                        currentOptionSelected = BED_OFFSET_FROM_ORIGIN_TO_FOCAL_OPTION;
                        refreshDisplay();                    
                    }
                    break;

                    case SAVE_SETTINGS_OPTION:
                    if (newTicks == 1) {
                        currentOptionSelected = BACK_OPTION;
                        refreshDisplay();                    
                    }
                    if (newTicks == -1) {
                        currentOptionSelected = THICKNESS_SETTINGS_OPTION;
                        refreshDisplay();                    
                    }
                    break;

                    case BACK_OPTION:
                    if (newTicks == -1) {
                        currentOptionSelected = SAVE_SETTINGS_OPTION;
                        refreshDisplay();                    
                    }
                    break;                                
                }
                break;
            
            case MATERIAL_THICKNESS_SCREEN:
                switch (currentOptionSelected)            
                {
                    case MATERIAL_THICKNESS_SET_OPTION:
                    if (newTicks == 1) {
                        //debug_message(F("Increasing materialThickness_mm to 1 mm"), true);
                        if (bedSettings.materialThickness_mm < MAX_BED_OFFSET_FROM_ORIGIN) 
                            bedSettings.materialThickness_mm ++; //add one millimeter
                    }

                    if (newTicks == -1) {
                        //debug_message(F("Decreasing materialThickness_mm to 1 mm"), true);
                        if (bedSettings.materialThickness_mm > 0) 
                            bedSettings.materialThickness_mm --; //add one millimeter
                    }
                    refreshDisplay();
                    break;
                }
                break;

            case BED_OFFSET_FROM_ORIGIN_TO_FOCAL_SCREEN:                
                switch (currentOptionSelected)            
                {
                    case BED_OFFSET_FROM_ORIGIN_TO_FOCAL_SET_OPTION:
                    if (newTicks == 1) {
                        //debug_message(F("Increasing bedOffsetFromOriginToFocalPoint_mm to 1 mm"), true);
                        if (bedSettings.bedOffsetFromOriginToFocalPoint_mm < MAX_MATERIAL_THICKNESS) 
                            bedSettings.bedOffsetFromOriginToFocalPoint_mm ++; //add one millimeter
                    }

                    if (newTicks == -1) {
                        //debug_message(F("Decreasing bedOffsetFromOriginToFocalPoint_mm to 1 mm"), true);
                        if (bedSettings.bedOffsetFromOriginToFocalPoint_mm > 0) 
                            bedSettings.bedOffsetFromOriginToFocalPoint_mm --; //add one millimeter
                    }
                    refreshDisplay();
                    break;
                }
                break;
            case MAIN_MENU_SCREEN:
                switch (currentOptionSelected)            
                {            
                    case SETTINGS_OPTION:
                    if (newTicks == 1) {
                        currentOptionSelected = CONTROL_OPTION;
                        refreshDisplay();                    
                    }
                    break;

                    case CONTROL_OPTION:
                    if (newTicks == -1) {
                        currentOptionSelected = SETTINGS_OPTION;
                        refreshDisplay();                    
                    }
                    break;                                
                }
                break;
        }

        //debug_message("Encoder newTickes values : " + String(newTicks), true);        
        resetEncoder();
    }
}

void handleMotorManagement() {

    if (!bed.isRunning())  // Cut motor power is position reached.
        bed.disableOutputs();

    bed.run();     // MUST also be called for movements management...
}

void handleEncoderButtonManagement() {

    if (encoderButton.isPressed()) {
        pressedTime = millis();  
        isPressing = true;      
    }

    if(encoderButton.isReleased()) {
        isPressing = false;
        buttonPressedHandled = false;
    }

    //Button pressed more than one sec and not handled yet?
    if ((millis() - pressedTime >=  LONG_PRESS_TIME) && isPressing && !buttonPressedHandled) {

        //debug_message(F("Button pressed !"), true);
        buttonPressedHandled = true;
        
        switch (currentOptionSelected)
        {       
            case MOVE_FROM_ORIGIN_TO_BED_FOCAL_OPTION:
                //buttonPressedHandled = true;
                bedComputedSettings.bed_level_status = FROM_ORIGIN_TO_BED_FOCAL_POINT_LEVEL;
                moveBedToCurrentStatus();
            break;

            case MOVE_FROM_ORIGIN_TO_MATERIAL_FOCAL_ENGRAVE_OPTION:
                //buttonPressedHandled = true;
                bedComputedSettings.bed_level_status = FROM_ORIGIN_TO_MATERIAL_ENGRAVE_FOCAL_POINT_LEVEL;
                moveBedToCurrentStatus();
            break;

            case MOVE_FROM_ORIGIN_TO_MATERIAL_FOCAL_CUT_OPTION:
                //buttonPressedHandled = true;
                bedComputedSettings.bed_level_status = FROM_ORIGIN_TO_MATERIAL_CUT_FOCAL_POINT_LEVEL;
                moveBedToCurrentStatus();
            break;            

            case SAVE_SETTINGS_OPTION:
                //buttonPressedHandled = true;
                currentScreen = SAVING_SETTINGS_SCREEN;                    
                refreshDisplay();
                updateSettings();
                delay(1000);
                currentScreen = SETTINGS_SCREEN;    
                refreshDisplay();                
            break;

            //User wants to change material thickness ?
            case THICKNESS_SETTINGS_OPTION:
                currentMaterialThickness = bedSettings.materialThickness_mm;
                //buttonPressedHandled = true;
                currentScreen = MATERIAL_THICKNESS_SCREEN;    
                currentOptionSelected = MATERIAL_THICKNESS_SET_OPTION;
                refreshDisplay();
            break;

            case MATERIAL_THICKNESS_SET_OPTION:
                //User changed material thickness ?
                if (currentMaterialThickness != bedSettings.materialThickness_mm) {
                    moveBedToCurrentStatus();
                }

                //buttonPressedHandled = true;                
                currentScreen = SETTINGS_SCREEN;    
                currentOptionSelected = THICKNESS_SETTINGS_OPTION;
                refreshDisplay();
            break;

            case SETTINGS_OPTION:
                //buttonPressedHandled = true;
                currentScreen = SETTINGS_SCREEN;    
                currentOptionSelected = BED_OFFSET_FROM_ORIGIN_TO_FOCAL_OPTION;
                refreshDisplay();
            break;

            case CONTROL_OPTION:
                //buttonPressedHandled = true;
                currentScreen = CONTROL_SCREEN;    
                currentOptionSelected = MOVE_FROM_ORIGIN_TO_BED_FOCAL_OPTION;
                refreshDisplay();
            break;

            case BED_OFFSET_FROM_ORIGIN_TO_FOCAL_OPTION:
                currentFromOriginToBedFocalPoint = bedSettings.bedOffsetFromOriginToFocalPoint_mm;
                //buttonPressedHandled = true;
                currentScreen = BED_OFFSET_FROM_ORIGIN_TO_FOCAL_SCREEN;    
                currentOptionSelected = BED_OFFSET_FROM_ORIGIN_TO_FOCAL_SET_OPTION;
                refreshDisplay();
            break;

            case BED_OFFSET_FROM_ORIGIN_TO_FOCAL_SET_OPTION:
                //User changed something ?
                if (currentFromOriginToBedFocalPoint != bedSettings.bedOffsetFromOriginToFocalPoint_mm) {
                    moveBedToCurrentStatus();
                }

                //buttonPressedHandled = true;
                currentScreen = SETTINGS_SCREEN;
                currentOptionSelected = BED_OFFSET_FROM_ORIGIN_TO_FOCAL_OPTION;
                refreshDisplay();
            break;

            case MOVE_TO_BED_ORIGIN:
                currentScreen = ORIGIN_SCREEN;
                refreshDisplay();
                goToOrigin();
                currentScreen = CONTROL_SCREEN;
                refreshDisplay();
            break;

            case BACK_OPTION:
                //debug_message(F("Back selected : "), false);
                switch (currentScreen)
                {
                    case SETTINGS_SCREEN:
                        //debug_message(F("from BED_OFFSET_FROM_ORIGIN_TO_FOCAL_SCREEN"), true);
                        //buttonPressedHandled = true;
                        currentScreen = MAIN_MENU_SCREEN;
                        currentOptionSelected = SETTINGS_OPTION;
                        refreshDisplay();
                        break; 
                    case CONTROL_SCREEN:
                        //buttonPressedHandled = true;
                        currentScreen = MAIN_MENU_SCREEN;
                        currentOptionSelected = CONTROL_OPTION;
                        refreshDisplay();
                        break;
                }                              
            break;
        }        
    }
}

void loop() {
        
    encoderButton.loop(); // MUST call the loop() function first to properly manage button.

    handleEncoderButtonManagement();

    handleSelectionOptionManagement();

    handleMotorManagement();  
}