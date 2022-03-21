// SmartHab Water Code for Slave Arduino Board

// INCLUDING LIBS
#include <Wire.h>                                                     // Including i2C Lib
#include <OLED_I2C.h>                                                 // Including display libs
#include <OneWire.h>                                                  // Inluding OneWire lib for DS18B20
#include <DallasTemperature.h>                                        // Including Dallas Temperaire lib
#include <Bounce2.h>                                                  // Including Bounce Lib to eliminate bouncing on contacts
#include <stdio.h>                                                    // Standars IO Lib
#include <DS1302.h>                                                   // Including RTC libs

// DEFINING PINS
#define ColdWaterPin 2                                                // Pin for Cold Water counter (HIGH, getting LOW on Hall sensor work)
#define HotWaterPin 3                                                 // Pin for Hot Water counter (HIGH, getting LOW on Hall sensor work)
#define DS18B20Pin 4                                                  // Pin for hot and cold water temperature sensors
#define RTCRSTPin 5                                                   // Pin for RTC Reset Pin
#define RTCCLKPin 6                                                   // Pin for RTC Clock Pin
#define RTCDATPin 7                                                   // Pin for RTC Data Pin

// Settind Global Variables for physical parameters
uint8_t ColdTemp=0;                                                   // Hot Water Temperature (possible 0-100)
uint8_t HotTemp=0;                                                    // Hot Water Temperature (possible 0-100)
union ColdUnion {
  uint32_t ColdVal32;
  uint8_t  ColdVal8[4];
} ColdValue={ColdVal32:0};                                            //Cold Value in Liters (possible 0-99999999)
union HotUnion {
  uint32_t HotVal32;
  uint8_t  HotVal8[4];
}HotValue={HotVal32:0};                                               //Hot Value in Liters (possible 0-99999999)
uint32_t OldColdValue=0;                                              // Cold Value in Liters 12 minutes ago (possible 0-99999999)
uint32_t OldHotValue=0;                                               // Hot Value in Liters 12 minutes ago (possible 0-99999999)
uint8_t ColdHallvalue=0;                                              // Value of Cold Hall sensor
uint8_t HotHallvalue=0;                                               // Value of Cold Hall sensor
uint8_t SimpleTimer;                                                  // Simple timer (0-10 secs) to load-balance internal procidures on time line
uint8_t OldSimpleTimer;                                               // To track Second Changed Event
uint8_t WaterCons[24];                                                // Array for storing consumption in liters for every 12 mins last 24 hours
uint16_t LitersPerFullLine=50;                                        // Maximum value of lines drawn - setting smallest scale of 50
uint16_t total24 = 0;                                                 // Water consumed in 24 hours

// Setting variables for flags
bool SecondChanged=false;                                             // Flag for second changed event
bool TempRead=false;                                                  // Flag for tracking temperature read
bool ScrUpdated=false;                                                // Flag for tracking Screen Update
bool TotalRecalc=false;                                               // Flag to avoid recalc of array more then once

// Setting variables for transmission
uint8_t CorBytesExpected=16;

