#include <WiFi.h>                 //WiFi
#include <AsyncTCP.h>             //WebServer
#include <ESPAsyncWebServer.h>    //WebServer
#include <Wire.h>                 //enable I2C.
#include <LiquidCrystal_I2C.h>    //LCD
#include <Button.h>               //Button
#include <FastLED.h>              //NeoPixel LED Lights

//-----------[ DBUG ]---------
const int DBUG = 0;          // Set this to 0 for no serial output for debugging, 1 for moderate debugging, 2 for FULL debugging to see serail output in the Arduino GUI.
//----------------------------

//----------[ GLOBAL VARS ]---------------
const char* ssid = "HOME-55A2";
const char* password =  "3E7D73F4CED37FAC";
bool GV_WEB_REQUEST_IN_PROGRESS = false;
bool GV_READ_REQUEST_IN_PROGRESS = false;
bool GV_THIS_IS_A_SERIAL_COMMAND = false;
bool GV_QUERY_DO_NAME_ON_NEXT_COMMAND = true;   // Loop tests first to see if we need to query the DO for it's name.  Set to true to do this first thing
bool GV_BOOTING_UP = true;
//----------------------------------------

//LED LIGHTS
#define DATA_PIN 15
#define NUM_LEDS 7
CRGB leds[NUM_LEDS];  // Define the array of leds
//----------

//BUTTON
Button button1(2);                                      // Connect your button between pin 2 and GND
int     GV_LCD_MAIN_TEXT_INDEX=0;                          // Text to show in top left of LCD (DO sensor name, IP Address, Program version, FireBeatle version.  Depending on the button.
String  GV_LCD_MAIN_TEXT[4] = {"xxx.xxx.xxx.xxx \0",    //Text to cycle through when button is pressed.
                                "JoeSmoe        \0",
                                "V12.28         \0",
                                "Program V1.00  \0"};
//-----
 
// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);               // set the LCD address to 0x27 for a 16 chars and 2 line
const bool ClearLCD = true, NoClearLCD = false;   // Used for LCD_DISPLAY
const bool PrintSerial = true, NoSerial = false;  // Used for LCD_DISPLAY

//I2C
#define DOxAddress 97                 // default I2C ID number for EZO D.O. Circuit.
String GV_DOX_DATA = "";            // Holds latest reading from D.O. Circuit.
String GV_SENSOR_RESPONSE = "NONE";  // Holds latest response from D.O. Circuit.
String GV_WEB_RESPONSE_TEXT ="";

char computerdata[20];           //we make a 20 byte character array to hold incoming data from a pc/mac/other.
byte received_from_computer = 0; //we need to know how many characters have been received.
char DO_data[20];                //we make a 20 byte character array to hold incoming data from the D.O. circuit.
byte in_char = 0;                //used as a 1 byte buffer to store inbound bytes from the D.O. Circuit.
int time_ = 600;                 //used to change the delay needed depending on the command sent to the EZO Class D.O. Circuit.
String DO;                        //char pointer used in string parsing.
int i = 0;
char R = 'r';
char *ReadDOx = &R;



//---
unsigned long DOxReadingMillis = millis();  // Stores milliseconds since last D.O. reading.
unsigned long HeartBeatMillis = millis();
unsigned long WebServerMillis = millis();
char HeartBeat = ' ';
//----------------------------------------

//---------------[ PRE-SETUP]-------------
AsyncWebServer server(80);              // Setup Web Server Port.
const char* PARAM_MESSAGE = "send";     // HTTP_GET parameter to look for.
//----------------------------------------

