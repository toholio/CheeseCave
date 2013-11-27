#include <Wire.h>
#include "RTClib.h"
#include <gLCD.h>
#include <stdlib.h>
#include <DHT.h>

// Temperature and humidity sensor:
// Connect pin 1 (on the left) of the sensor to +5V
// Connect pin 2 of the sensor to whatever your DHTPIN is
// Connect pin 4 (on the right) of the sensor to GROUND
// Connect a 10K resistor from pin 2 (data) to pin 1 (power) of the sensor
#define DHTTYPE DHT22   // DHT 22  (AM2302)
#define DHTPIN 2     // what pin we're connected to
DHT dht( DHTPIN, DHTTYPE );

// Sparkfun GLCD shield:
const char RST = 8;
const char CS = 9;
const char Clk = 13;
const char Data = 11;
gLCD graphic( RST, CS, Clk, Data, HIGH_SPEED );

// Realtime clock:
RTC_DS1307 RTC;

// Cave ideal conditions:
#define TARGET_TEMPERATURE 8.5
#define TARGET_HUMIDITY 85.0

// Seconds between compressor/humidifier switches:
#define FRIDGE_WAIT 60
#define HUMIDIFIER_WAIT 10

// I2C relay command numbers and last state:
#define FRIDGE_RELAY               1
#define FRIDGE_RELAY_TIMED_OFF     4
#define HUMIDIFIER_RELAY           2
#define HUMIDIFIER_RELAY_TIMED_OFF 7
bool fridgeState = false;
bool humidifierState = false;

// State of the cave and relays:
float lastHumidity = 0;
float lastTemperature = 0;
DateTime lastFridgeTime;
DateTime lastHumidifierTime;
DateTime lastSensorUpdate;

void setup()
{
  Serial.begin( 9600 );
  
  Wire.begin();
  RTC.begin();

  if ( !RTC.isrunning() ) {
    // following line sets the RTC to the date & time this sketch was compiled
    RTC.adjust( DateTime( __DATE__, __TIME__ ) );
  }
 
  dht.begin();
  
  graphic.begin( 0, 0, 0, PHILLIPS_3 );
  graphic.setBackColour( GLCD_BLACK );
  graphic.Clear();
  
  SetRelay( FRIDGE_RELAY, fridgeState );
  SetRelay( HUMIDIFIER_RELAY, humidifierState );
  
  lastFridgeTime = RTC.now();
  lastHumidifierTime = RTC.now();
  lastSensorUpdate = 0; // force an immediate read
}

void SetRelay( int relay, bool state )
{
  Wire.beginTransmission( 0x31 );
  Wire.write( relay );
  Wire.write( state ? 1 : 0 );
  Wire.endTransmission();
}

void SetRelaysTimedOff()
{
  // Delays are given as two byte multiples of 200mS.
  
  Wire.beginTransmission( 0x31 );
  Wire.write( FRIDGE_RELAY_TIMED_OFF );
  Wire.write( 0x1 );
  Wire.write( 0x2c );
  Wire.endTransmission();
  
  Wire.beginTransmission( 0x31 );
  Wire.write( HUMIDIFIER_RELAY_TIMED_OFF );
  Wire.write( 0x1 );
  Wire.write( 0x2c );
  Wire.endTransmission();
}

unsigned int SecondsSince( const DateTime time )
{
   return RTC.now().unixtime() - time.unixtime();
}

void UpdateTempAndHumidityDisplay( void )
{
  char buffer[5];
  
  graphic.setFont( Large_SolidBG );
  graphic.setForeColour( GLCD_WHITE );
  graphic.setCoordinate( 5, 5 );
  graphic.print( dtostrf( lastTemperature, 4, 1, buffer ) );
  graphic.setCoordinate( 70, 5 );
  graphic.print( dtostrf( lastHumidity, 4, 1, buffer ) );
  graphic.setFont( Normal_SolidBG );
  graphic.setCoordinate( 55, 5 );
  graphic.print( "C" );
  graphic.setCoordinate( 120, 5 );
  graphic.print( "%" );
}

