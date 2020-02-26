#include <Arduino.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESPAsyncWebServer.h>

#include <TFT_eSPI.h> // Graphics and font library for ILI9341 driver chip
#include <SPI.h>
#define TFT_GREY 0x5AEB // New colour
TFT_eSPI tft = TFT_eSPI();  // Invoke library

#include <Preferences.h>
Preferences preferences;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

String formattedDate;
String dayStamp;
String timeStamp;

// Set offset time in seconds to adjust for your timezone, for example:
// GMT +1 = 3600
// GMT +8 = 28800
// GMT -1 = -3600
// GMT 0 = 0
int32_t time_offset = -28800;

// time in seconds
uint32_t current_time;
uint32_t destination_time;
uint32_t departure_time;
uint32_t yellow_time = 30;  // minutes
uint32_t on_set_time;
uint32_t on_time;
uint32_t manual_time = 15;  // minutes
uint32_t manual_on_set_time = 15;

// initial mode to run on manual
uint8_t light_mode = 2;

// edit time mode
uint8_t edit_mode = 0;
int8_t edit_digit = 0;

/* Put your SSID & Password */
const char* ssid = "synshop";  // Enter SSID here
const char* password = "makestuffawesome";  //Enter Password here

/* Put IP Address details */
//IPAddress local_ip(10,0,40,12);
//IPAddress gateway(10,0,40,1);
//IPAddress subnet(255,255,255,0);

// yes it's on port 80, but the ESP32 runs really really slow when it has to do crypto.
AsyncWebServer server(80);

struct Button {
    const uint8_t PIN;
    uint32_t time_key_pressed;
    uint8_t value;
    uint8_t pressed;
};

// gpio inputs up 32, down 33, right 34, left 35, select 36
Button bn_up = {33, 0, 0, false};
Button bn_down = {35, 0, 0, false};
Button bn_right = {34, 0, 0, false};
Button bn_left = {32, 0, 0, false};
Button bn_select = {36, 0, 0, false};

void IRAM_ATTR isr(void* arg) {
    Button* s = static_cast<Button*>(arg);
    s->time_key_pressed = current_time;
    s->value = digitalRead(s->PIN);
    s->pressed = 1;
}

// gpio outputs red 25, yellow 26, green 27
uint8_t red_lightpin = 25;
bool red_lightstatus = LOW;

uint8_t yellow_lightpin = 26;
bool yellow_lightstatus = LOW;

uint8_t green_lightpin = 27;
bool green_lightstatus = LOW;