//===============================[ SETUP ]============================
void setup() {
  Serial.begin(115200);
  //I2C
  Wire.begin();                //enable I2C port.
  //---

  //LED LIGHTS
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
  FastLED.setBrightness(5);
  //----------
  
  //BUTTON
  button1.begin();
  //------
  
  // LCD
  lcd.begin(); //initialize the lcd

  //-----------------------------[ WEB SERVER ]-----------------------------------------------
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    LCD_DISPLAY("Wifi..", 0, 0, ClearLCD, PrintSerial);
    LCD_DISPLAY(ssid, 0, 1, NoClearLCD, PrintSerial);
  }
  lcd.clear();
  
  IPAddress IP=WiFi.localIP();
  GV_LCD_MAIN_TEXT[0]=String(IP[0]) + '.' + String(IP[1]) + '.' + String(IP[2]) + '.' + String(IP[3]);

  SendCommandAndSetDOxVariables("name,?");          // get the name of the DO sensor to display on the LCD
  
  //-----------------[ read web page]-------------------------------
  server.on("/read", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", GV_WEB_RESPONSE_TEXT);
    });
  //-----------------[ send web page]-------------------------------
  // Send a GET request to <IP>/get?message=<message>
  server.on("/DO", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String message;
    int w=0;
    GV_WEB_REQUEST_IN_PROGRESS = true;               // set this to tell the auto DOx read loop we want to talk to the DOx sensor from the micro PC's web page.  I assume this is an Interupt.
    if (request->hasParam(PARAM_MESSAGE)) {
      message = request->getParam(PARAM_MESSAGE)->value();
    } else {
      message = "No message sent";
    }
    while (GV_READ_REQUEST_IN_PROGRESS){             // wait until the auto DOx read ends so we can have a turn sending this request sent from the micro PC's web page to the DOx sensor
      w++;
      if (w < 2000) break;
      }
      
    SendCommandAndSetDOxVariables(message);

    request->send(200, "text/plain", GV_WEB_RESPONSE_TEXT);

    GV_WEB_REQUEST_IN_PROGRESS = false;
    });

  server.begin();
 
  //----------------------------------------------------------------------------------------
}



//===============================[ LOOP ]==============================
void loop() {

  if (GV_QUERY_DO_NAME_ON_NEXT_COMMAND){              // query DO name if this is set to true, then display it on the LCD screen. 
    SendCommandAndSetDOxVariables("name,?\0");
    Serial.println("DOOD");
    Serial.println(GV_DOX_DATA);
    GV_DOX_DATA.remove(0,6);
    GV_LCD_MAIN_TEXT[1]=GV_DOX_DATA;
    GV_LCD_MAIN_TEXT_INDEX=1;
    LCD_DISPLAY(GV_LCD_MAIN_TEXT[GV_LCD_MAIN_TEXT_INDEX],0,0,NoClearLCD,PrintSerial);
    GV_QUERY_DO_NAME_ON_NEXT_COMMAND = false;
  }
  
  if (Serial.available() > 0) {                                           //if data is holding in the serial buffer
    received_from_computer = Serial.readBytesUntil(13, computerdata, 20); //we read the data sent from the serial monitor(pc/mac/other) until we see a <CR>. We also count how many characters have been received.
    computerdata[received_from_computer] = 0;                             //stop the buffer from transmitting leftovers or garbage.
    computerdata[0] = tolower(computerdata[0]);                           //we make sure the first char in the string is lower case.
    if (computerdata[0] == 'c' || computerdata[0] == 'r')time_ = 600;     //if a command has been sent to calibrate or take a reading we wait 600ms so that the circuit has time to take the reading.
    else time_ = 300;                                                     //if not 300ms will do

    GV_THIS_IS_A_SERIAL_COMMAND=true;                                     //set gloabal indicator that the next command to the DO cercuit is from the Serial Monitor.
    String str_serial_command=computerdata;                               //convert char array to String type for the function that follows
    SendCommandAndSetDOxVariables(str_serial_command);                    //send the command received by the serial monitoring device.
  }
  
  if (((millis() - DOxReadingMillis) > 2000) && !GV_WEB_REQUEST_IN_PROGRESS){
    GV_READ_REQUEST_IN_PROGRESS=true;
    SendCommandAndSetDOxVariables(ReadDOx);
    LCD_DISPLAY(GV_DOX_DATA, 0, 1, NoClearLCD, PrintSerial);
    LCD_DISPLAY(" ", 4, 1, NoClearLCD, NoSerial);
    DOxReadingMillis = millis();
    GV_READ_REQUEST_IN_PROGRESS=false;
  }
  
  if ((millis() - WebServerMillis) > 500000) {
    if(DBUG) Serial.println("WebServer re-begin");
    WebServerMillis = millis();
    server.begin();
  }
  
  LCDshowHeartBeat();

  BUTTON_WasItPressed_ChangeLCD();

  GV_BOOTING_UP=false;
  
}