void UpdateEquipmentStateDisplay( void )
{
  graphic.setFont( Normal_SolidBG );
  unsigned long colour = GLCD_WHITE;
  char *text = NULL;
  
  graphic.setCoordinate( 10, 30 );
  graphic.setForeColour( GLCD_WHITE );
  
  graphic.print( "Refrigerator:" );
  if ( fridgeState ) {
    colour = GLCD_GREEN;
    text = " ON";
  } else {
    colour = GLCD_RED;
    text = "OFF";
  }
  graphic.setCoordinate( 100, 30 );
  graphic.setForeColour( colour );
  graphic.print( text );
  
  graphic.setCoordinate( 10, 40 );
  graphic.setForeColour( GLCD_WHITE );
  graphic.print( "Humidifier:" );
  if ( humidifierState ) {
    colour = GLCD_GREEN;
    text = " ON";
  } else {
    colour = GLCD_RED;
    text = "OFF";
  }
  graphic.setCoordinate( 100, 40 );
  graphic.setForeColour( colour );
  graphic.print( text );
}

void UpdateLockoutDisplay( void )
{
  graphic.setCoordinate( 10, 60 );
  graphic.setForeColour( GLCD_WHITE );
  graphic.print( "Lockout: F   H  " );
  int lockout = FRIDGE_WAIT - SecondsSince( lastFridgeTime );
  graphic.setCoordinate( 70, 60 );
  if ( lockout > 0 ) { 
    graphic.print( lockout );
  } else {
    graphic.print( "--" );
  }
  lockout = HUMIDIFIER_WAIT - SecondsSince( lastHumidifierTime );
  graphic.setCoordinate( 94, 60 );
  if ( lockout > 0 ) {
    graphic.print( lockout );
  } else {
    graphic.print( "--" );
  }
}

void UpdateDateAndTimeDisplay( void )
{
  char buffer[11];
  DateTime now = RTC.now();
  DateString( now ).toCharArray( buffer, 11 );
  graphic.setCoordinate( 10, 80 );
  graphic.print( buffer );
  TimeString( now ).toCharArray( buffer, 11 );
  graphic.setCoordinate( 10, 90 );
  graphic.print( buffer ); 
}

String PadZero( const int number )
{
   return String( number < 10 ? "0" : "" ) + number;
}

String DateString( const DateTime time )
{
  return String( time.year() ) + '-' + PadZero( time.month() ) + '-' + PadZero( time.day() );
}

String TimeString( const DateTime time )
{
  return PadZero( time.hour() ) + ':' + PadZero( time.minute() ) + ':' + PadZero( time.second() );
}

void UpdateLog( void )
{
  DateTime now = RTC.now();
  Serial.print( DateString( now ) );
  Serial.print(',');
  Serial.print( TimeString( now ) );
  Serial.print(',');
  
  // check if returns are valid, if they are NaN (not a number) then something went wrong!
  if ( isnan( lastTemperature ) || isnan( lastHumidity ) )  {
    Serial.println("0,0");
  } else {
    Serial.print( lastTemperature );
    Serial.print(',');
    Serial.print( lastHumidity );
  }
  Serial.print(',');
  
  Serial.print( fridgeState );
  Serial.print(',');
  Serial.print( humidifierState );
  Serial.println();
}

void loop() {
  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
  if ( SecondsSince( lastSensorUpdate ) > 0 ) {
    lastHumidity = dht.readHumidity();
    lastTemperature = dht.readTemperature();
    
    UpdateLog();
    lastSensorUpdate = RTC.now();
  }
  
  UpdateTempAndHumidityDisplay();
  UpdateEquipmentStateDisplay();
  UpdateLockoutDisplay();
  UpdateDateAndTimeDisplay();

  // The compressor and humidifier might be damaged if switched on and off too often.
  int desiredFridge = lastTemperature > TARGET_TEMPERATURE ? HIGH : LOW;
  if ( SecondsSince( lastFridgeTime ) >= FRIDGE_WAIT && desiredFridge != fridgeState ) {
    fridgeState = desiredFridge;
    SetRelay( FRIDGE_RELAY, fridgeState );
    lastFridgeTime = RTC.now();
  }
    
  int desiredHumidifer = lastHumidity < TARGET_HUMIDITY ? HIGH : LOW;
  if ( SecondsSince( lastHumidifierTime ) >= HUMIDIFIER_WAIT && desiredHumidifer != humidifierState ) {
    humidifierState = desiredHumidifer;
    SetRelay( HUMIDIFIER_RELAY, humidifierState );
    lastHumidifierTime = RTC.now();
  }
  
  // Push back the automatic shutdown of the relays.
  SetRelaysTimedOff();
}