void setup() {
  pinMode(bn_up.PIN, INPUT_PULLUP);
  pinMode(bn_down.PIN, INPUT_PULLUP);
  pinMode(bn_right.PIN, INPUT_PULLUP);
  pinMode(bn_left.PIN, INPUT_PULLUP);
  pinMode(bn_select.PIN, INPUT_PULLUP);
  attachInterruptArg(bn_up.PIN, isr, &bn_up, FALLING);
  attachInterruptArg(bn_down.PIN, isr, &bn_down, FALLING);
  attachInterruptArg(bn_right.PIN, isr, &bn_right, FALLING);
  attachInterruptArg(bn_left.PIN, isr, &bn_left, FALLING);
  attachInterruptArg(bn_select.PIN, isr, &bn_select, FALLING);
  
  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);

  tft.setCursor(12, 2, 2);
  tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextFont(2);

  Serial.begin(115200);
  pinMode(red_lightpin, OUTPUT);
  pinMode(yellow_lightpin, OUTPUT);
  pinMode(green_lightpin, OUTPUT);

  preferences.begin("synshop_signal", false);

  // initial setup
  // preferences.clear();
  // preferences.remove("key");
  // preferences.putUInt("yellow_time", yellow_time);
  // preferences.putUInt("manual_time", manual_time);
  // preferences.putUInt("time_offset", time_offset);

  yellow_time = preferences.getUInt("yellow_time", false);
  manual_time = preferences.getUInt("manual_time", false);
  time_offset = preferences.getLong("time_offset", false);
  preferences.end();

  Serial.println("Loading preferences:");
  Serial.print("- Yellow time:");
  Serial.println(yellow_time);
  Serial.print("- Manual time:");
  Serial.println(manual_time);
  Serial.print("- Time offset:");
  Serial.println(time_offset);
  Serial.println("");
  
  // turn off green light
  digitalWrite(green_lightpin, HIGH);

  tft.print("Connecting to ");
  tft.println(ssid);

  Serial.print("Connecting to ");
  Serial.println(ssid);

  uint8_t wifi_wait = 0;

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    // flash yellow light while connecting
    wifi_wait++;
    if(wifi_wait % 2)
    {
      digitalWrite(yellow_lightpin, HIGH);
    }
    else
    {
      digitalWrite(yellow_lightpin, LOW);
    }

    delay(500);
    Serial.print(".");
  }

  // turn off yellow and red lights
  digitalWrite(yellow_lightpin, HIGH);
  digitalWrite(red_lightpin, HIGH);

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  tft.println("Wifi connected");
  tft.print("IP address: ");
  tft.println(WiFi.localIP());

  timeClient.begin();
  timeClient.setTimeOffset(time_offset);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    // show current light status
    request->send(200, "text/html", SendHTML(red_lightstatus,yellow_lightstatus,green_lightstatus)); 
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", SendJSON()); 
  });

  server.on("/red_light_on", HTTP_GET, [](AsyncWebServerRequest *request){
    red_lightstatus = HIGH;
    light_mode = 0;
    manual_on_set_time = current_time + (manual_time * 60);
    Serial.print(formattedDate);
    Serial.println(": Manual red_light Status: ON");
    request->send(200, "text/html", SendHTML(true,yellow_lightstatus,green_lightstatus)); 
  });

  server.on("/red_light_off", HTTP_GET, [](AsyncWebServerRequest *request){
    red_lightstatus = LOW;
    light_mode = 0;
    manual_on_set_time = current_time + (manual_time * 60);
    Serial.print(formattedDate);
    Serial.println(": Manual red_light Status: OFF");
    request->send(200, "text/html", SendHTML(false,yellow_lightstatus,green_lightstatus)); 
  });

  server.on("/yellow_light_on", HTTP_GET, [](AsyncWebServerRequest *request){
    yellow_lightstatus = HIGH;
    light_mode = 0;
    manual_on_set_time = current_time + (manual_time * 60);
    Serial.print(formattedDate);
    Serial.println(": Manual yellow_light Status: ON");
    request->send(200, "text/html", SendHTML(red_lightstatus,true,green_lightstatus)); 
  });

  server.on("/yellow_light_off", HTTP_GET, [](AsyncWebServerRequest *request){
    yellow_lightstatus = LOW;
    light_mode = 0;
    manual_on_set_time = current_time + (manual_time * 60);
    Serial.print(formattedDate);
    Serial.println(": Manual yellow_light Status: OFF");
    request->send(200, "text/html", SendHTML(red_lightstatus,false,green_lightstatus)); 
  });

  server.on("/green_light_on", HTTP_GET, [](AsyncWebServerRequest *request){
    green_lightstatus = HIGH;
    light_mode = 0;
    manual_on_set_time = current_time + (manual_time * 60);
    Serial.print(formattedDate);
    Serial.println(": Manual green_light Status: ON");
    request->send(200, "text/html", SendHTML(red_lightstatus,yellow_lightstatus,true)); 
  });
  
  server.on("/green_light_off", HTTP_GET, [](AsyncWebServerRequest *request){
    green_lightstatus = LOW;
    light_mode = 0;
    manual_on_set_time = current_time + (manual_time * 60);
    Serial.print(formattedDate);
    Serial.println(": Manual green_light Status: OFF");
    request->send(200, "text/html", SendHTML(red_lightstatus,yellow_lightstatus,false)); 
  });

  server.on("/set", HTTP_GET, [] (AsyncWebServerRequest *request){
    if(request->hasParam("on_time"))
    {
      light_mode = 1;
      on_time = request->getParam("on_time")->value().toInt();
      on_set_time = current_time + (on_time * 60);

      Serial.print(formattedDate);
      Serial.print(": Automatic mode, on_time set to ");
      Serial.print(on_time);
      Serial.println(" minutes");

      departure_time = current_time;
      destination_time = on_set_time;
    }

    if(request->hasParam("time_offset"))
    {
      time_offset = request->getParam("time_offset")->value().toInt();

      timeClient.setTimeOffset(time_offset);

      Serial.print(formattedDate);
      Serial.print(": Adjusted time offset to ");
      Serial.print(time_offset);
      Serial.println(" seconds");

      preferences.begin("synshop_signal", false);
      preferences.clear();
      preferences.putUInt("manual_time", manual_time);
      preferences.putUInt("yellow_time", yellow_time);
      preferences.putLong("time_offset", time_offset);
      preferences.end();
    }

    if(request->hasParam("manual_time"))
    {
      light_mode = 0;
      manual_time = request->getParam("manual_time")->value().toInt();
      manual_on_set_time = current_time + (manual_time * 60);

      preferences.begin("synshop_signal", false);
      preferences.clear();
      preferences.putUInt("manual_time", manual_time);
      preferences.putUInt("yellow_time", yellow_time);
      preferences.putLong("time_offset", time_offset);
      preferences.end();

      Serial.print(formattedDate);
      Serial.print(": Manual mode, manual_time set to ");
      Serial.print(manual_time);
      Serial.println(" minutes");

      departure_time = current_time;
      destination_time = manual_on_set_time;
    }
 
    if(request->hasParam("yellow_time"))
    {
      yellow_time = request->getParam("yellow_time")->value().toInt();

      preferences.begin("synshop_signal", false);
      preferences.clear();
      preferences.putUInt("manual_time", manual_time);
      preferences.putUInt("yellow_time", yellow_time);
      preferences.putLong("time_offset", time_offset);
      preferences.end();
      
      Serial.print(formattedDate);
      Serial.print(": yellow_time set to ");
      Serial.print(yellow_time);
      Serial.println(" minutes");
    }

    request->send(200, "text/html", SendHTML(red_lightstatus,yellow_lightstatus,green_lightstatus));
  });

  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("HTTP server started");

  // start light mode in automatic
  light_mode = 1;

  // init departure and destination times
  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }
  
  current_time = timeClient.getEpochTime();
  destination_time = current_time;
  departure_time = current_time;

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(12, 2, 2);
}