// SETTING SPECIAL OBJECTS
Bounce ColdDebouncer = Bounce();                                      // Instantiate a Bounce object for Cold Water
Bounce HotDebouncer = Bounce();                                       // Instantiate a Bounce object for Cold Water
OneWire oneWire(DS18B20Pin);                                          // Init of Thermo ds18b20 chain
DallasTemperature sensors(&oneWire);                                  // Initializing thermo library
OLED  myOLED(8, 9);                                                   // Initializing display on SDA/SCL Pins
extern uint8_t SmallFont[];                                           // Choosilg standard small fonts
DS1302 rtc(RTCRSTPin, RTCDATPin, RTCCLKPin);                          // Declaring Time object for RTC module
String dayAsString(const Time::Day day) {
  switch (day) {
    case Time::kSunday: return "Su";
    case Time::kMonday: return "Mo";
    case Time::kTuesday: return "Tu";
    case Time::kWednesday: return "We";
    case Time::kThursday: return "Th";
    case Time::kFriday: return "Fr";
    case Time::kSaturday: return "Sa";
  }
  return "(un)";
}
// SETUP____________________________________________________________________________________________________________________________
void setup() {
  delay(2000);                                                        // Waiting 2 seconds for everytinng to come up
  // SETTING PIN MODES
  pinMode(ColdWaterPin,INPUT);                                        // Setting Pin Mode for Cold Counter, and using internal pullup
  digitalWrite(ColdWaterPin,HIGH);
  pinMode(HotWaterPin,INPUT);                                         // Setting Pin Mode for Hot Counter, and using internal pullup
  digitalWrite(HotWaterPin,HIGH);

  // INITIALIZING EQUIPMENT
  ColdDebouncer.attach(ColdWaterPin);                                 // Attaching debouncer to pin
  ColdDebouncer.interval(20);                                         // interval in ms
  HotDebouncer.attach(HotWaterPin);                                   // Attaching debouncer to pin
  HotDebouncer.interval(20);                                          // interval in ms  
  //I2C communications setup
  Wire.begin(8);                                                      // Join i2c bus with address 8
  Wire.onReceive(receiveEvent);                                       // Register On recieve Event Function
  Wire.onRequest(requestEvent);                                       // Register On Request Event Function
  //Setting display
  myOLED.begin();                                                     // Initializing OLED
  myOLED.setFont(SmallFont);                                          // Choosing font for OLED
  sensors.begin();                                                    // Starting D18B20
  rtc.writeProtect(false);                                            // Un-halting RTC just in case
  rtc.halt(false); 
  rtc.writeProtect(false);
  
  // Trying to get values from RTC chip on reset
  for(int i=0;i<=3;i++){
    ColdValue.ColdVal8[i]=rtc.readRam(i);                             // Read cold value from bytes 0-3
  }
  for(int i=4;i<=7;i++){
    HotValue.HotVal8[i-4]=rtc.readRam(i);                             // Read cold value from bytes4-7
  }
  byte MaxConsume=0;                                                  // Var for detecting maximum
  for(int i=8;i<=30;i++){                                             // Reading last 23-hours values (dropping current hour to 0 due to insufficient 1 byte of memory)
    WaterCons[i-7]=rtc.readRam(i);                                    // Dropping [0], starting from [1]to[23]
    if (WaterCons[i-7]>MaxConsume){
      MaxConsume=WaterCons[i];                                        // Detecting maximum consume value
    }
  }
  if(MaxConsume<50){                                                  // Rescaling graph maximum
    LitersPerFullLine=50;
    }else{LitersPerFullLine=((MaxConsume/50)+1)*50;                   // Max Consume larger then 50, so graph maximum scales from 100 to 250 
  }
 
  OldColdValue=ColdValue.ColdVal32;                                   // Old Cold Value setting to loaded from RAM (to avoid jump on graph
  OldHotValue=HotValue.HotVal32;                                      // Old Hot Value setting to loaded from RAM (to avoid jump on graph
  
  // Trying to update Values from WiFi memory on reset
  
}
//LOOP____________________________________________________________________________________________________________________________________
void loop() {
  // Calculating simple timer and timong events
  SimpleTimer=(millis()/1000)%10;                                     // Calculating seconds from milliseconds
  if (SimpleTimer==0){                                                // Every 10 seconds reset temp flags 
    TempRead=false;                                                   // Resetting Temperature Read Flag
  }
  if (OldSimpleTimer!=SimpleTimer){                                   // Detecting changing every second for ticks and flags
    SecondChanged=true;
    OldSimpleTimer=SimpleTimer;
  }else{
    SecondChanged=false;
  }
  if (SecondChanged==true){                                           // What to renew every new second
    ScrUpdated=false;                                                 // Dropping flag for Screen Update
    TotalRecalc=false;                                                // Dropping flag for 24 - consumption Recalc
  }
  if (int(millis()/1000)%3600==0 and TotalRecalc==false and SecondChanged==true){ // Once every 3600 secs - recalculate comsumption array.
    Recalc24Usage();
  }
  GetWaterValues();                                                   // Reading water senors values - every moment possible
  if (SimpleTimer==1 and TempRead==false){                            // on 1 second of every 10 - attempt to read temperature
    GetTempValues();                                                  // Reading temperature values from ds18b20 chips
  }
  if (ScrUpdated==false){                                             // Refreshing screen only once per second to save resources
    RefreshOLED();
  }  
}