//===============================[ FUNCTIONS ]=========================
void LED_Center_Blue(bool ON){
  if (ON) {
    leds[0] = CRGB::Blue;
    FastLED.show();    
  }else{
    leds[0] = CRGB::Black;
    leds[1] = CRGB::Black;
    leds[2] = CRGB::Black;
    leds[3] = CRGB::Black;
    leds[4] = CRGB::Black;
    leds[5] = CRGB::Black;
    leds[6] = CRGB::Black;
    FastLED.show();    
  }
  
}

String AddCarrageReturnIfNeeded(String str){
    
    long int len = str.length();
    int i=0;
    bool HasCR = false;
    
    for (i=0; i < len; i++) if (str[i]=='\r') HasCR=true;
    
    if (!HasCR) { str[i++]='\r'; str[i++]='\0'; }
    
    if(DBUG==2){
        for (i=0; i < (str[i] != '\0'); i++){
            Serial.print(str[i]); Serial.print(":"); Serial.print(i); Serial.print(":"); Serial.print(int(str[i]));
        }
    }

    return str;
    
}

void LCD_DISPLAY(String Text, int row, int col, bool xClearLCD, bool xprintSerial) { // 12 Millis
  if (xClearLCD) lcd.clear();
  lcd.setCursor(row, col); // set the cursor to column 15, line 1
  lcd.print(Text);
  if (xprintSerial) if(DBUG) Serial.println("LCD:" + Text);
}

void BUTTON_WasItPressed_ChangeLCD(){
  String LCDTEXT;
  if (button1.pressed() && !GV_BOOTING_UP){
    if(DBUG) Serial.println("Button 1 pressed");
    switch (GV_LCD_MAIN_TEXT_INDEX){
      case 0: GV_LCD_MAIN_TEXT_INDEX=1;
              LCDTEXT=GV_LCD_MAIN_TEXT[GV_LCD_MAIN_TEXT_INDEX];
              break;
      case 1: GV_LCD_MAIN_TEXT_INDEX=2;
              LCDTEXT=GV_LCD_MAIN_TEXT[GV_LCD_MAIN_TEXT_INDEX];
              break;
      case 2: GV_LCD_MAIN_TEXT_INDEX=3;
              LCDTEXT=GV_LCD_MAIN_TEXT[GV_LCD_MAIN_TEXT_INDEX];
              break;
      case 3: GV_LCD_MAIN_TEXT_INDEX=0;
              LCDTEXT=GV_LCD_MAIN_TEXT[GV_LCD_MAIN_TEXT_INDEX];
              break;
    }
    if(DBUG) Serial.println(LCDTEXT);
    LCD_DISPLAY(LCDTEXT, 0, 0, NoClearLCD, PrintSerial);                            
   }  
}

void LCDshowHeartBeat() {
  if ((millis() - HeartBeatMillis) > 1000) {
    if (HeartBeat != ' ') {
      HeartBeat = ' ';
      LED_Center_Blue(false);
    }
    else {
      HeartBeat = '*';
      LED_Center_Blue(true);
    }
    if(DBUG==2) Serial.println(HeartBeat);
    LCD_DISPLAY(&HeartBeat, 15, 0, NoClearLCD, NoSerial);
    HeartBeatMillis = millis();
  }
}