void loop()
{
  //  server.handleClient();
  
  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }
  current_time = timeClient.getEpochTime();

  // check that screen is ok
  tft_keepalive();
  
  // start time circuit display
  tft.setCursor(12, 2, 2);
  tft.setTextColor(TFT_BLACK,TFT_WHITE); tft.setTextFont(4); tft.setTextSize(1);
  if(light_mode == 1)
  {
    tft.println("Automatic                     ");
  }
  else
  {
    tft.println("Manual                        ");
  }

  // destination time
  tft.setCursor(12, 32, 2);
  tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextFont(4);
  tft.println("Destination Time");
  
  formattedDate = getFormattedTime(destination_time);

  if(edit_mode == 0)
  {
    // normal mode just display the time
    tft.setCursor(12, 56, 2);
    tft.setTextColor(TFT_YELLOW,TFT_BLACK); tft.setTextFont(7);
    tft.println(formattedDate);
  }
  else
  {
    // edit mode

      switch(edit_digit)
      {
        case 0:
          tft.setCursor(12, 56, 2);
          tft.setTextFont(7);
          tft.setTextColor(TFT_YELLOW,TFT_BLACK);
          tft.setTextColor(TFT_BLACK,TFT_YELLOW);
          tft.print(formattedDate.charAt(0));
          tft.setTextColor(TFT_YELLOW,TFT_BLACK);
          tft.print(formattedDate.charAt(1));
          tft.print(":");
          tft.print(formattedDate.charAt(3));
          tft.print(formattedDate.charAt(4));
          tft.print(":");
          tft.print(formattedDate.charAt(6));
          tft.print(formattedDate.charAt(7));
          break;
        case 1:
          tft.setCursor(12, 56, 2);
          tft.setTextFont(7);
          tft.setTextColor(TFT_YELLOW,TFT_BLACK);
          tft.print(formattedDate.charAt(0));
          tft.setTextColor(TFT_BLACK,TFT_YELLOW);
          tft.print(formattedDate.charAt(1));
          tft.setTextColor(TFT_YELLOW,TFT_BLACK);
          tft.print(":");
          tft.print(formattedDate.charAt(3));
          tft.print(formattedDate.charAt(4));
          tft.print(":");
          tft.print(formattedDate.charAt(6));
          tft.print(formattedDate.charAt(7));
          break;
        case 2:
          tft.setCursor(12, 56, 2);
          tft.setTextFont(7);
          tft.setTextColor(TFT_YELLOW,TFT_BLACK);
          tft.print(formattedDate.charAt(0));
          tft.print(formattedDate.charAt(1));
          tft.print(":");
          tft.setTextColor(TFT_BLACK,TFT_YELLOW);
          tft.print(formattedDate.charAt(3));
          tft.setTextColor(TFT_YELLOW,TFT_BLACK);
          tft.print(formattedDate.charAt(4));
          tft.print(":");
          tft.print(formattedDate.charAt(6));
          tft.print(formattedDate.charAt(7));
          break;
        case 3:
          tft.setCursor(12, 56, 2);
          tft.setTextFont(7);
          tft.setTextColor(TFT_YELLOW,TFT_BLACK);
          tft.print(formattedDate.charAt(0));
          tft.print(formattedDate.charAt(1));
          tft.print(":");
          tft.print(formattedDate.charAt(3));
          tft.setTextColor(TFT_BLACK,TFT_YELLOW);
          tft.print(formattedDate.charAt(4));
          tft.setTextColor(TFT_YELLOW,TFT_BLACK);
          tft.print(":");
          tft.print(formattedDate.charAt(6));
          tft.print(formattedDate.charAt(7));
          break;
        case 4:
          tft.setCursor(12, 56, 2);
          tft.setTextFont(7);
          tft.setTextColor(TFT_YELLOW,TFT_BLACK);
          tft.print(formattedDate.charAt(0));
          tft.print(formattedDate.charAt(1));
          tft.print(":");
          tft.print(formattedDate.charAt(3));
          tft.print(formattedDate.charAt(4));
          tft.print(":");
          tft.setTextColor(TFT_BLACK,TFT_YELLOW);
          tft.print(formattedDate.charAt(6));
          tft.setTextColor(TFT_YELLOW,TFT_BLACK);
          tft.print(formattedDate.charAt(7));
          break;
        case 5:
          tft.setCursor(12, 56, 2);
          tft.setTextFont(7);
          tft.setTextColor(TFT_YELLOW,TFT_BLACK);
          tft.print(formattedDate.charAt(0));
          tft.print(formattedDate.charAt(1));
          tft.print(":");
          tft.print(formattedDate.charAt(3));
          tft.print(formattedDate.charAt(4));
          tft.print(":");
          tft.print(formattedDate.charAt(6));
          tft.setTextColor(TFT_BLACK,TFT_YELLOW);
          tft.print(formattedDate.charAt(7));
          tft.setTextColor(TFT_YELLOW,TFT_BLACK);
          break;
      }

    // reduce flicker
    tft.setCursor(12, 32, 2);
    tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextFont(4);
    tft.println("Destination Time");
  }

  // present time
  tft.setCursor(12, 110, 2);
  tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextFont(4);
  tft.println("Present Time");

  formattedDate = getFormattedTime(current_time);

  tft.setCursor(12, 134, 2);
  tft.setTextColor(TFT_GREEN,TFT_BLACK); tft.setTextFont(7);
  tft.println(formattedDate);

  // last departure
  tft.setCursor(12, 188, 2);
  tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextFont(4);
  tft.println("Last Departure");

  formattedDate = getFormattedTime(departure_time);

  tft.setCursor(12, 212, 2);
  tft.setTextColor(TFT_ORANGE,TFT_BLACK); tft.setTextFont(7);
  tft.println(formattedDate);
  // end time circuit display

  if(red_lightstatus == HIGH)
  {
    tft.fillCircle(60,290,18, TFT_RED);
    tft.drawCircle(60,290,19, TFT_RED);
    tft.drawCircle(60,290,20, TFT_RED);
  }
  else
  {
    tft.fillCircle(60,290,18, TFT_BLACK);
    tft.drawCircle(60,290,19, TFT_RED);
    tft.drawCircle(60,290,20, TFT_RED);
  }

  if(yellow_lightstatus == HIGH)
  {
    tft.fillCircle(120,290,18, TFT_YELLOW);
    tft.drawCircle(120,290,19, TFT_YELLOW);
    tft.drawCircle(120,290,20, TFT_YELLOW);
  }
  else
  {
    tft.fillCircle(120,290,18, TFT_BLACK);
    tft.drawCircle(120,290,19, TFT_YELLOW);
    tft.drawCircle(120,290,20, TFT_YELLOW);
  }

  if(green_lightstatus == HIGH)
  {
    tft.fillCircle(180,290,20, TFT_GREEN);
    tft.drawCircle(180,290,19, TFT_GREEN);
    tft.drawCircle(180,290,20, TFT_GREEN);
  }
  else
  {
    tft.fillCircle(180,290,18, TFT_BLACK);
    tft.drawCircle(180,290,19, TFT_GREEN);
    tft.drawCircle(180,290,20, TFT_GREEN);
  }

  if (edit_mode == 1 && bn_up.pressed && bn_up.value == 0)
  {
    Serial.println("up");
    bn_up.pressed = false;
    switch(edit_digit){
      case 0:
        // adjust tens for hours
        if( ((destination_time % 86400L) / 3600) >= 20 )
        {
          // time is 20:00 or above, can't add 10 hours so subtract 20
          destination_time = destination_time - 72000;
        }
        else
        {
          if( ((destination_time % 86400L) / 3600) >= 14 )
          {
            // time is 14:00 or above, can't add 10 hours so subtract how many hours there are and add 20
            destination_time = destination_time - (long ((destination_time % 86400L) / 3600) * 3600) + 72000;
          }
          else
          {
            // add ten hours
            destination_time = destination_time + 36000;
          }
        }
        break;
      case 1:
        // adjust ones for hours
        if( ((destination_time % 86400L) / 3600) >= 23 )
        {
          // time is greater than 23:00 so subtract three hours
          destination_time = destination_time - 10800;
        }
        else if( ((destination_time % 86400L) / 3600) >= 20 )
        {
          // time is greater than 20:00 so add an hour
          destination_time = destination_time + 3600;
        }
        else if( ((destination_time % 86400L) / 3600) >= 19 )
        {
          // time is greater than 19:00 so subtract 9 hours
          destination_time = destination_time - 32400;
        }
        else if( ((destination_time % 86400L) / 3600) >= 10 )
        {
          // time is greater than 10:00 so add an hour
          destination_time = destination_time + 3600;
        }
        else if( ((destination_time % 86400L) / 3600) >= 9 )
        {
          // time is greater than 09:00 so subtract 9 hours
          destination_time = destination_time - 32400;
        }
        else
        {
          // add an hour
          destination_time = destination_time + 3600;
        }
        break;
      case 2:
        // adjust tens for minutes
        if( ((destination_time % 3600) / 60) >= 50 )
        {
          // time is xx:50 or above, can't add ten minutes so subtract 50 minutes
          destination_time = destination_time - 3000;
        }
        else
        {
          // add ten minutes
          destination_time = destination_time + 600;
        }
        break;
      case 3:
        // adjust ones for minutes
        if( (((destination_time % 3600) / 60) % 10) >= 9 )
        {
          // time is xx:x9, can't add one minute so subtract 9 minutes
          destination_time = destination_time - 540;
        }
        else
        {
          // add one minutes
          destination_time = destination_time + 60;
        }
        break;
      case 4:
        // adjust tens for seconds
        if( (destination_time % 60) >= 50 )
        {
          // time is xx:xx:50 or above, can't add ten seconds so subtract 50 seconds
          destination_time = destination_time - 50;
        }
        else
        {
          // add ten seconds
          destination_time = destination_time + 10;
        }
        break;
      case 5:
        // adjust ones for seconds
        if( ((destination_time % 60) % 10) >= 9 )
        {
          // time is xx:xx:x9, can't add one second so subtract 9 seconds
          destination_time = destination_time - 9;
        }
        else
        {
          // add one second
          destination_time = destination_time + 1;
        }
        break;
    }
  }
  if (edit_mode == 1 && bn_down.pressed && bn_down.value == 0)
  {
    Serial.println("down");
    bn_down.pressed = false;
    switch(edit_digit){
      case 0:
        // adjust tens for hours
        if( ((destination_time % 86400L) / 3600) <= 3 )
        {
          // time less than or equal to 03:00 so add 20 hours
          destination_time = destination_time + (72000);
        }
        else if( ((destination_time % 86400L) / 3600) <= 9 )
        {
          // time less than or equal to 09:00 so subtract current hours and add 20 hours
          destination_time = destination_time - (long ((destination_time % 86400L) / 3600) * 3600) + 72000;
        }
        else
        {
          // subtract an hour
          destination_time = destination_time - (36000);
        }
        break;
      case 1:
        // adjust ones for hours
        if( ((destination_time % 86400L) / 3600) >= 21 )
        {
          // time is greater than 21:00 so subtract an hour
          destination_time = destination_time - 3600;
        }
        else if( ((destination_time % 86400L) / 3600) >= 20 )
        {
          // time is greater than 20:00 so add three hours
          destination_time = destination_time + 10800;
        }
        else if( ((destination_time % 86400L) / 3600) >= 11 )
        {
          // time is greater than 11:00 so subtract an hour
          destination_time = destination_time - 3600;
        }
        else if( ((destination_time % 86400L) / 3600) >= 10 )
        {
          // time is greater than 10:00 so add nine hours
          destination_time = destination_time + 32400;
        }
        else if( ((destination_time % 86400L) / 3600) >= 1 )
        {
          // time is greater than 01:00 so subtract an hour
          destination_time = destination_time - 3600;
        }
        else
        {
          // time is less than 01:00 so add nine hours
          destination_time = destination_time + 32400;
        }
        break;
      case 2:
        // adjust tens for minutes
        if( ((destination_time % 3600) / 60) >= 10 )
        {
          // time is xx:10 or above, so subtract 10 minutes
          destination_time = destination_time - 600;
        }
        else
        {
          // time is xx:00 or above, can't subtract ten minutes so add 50 minutes
          destination_time = destination_time + 3000;
        }
        break;
      case 3:
        // adjust ones for minutes
        if( (((destination_time % 3600) / 60) % 10) <= 0 )
        {
          // time is xx:x0, can't subtract one minute so add 9 minutes
          destination_time = destination_time + 540;
        }
        else
        {
          // subtract one minutes
          destination_time = destination_time - 60;
        }
        break;
      case 4:
        // adjust tens for seconds
        if( (destination_time % 60) >= 10 )
        {
          // time is xx:xx:10 or above, so subtract 10 seconds
          destination_time = destination_time - 10;
        }
        else
        {
          // time is xx:xx:00 or above, can't subtract ten seconds so add 50 seconds
          destination_time = destination_time + 50;
        }
        break;
      case 5:
        // adjust ones for seconds
        if( ((destination_time % 60) % 10) <= 0 )
        {
          // time is xx:xx:x0, can't subtract one second so add 9 seconds
          destination_time = destination_time + 9;
        }
        else
        {
          // subtract one second
          destination_time = destination_time - 1;
        }
        break;
    }
  }
  if (edit_mode == 1 && bn_right.pressed && bn_right.value == 0)
  {
    Serial.println("right");
    bn_right.pressed = false;
    if(edit_mode)
    {
      edit_digit++;
      if(edit_digit > 5)
      {
        edit_digit = 0;
        tft.init();
        tft.setRotation(2);
        tft.fillScreen(TFT_BLACK);
      }
    }
  }
  if (edit_mode == 1 && bn_left.pressed && bn_left.value == 0)
  {
    Serial.println("left");
    bn_left.pressed = false;
    if(edit_mode)
    {
      edit_digit--;
      if(edit_digit < 0)
      {
        edit_digit = 5;
        tft.init();
        tft.setRotation(2);
        tft.fillScreen(TFT_BLACK);
      }
    }
  }
  if (bn_select.pressed && bn_select.value == 0)
  {
    Serial.println("select");
    bn_select.pressed = false;
    
    if(edit_mode == 0)
    {
      edit_mode = 1;
      edit_digit = 0;
      if(destination_time < current_time)
      {
        destination_time = current_time;
      }
      tft.init();
      tft.setRotation(2);
      tft.fillScreen(TFT_BLACK);
    }
    else
    {
      edit_mode = 0;

      // 240x320
      if(destination_time < current_time)
      {
        tft.fillRoundRect(20, 20, 180, 260, 12, TFT_BLACK);
        tft.drawRoundRect(20, 20, 180, 260, 12, TFT_RED);
        tft.setCursor(32, 40, 2);
        tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextFont(4);
        tft.println("Error:");
        tft.setCursor(32, 70, 2);
        tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextFont(4);
        tft.println("Flux");
        tft.setCursor(32, 100, 2);
        tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextFont(4);
        tft.println("Capacitor");
        tft.setCursor(32, 130, 2);
        tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextFont(4);
        tft.println("Not Found!");
        tft.setCursor(32, 180, 2);
        tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextFont(4);
        tft.println("Reverse");
        tft.setCursor(32, 210, 2);
        tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextFont(4);
        tft.println("Time Travel");
        tft.setCursor(32, 240, 2);
        tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextFont(4);
        tft.println("Disabled.");

        int n = 0;
        while(n < 500)
        {
          n++;
          if(bn_select.pressed && bn_select.value == 0)
          {
            n = 500;                           
          }
          delay(10);
        }

        tft.fillScreen(TFT_BLACK);

        // destination_time = on_set_time;
      }
      on_set_time = destination_time;
      light_mode = 1;
    }
  }

  if(light_mode == 1)
  {
    // automated mode
    if(current_time > on_set_time)
    {
      if(red_lightstatus == LOW)
      {
        Serial.print(formattedDate);
        Serial.println(": Light changed to red");
      }
      // set light to red
      red_lightstatus = HIGH;
      yellow_lightstatus = LOW;
      green_lightstatus = LOW;
    }
    else if(current_time > on_set_time - (yellow_time * 60))
    {
      if(yellow_lightstatus == LOW)
      {
        Serial.print(formattedDate);
        Serial.println(": Light changed to yellow");
      }
      // set light to yellow
      red_lightstatus = LOW;
      yellow_lightstatus = HIGH;
      green_lightstatus = LOW;
    }
    else if(current_time <= on_set_time)
    {
      if(green_lightstatus == LOW)
      {
        Serial.print(formattedDate);
        Serial.println(": Light changed to green");
      }
      // set light to green
      red_lightstatus = LOW;
      yellow_lightstatus = LOW;
      green_lightstatus = HIGH;
    }
  }
  else if(light_mode == 2)
  {
    // initial manual mode
    Serial.print(formattedDate);
    Serial.print(": Manual mode for ");
    Serial.print(manual_time);
    Serial.println(" minutes");
    
    light_mode = 0;

    manual_on_set_time = current_time + (manual_time * 60);
    departure_time = current_time;
    destination_time = manual_on_set_time;
  }
  else
  {
    // manual mode
    if(current_time > manual_on_set_time)
    {
      Serial.print(formattedDate);
      Serial.println(": Changing to automatic mode");

      // mode changed so set the departure time
      departure_time = current_time;

      // switch destination to on set time
      if(on_set_time)
      {
        destination_time = on_set_time;
      }

      // end of manual mode, switch back to automatic
      light_mode = 1;

      // inefficient to do this again here, but meh...
      if(current_time > on_set_time)
      {
        Serial.print(formattedDate);
        Serial.println(": Light set to red");

        // set light to red
        red_lightstatus = HIGH;
        yellow_lightstatus = LOW;
        green_lightstatus = LOW;
      }
      else if(current_time > on_set_time - (yellow_time * 60))
      {
        Serial.print(formattedDate);
        Serial.println(": Light set to yellow");

        // set light to yellow
        red_lightstatus = LOW;
        yellow_lightstatus = HIGH;
        green_lightstatus = LOW;
      }
      else if(current_time <= on_set_time)
      {
        Serial.print(formattedDate);
        Serial.println(": Light set to green");

        // set light to green
        red_lightstatus = LOW;
        yellow_lightstatus = LOW;
        green_lightstatus = HIGH;
      }
    }
  }
  
  if(red_lightstatus)
  {digitalWrite(red_lightpin, LOW);}
  else
  {digitalWrite(red_lightpin, HIGH);}
  
  if(yellow_lightstatus)
  {digitalWrite(yellow_lightpin, LOW);}
  else
  {digitalWrite(yellow_lightpin, HIGH);}

  if(green_lightstatus)
  {digitalWrite(green_lightpin, LOW);}
  else
  {digitalWrite(green_lightpin, HIGH);}
}