// ROUTINE TO RECIEVE CORRECTIONS FROM WiFi BOARD AND UPDATE LOCAL VALUES TO IT_______________________________________________________________________________________________
void receiveEvent(int howMany) {
  // Variables for values accepted via I2C
  uint8_t CorDoW=1;                                                   // Correct Day of Week (1 bytes)
  uint16_t CorYear;                                                   // Correct Year
  uint8_t CorYearH;                                                   // Correct Year High Byte (1 byte)
  uint8_t CorYearL;                                                   // Correct Year Low Byte (1 byte)
  uint8_t CorMon;                                                     // Correct Month (1 byte)
  uint8_t CorDay;                                                     // Correct Day (1 byte)
  uint8_t CorHr;                                                      // Correct Hour (1 byte)
  uint8_t CorMin;                                                     // Correct Minute (1 byte)
  uint8_t CorSec;                                                     // Correct Second (1 byte)
  union CorColdUnion{                                                 // Union for correct cold value
    uint32_t ColdVal32;                                               // 32-bit variant
    uint8_t ColdVal8[4];                                              // 8-bit variant
  } CorColdValue;                                                     // converter
  union CorHotUnion{                                                  // Union for correct hot value
    uint32_t HotVal32;                                                // 32-bit variant
    uint8_t HotVal8[4];                                               // 8-bit variant
  } CorHotValue;                                                      // converter
  if (CorBytesExpected==Wire.available()){                            // We expect 16 bytes, no more no less
    CorDoW=Wire.read();                                               // Getting First half of Correct DoW
    CorYearH=Wire.read();                                             // Getting Correct Year High Byte
    CorYearL=Wire.read();                                             // Getting Correct Year Low Byte
    CorYear=uint16_t(CorYearH,CorYearL);                                  // Reassembling Correct Year
    CorMon=Wire.read();                                               // Getting Correct Month 
    CorDay=Wire.read();                                               // Correct Correct Day
    CorHr=Wire.read();                                                // Correct Correct Hour
    CorMin=Wire.read();                                               // Correct Correct Minute
    CorSec=Wire.read();                                               // Correct Correct Second
    for(int i=0;i<=3;i++){
      CorColdValue.ColdVal8[i]=Wire.read();                           // Getting correct value for Cold counter
    }
    for(int i=0;i<=3;i++){
      CorHotValue.HotVal8[i]=Wire.read();                             // Getting correct value for Hot counter
    }                                                                 // End recieve - I hope transmission ended by now
    Time t = rtc.time();                                              // Get the current time and date from the chip.
    if (t.day!=CorDoW or t.yr!=CorYear or t.mon!=CorMon or            // Checking if time update is needed
    t.date!=CorDay or t.hr!=CorHr or t.min!=CorMin or t.sec!=CorSec){ // If any of values recieved is not correct
      rtc.writeProtect(false);                                        //Dsbling RTC write protect
      Time t(CorYear,CorMon,CorDay,CorHr,CorMin,CorSec,Time::Day(CorDoW));
      rtc.time(t);                                                    // Writing new time toRTC Chip
      rtc.writeProtect(true);                                         // Turning back write protection
    }                                        
    if(CorColdValue.ColdVal32!=ColdValue.ColdVal32){                  // Checking if Cold Counter Variable needs update
      ColdValue.ColdVal32=CorColdValue.ColdVal32;
      WriteWaterValuesToRAM();                                        // Writing current values to RAM on RTC
    }
    if(CorHotValue.HotVal32!=HotValue.HotVal32){                      // Checking if Cold Counter Variable needs update
      HotValue.HotVal32=CorHotValue.HotVal32;
      WriteWaterValuesToRAM();                                        // Writing current values to RAM on RTC
    }
  }
}