void SendCommandAndSetDOxVariables(String command) {
  char Ccommand[20];
  byte code = 0;                   //used to hold the I2C response code.
  command[0] = tolower(command[0]);
  command=AddCarrageReturnIfNeeded(command);
  command.toCharArray(Ccommand,20);
  Wire.beginTransmission(DOxAddress);                              //call the circuit by its ID number.
  Wire.write(Ccommand);                                            //transmit the command that was sent through the serial port.
  Wire.endTransmission();                                          //end the I2C data transmission.
 
  if (DBUG){ Serial.print("DOc command:("); Serial.print(Ccommand); Serial.print(")"); }

  if (Ccommand[0] == 'c' || Ccommand[0] == 'r' || Ccommand[0] == 'n')time_ = 600;     //if a command has been sent to calibrate or take a reading we wait 600ms so that the circuit has time to take the reading.
  else time_ = 300;                                             //if not 300ms will do
    
  //if (strcmp(computerdata, "sleep") != 0) {  //if the command that has been sent is NOT the sleep command, wait the correct amount of time and request data.
  //if it is the sleep command, we do nothing. Issuing a sleep command and then requesting data will wake the D.O. circuit.

  delay(time_);                     //wait the correct amount of time for the circuit to complete its instruction.

  Wire.requestFrom(DOxAddress, 20, 1); //call the circuit and request 20 bytes (this may be more than we need)
  code = Wire.read();               //the first byte is the response code, we read this separately.

  switch (code) {                   //switch case based on what the response code is.
    case 1:                         //decimal 1.
      if(DBUG==2) Serial.println("Success");    //means the command was successful.
      GV_SENSOR_RESPONSE="Success";
      break;                        //exits the switch case.
    case 2:                         //decimal 2.
      if(DBUG==2) Serial.println("Failed");     //means the command has failed.
      GV_SENSOR_RESPONSE="Failed \0";
      GV_DOX_DATA="";
      break;                        //exits the switch case.
    case 254:                      //decimal 254.
      if(DBUG==2) Serial.println("Pending");   //means the command has not yet been finished calculating.
      GV_SENSOR_RESPONSE="Pending\0";
      GV_DOX_DATA="";
      break;                       //exits the switch case.
    case 255:                      //decimal 255.
      if(DBUG==2) Serial.println("No Data");   //means there is no further data to send.
      GV_SENSOR_RESPONSE="No Data\0";
      GV_DOX_DATA="";
      break;                       //exits the switch case.
  }
   
  while (Wire.available()) {       //are there bytes to receive.
    in_char = Wire.read();         //receive a byte.
    DO_data[i] = in_char;          //load this byte into our array.
    if(DBUG==2) Serial.println(DO_data[i]);
    i += 1;                        //incur the counter for the array element.
    if (in_char == 0) {            //if we see that we have been sent a null command.
      i = 0;                       //reset the counter i to 0.
      Wire.endTransmission();      //end the I2C data transmission.
      break;                       //exit the while loop.
    }
  }

  GV_DOX_DATA = DO_data;         

  GV_WEB_RESPONSE_TEXT=GV_DOX_DATA + "," + GV_SENSOR_RESPONSE;

  if (Ccommand[0] == 'n' && Ccommand[5] != '?'){              // someone sent the name,TheName to the DO cercuit (not just querying the name,?).
    GV_QUERY_DO_NAME_ON_NEXT_COMMAND = true;    
  }
  
  if ( DBUG==2 || (DBUG==1 && GV_THIS_IS_A_SERIAL_COMMAND) ){
      Serial.println("GV_DOX_DATA:" + GV_DOX_DATA);
      Serial.println("GV_SENSOR_RESPONSE:" + GV_SENSOR_RESPONSE);
      Serial.println("GV_WEB_RESPONSE_TEXT" + GV_WEB_RESPONSE_TEXT);
  }
  if (GV_THIS_IS_A_SERIAL_COMMAND) GV_THIS_IS_A_SERIAL_COMMAND=false;                                     //set gloabal indicator that command from the Serial Monitor is done.
}