void tft_keepalive(void)
{
  /*
   * this code doesn't work for some reason, reads are always 0
   */
/*
  uint16_t read_color;
  tft.drawPixel(1,1,TFT_DARKGREY);
  read_color = tft.readPixel(1,1);
  if(read_color != TFT_DARKGREY)
  {
    Serial.println(read_color);

    tft.init();
    tft.setRotation(2);
    tft.fillScreen(TFT_BLACK);
  }
  */
  // it's a hack but reset the display every ten minutes and five seconds
  if((current_time % 1805) == 0)
  {
    tft.init();
    tft.setRotation(2);
    tft.fillScreen(TFT_BLACK);
  }
}

String getFormattedTime(long rawTime)
{
  unsigned long hours = (rawTime % 86400L) / 3600;
  String hoursStr = hours < 10 ? "0" + String(hours) : String(hours);

  unsigned long minutes = (rawTime % 3600) / 60;
  String minuteStr = minutes < 10 ? "0" + String(minutes) : String(minutes);

  unsigned long seconds = rawTime % 60;
  String secondStr = seconds < 10 ? "0" + String(seconds) : String(seconds);

  return hoursStr + ":" + minuteStr + ":" + secondStr;
}


String SendHTML(uint8_t red_lightstat,uint8_t yellow_lightstat,uint8_t green_lightstat){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";

  // refresh the page automatically
  ptr +="<meta http-equiv=\"refresh\" content=\"15; /\">";
  // ptr += WiFi.localIP();
  // ptr +="\">";

  ptr +="<title>LED Control</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto;}\n";
  ptr +="body{margin-top: 0px;} h1 {color: #444444;margin: 5px auto 5px;} h3 {color: #444444;margin-bottom: 10px;}\n";
  ptr +=".button {display: block;width: 80px;background-color: #3498db;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr +=".button-on {background-color: #3498db;}\n";
  ptr +=".button-on:active {background-color: #2980b9;}\n";
  ptr +=".button-off {background-color: #34495e;}\n";
  ptr +=".button-off:active {background-color: #2c3e50;}\n";
  ptr +="p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr +="div.frame {\n";
  ptr +="  width: 100%;\n";
  ptr +="  text-align: center;\n";
  ptr +="  padding: 0;\n";
  ptr +="  margin: 0;\n";
  ptr +="}\n";
  ptr +="\n";
  ptr +="div.frame-center {\n";
  ptr +="  display: inline-block;\n";
  ptr +="  text-align: left;\n";
  ptr +="  width: 800px;\n";
  ptr +="  padding: 0;\n";
  ptr +="  margin: 0;\n";
  ptr +="}\n";
  ptr +="\n";
  ptr +="div.header {\n";
  ptr +="  display: block;\n";
  ptr +="  width: 800px;\n";
  ptr +="  padding: 25px 0px;\n";
  ptr +="}\n";
  ptr +="\n";
  ptr +="div.content {\n";
  ptr +="  display: block;\n";
  ptr +="  width: 790px;\n";
  ptr +="  background-color: #72736D;\n";
  ptr +="  font-family: Verdana, Geneva, sans-serif;\n";
  ptr +="  color: #32312D;\n";
  ptr +="  z-index: 1;\n";
  ptr +="}\n";
  ptr +="\n";
  ptr +="div.content-block {\n";
  ptr +="  clear: left;\n";
  ptr +="  text-align: center;\n";
  ptr +="  display: block;\n";
  ptr +="  padding: 20px 20px;\n";
  ptr +="  z-index: 1;\n";
  ptr +="}\n";
  ptr +="\n";
  ptr +="div.content-block-header {\n";
  ptr +="  background-color: #000;\n";
  ptr +="  width: 750px;\n";
  ptr +="  color: #F8F9F7;\n";
  ptr +="  padding: 5px 0px;\n";
  ptr +="  z-index: 1;\n";
  ptr +="}\n";
  ptr +="\n";
  ptr +="div.content-block-body {\n";
  ptr +="  background-color: #fff;\n";
  ptr +="  color: #000;\n";
  ptr +="  padding: 0px;\n";
  ptr +="  z-index: 1;\n";
  ptr +="}\n";
  ptr +="\n";
  ptr +="div.control-buttons {\n";
  ptr +="  float: left;\n";
  ptr +="  width: 300px;\n";
  ptr +="  height: 350px;\n";
  ptr +="  background-color: #fff;\n";
  ptr +="  color: #000;\n";
  ptr +="  padding: 0px;\n";
  ptr +="  z-index: 1;\n";
  ptr +="}\n";
  ptr +="\n";
  ptr +="div.control-values {\n";
  ptr +="  float: left;\n";
  ptr +="  text-align: left;\n";
  ptr +="  width: 450px;\n";
  ptr +="  height: 350px;\n";
  ptr +="  background-color: #fff;\n";
  ptr +="  color: #000;\n";
  ptr +="  padding: 0px;\n";
  ptr +="  z-index: 1;\n";
  ptr +="}\n";
  ptr +="\n";
  ptr +="div.automatic-values {\n";
  ptr +="  float: left;\n";
  ptr +="  text-align: left;\n";
  ptr +="  width: 720px;\n";
  ptr +="  height: 200px;\n";
  ptr +="  background-color: #fff;\n";
  ptr +="  color: #000;\n";
  ptr +="  padding: 0px 15px;\n";
  ptr +="  z-index: 1;\n";
  ptr +="}\n";
  ptr +="\n";
  ptr +="div.footer {\n";
  ptr +="  clear: left;\n";
  ptr +="  display: block;\n";
  ptr +="  width: 1016px;\n";
  ptr +="  padding: 15px 0px;\n";
  ptr +="}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="\n";
  ptr +="<div class=frame>\n";
  ptr +="  <div class=frame-center>\n";
  ptr +="    <div class=header>\n";
  ptr +="      <h1>SYN Shop Traffic Light</h1>\n";
  ptr +="      <h3>hacking all the things since 2011</h3>\n";
  ptr +="  </div>\n";
  ptr +=" <div class=content>\n";
  ptr +="      <div class=content-block>\n";
  ptr +="        <div class=content-block-header>\n";
  ptr +="          Manual Light Control\n";
  ptr +="        </div>\n";
  ptr +="        <div class=content-block-body>\n";
  ptr +="          <div class=control-buttons>\n";
  
  if(red_lightstat)
  {ptr +="<p>red_light Status: ON</p><a class=\"button button-off\" href=\"/red_light_off\">OFF</a>\n";}
  else
  {ptr +="<p>red_light Status: OFF</p><a class=\"button button-on\" href=\"/red_light_on\">ON</a>\n";}

  if(yellow_lightstat)
  {ptr +="<p>yellow_light Status: ON</p><a class=\"button button-off\" href=\"/yellow_light_off\">OFF</a>\n";}
  else
  {ptr +="<p>yellow_light Status: OFF</p><a class=\"button button-on\" href=\"/yellow_light_on\">ON</a>\n";}

  if(green_lightstat)
  {ptr +="<p>green_light Status: ON</p><a class=\"button button-off\" href=\"/green_light_off\">OFF</a>\n";}
  else
  {ptr +="<p>green_light Status: OFF</p><a class=\"button button-on\" href=\"/green_light_on\">ON</a>\n";}

  ptr +="          </div>\n";
  ptr +="          <div class=control-values>\n";
  ptr +="            <p>Manual Light Time in Minutes</p>\n";
  ptr +="      <form name=manual action=\"/set\" method=get>\n";
  ptr +="     <input name=manual_time value=" + String(manual_time) + "><input type=submit value=SET>\n";
  ptr +="     </form>\n";
  ptr +="          </div>\n";
  ptr +="        </div>\n";
  ptr +="      </div>\n";
  ptr +="      <div class=content-block>\n";
  ptr +="        <div class=content-block-header>\n";
  ptr +="          Automatic Light Control\n";
  ptr +="        </div>\n";
  ptr +="        <div class=content-block-body>\n";
  ptr +="          <div class=automatic-values>\n";
  ptr +="            <p>Current Time " + formattedDate + "</p>\n";
  ptr +="            <p>Open Shop for Time in Minutes</p>\n";
  ptr +="     <form name=automatic action=\"/set\" method=get>\n";
  ptr +="     <input name=on_time value=" + String(on_time) + "><input type=submit value=SET>\n";
  ptr +="     </form>\n";
  ptr +="            <p>Set Light to go Yellow in number of Minutes before closing</p>\n";
  ptr +="     <form name=automatic action=\"/set\" method=get>\n";
  ptr +="     <input name=yellow_time value=" + String(yellow_time) + "><input type=submit value=SET>\n";
  ptr +="     </form>\n";
  ptr +="          </div>\n";
  ptr +="        </div>\n";
  ptr +="      </div>\n";
  ptr +="      <div class=content-block>\n";
  ptr +="      </div>\n";
  ptr +="    </div>\n";
  ptr +="    <div class=footer>\n";
  ptr +="      Amazing Footer here\n";
  ptr +="    </div>\n";
  ptr +="  </div>\n";
  ptr +="</div>\n";
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}

String SendJSON(){
  /*
   * {
   *   "status": "green",
   *   "destination_time": 1579820935
   * }
   */
  
  String ptr = "{\n";
  ptr +="\"status\": \"";

  if(current_time > on_set_time)
  {
    ptr += "red";
  }
  else if(current_time > on_set_time - (yellow_time * 60))
  {
    ptr += "yellow";
  }
  else if(current_time <= on_set_time)
  {
    ptr += "green";
  }

  ptr +="\",\n";
  ptr +="\"destination_time\": " + String(destination_time - time_offset) + "\n";
  ptr +="}\n";  return ptr;
}