// ROUTINE ANSWER TO DATA REQUEST FROM WIFI AND SEND LOCAL VALUES_________________________________________________________________________________________________________________
void requestEvent(){                                                  // Routine to answer to WiFi chip with updated values 18 Bytes Total
  Time t = rtc.time();                                                // Get the current time and date from the RTC chip.
  Wire.write(t.day);                                                  // Sending Day of Week (1 bytes, 1=Sunday, 0 = Monday etc)
  Wire.write(highByte(t.yr));                                         //Sending year (2 bytes)
  Wire.write(lowByte(t.yr));                                          //Sending year (2 bytes)
  Wire.write(t.mon);                                                  // Sending months (1 byte)
  Wire.write(t.date);                                                 // Sending days (1 byte)
  Wire.write(t.hr);                                                   // Sending hours (1 byte)
  Wire.write(t.min);                                                  // Sending minutes (1 byte)
  Wire.write(t.sec);                                                  // Sending seconds (1 byte)
  Wire.write(ColdTemp);                                               // Sending cold Water Temperature (1 byte)
  Wire.write(HotTemp);                                                // Sending hot Water Temperature (1 byte)
  for(int i=0;i<=3;i++){
    Wire.write(ColdValue.ColdVal8[i]);                                // Sending Cold Water Counter value (4 bytes)
  }
  for(int i=0;i<=3;i++){
    Wire.write(HotValue.HotVal8[i]);                                  // Sending Hot Water Counter value (4 bytes)
  }
}

// REFRESH OLED SCREEN ROUTINE______________________________________________________________________________________________________________________________________________________
void RefreshOLED(){                                                   // Routine for refreshing OLED scriin (moving here seeems to save some RAM
  myOLED.clrScr();                                                    // Clearing screen
  char line[32];
  sprintf(line,"COLD:%05d,%03d/",int(ColdValue.ColdVal32/1000),ColdValue.ColdVal32%1000);
  myOLED.print(String(line)+String(ColdTemp)+"~C", LEFT, 0);
  sprintf(line,"HOT :%05d,%03d/",int(HotValue.HotVal32/1000),HotValue.HotVal32%1000);
  myOLED.print(String(line)+String(HotTemp)+"~C", LEFT, 8);
  Time t = rtc.time();                                                // Get the current time and date from the chip.
  const String day = dayAsString(t.day);                              // Name the day of the week.
  snprintf(line,sizeof(line),"%s%04d-%02d-%02d %02d:%02d:%02d",       // Format the time and date and insert into the temporary buffer.
         day.c_str(),
         t.yr, t.mon, t.date,
         t.hr, t.min, t.sec);
  myOLED.print(line,LEFT,16);
  byte MaxLine=32;
  float LitersPerDot=(float)LitersPerFullLine/MaxLine;
  byte CurLine=0;
  myOLED.print(String(LitersPerFullLine)+"/Last 24H:"+String(total24)+"L",LEFT,24);
  myOLED.drawLine(0,32,0,63);                                         // Box for graph
  myOLED.drawLine(0,32,127,32);
  myOLED.drawLine(127,32,127,63);
  myOLED.drawLine(0,63,127,63);
  for(byte i=1;i<=4;i++){                                             // Drawing middle lines
    myOLED.drawLine(0,63-i*int(round(float(MaxLine)/5)),127,63-i*int(round(float(MaxLine)/5)));
  } 
  for(byte i=0;i<=23;i++){
    if (WaterCons[i]>LitersPerFullLine){                              // Protection for line geting too large
      CurLine=MaxLine;
    }else{
      CurLine=int(WaterCons[i]/LitersPerDot);                         // Drawing hour graphs
    }
    for(byte j=1;j<=5;j++){
      myOLED.drawLine(i*5+j+4,63,i*5+j+4,63-CurLine);
    }
  }
  myOLED.update();                                                    // Udating screen
  myOLED.clrScr();                                                    // Cleaning display buffer
  ScrUpdated=true;                                                    // Setting flag what screen updated this second
}

// GET WATER VALUES ROUTINE_______________________________________________________________________________________________________________
void GetWaterValues() {                                               // Detecting hall sensor changes and increasing values
  if (ColdDebouncer.update()==true) {                                 // If state changed
    ColdHallvalue = ColdDebouncer.read();                             // Get the update value
    if ( ColdHallvalue == HIGH) {                                     // If Hall Sensor changes to HIGH - we reached number 3 on last dial
      ColdValue.ColdVal32=ColdValue.ColdVal32+3;                      // Adding 3 Liters to Cold Value
    }else{                                                            // If Hall Sensor changes to LOW - we reached number 0 on last dial
      ColdValue.ColdVal32=ColdValue.ColdVal32+7;                      // Adding 7 Liters to Cold Value
      //Invoking sending current value
    }
    WriteWaterValuesToRAM();                                          // Writing current values to RAM on RTC
  }
  if ( HotDebouncer.update() ==true) {                                // If state changed
    HotHallvalue = HotDebouncer.read();                               // Get the update value
    if ( HotHallvalue == HIGH) {                                      // If Hall Sensor changes to HIGH - we reached number 3 on last dial
      HotValue.HotVal32=HotValue.HotVal32+3;                          // Adding 3 Liters to Hot Value
    }else{                                                            // If Hall Sensor changes to LOW - we reached number 0 on last dial
      HotValue.HotVal32=HotValue.HotVal32+7;                          // Adding 7 Liters to Hot Value
    }
    WriteWaterValuesToRAM();                                          // Writing current values to RAM on RTC

  }
}

// GET TEMPERATURE VALUES ROUTINE______________________________________________________________________________________________________________
void GetTempValues() {                                                // Every 10 seconds - re read temperature sensors (it takes about 200 ms)
  sensors.requestTemperatures();                                      // Reading temperatures
  ColdTemp=int(sensors.getTempCByIndex(0));                                // Reading first sensor
  HotTemp=int(sensors.getTempCByIndex(1));                                 // Reading second sensor
  TempRead=true;                                                      // Setting flag to avoid re-read
}

// RECALCULATING 24 HOURS WATER USAGE ROUTINE___________________________________________________________________________________________________
void Recalc24Usage() {                                                // Recalculating last 24 hours value
  byte i=0;
  total24=0;
  byte MaxConsume=0;                                                  // Detecting maximum value
  for(i=23;i>=1;i--){                                                 // Recalculating array from the tail
    WaterCons[i]=WaterCons[i-1];
    total24=total24+WaterCons[i];
    if (WaterCons[i]>MaxConsume){
      MaxConsume=WaterCons[i];                                        // Detecting maximum consume value
    }
//    Serial.print(String(WaterCons[i])+"|");
  }
  WaterCons[0]=ColdValue.ColdVal32+HotValue.HotVal32-OldColdValue-OldHotValue; // Latest water consumption
  if (WaterCons[0]>MaxConsume){
    MaxConsume=WaterCons[0];                                          // Detecting maximum consume value
  }
      
  
//  Serial.println(String(WaterCons[0])+">"+String(SimpleTimer)+"("+int(millis()/1000)+")"+"["+String(millis())+"]");
  total24=total24+WaterCons[0];
  OldColdValue=ColdValue.ColdVal32;                                   // Old Cold Value in Liters
  OldHotValue=HotValue.HotVal32;                                      // Old Hot Value in Liters
  if(MaxConsume<50){                                                  // Rescaling graph maximum
    LitersPerFullLine=50;
    }else{LitersPerFullLine=((MaxConsume/50)+1)*50;                   // Max Consume larger then 50, so graph maximum scales from 100 to 250 
  }
  WriteWaterValuesToRAM();                                            // re-writing RAM values each hour
  TotalRecalc=true;
}

// ROUTINE FOR WRITING WATER VALUES TO RAM IN RTC CHIP_______________________________________________________________________________________________
void WriteWaterValuesToRAM(){                                         // Routine to write to RAM values: current time, cold value, hot value - to restore it on power loss
  rtc.writeProtect(false);                                            // Disabling write protection
  for (int i = 0; i < DS1302::kRamSize; ++i) {                        // Clearing all RAM in RTC
    rtc.writeRam(i, 0x00);
  }

  for(int i=0;i<=3;i++){
    rtc.writeRam(i,ColdValue.ColdVal8[i]);                            // Write cold value to bytes 0-3
  }
  for(int i=4;i<=7;i++){
    rtc.writeRam(i,HotValue.HotVal8[i-4]);                            // Write cold value to bytes4-7
  }
  for(int i=8;i<=30;i++){                                             // Writing last 23-hours values (dropping current hour due to insufficient 1 byte of memory)
    rtc.writeRam(i,WaterCons[i-7]);                                   // Dropping [0], starting from [1]to[23]
  }
  rtc.writeProtect(true);                                             // Re-Enabling Write Protection
}
 
