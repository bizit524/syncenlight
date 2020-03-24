// syncenlight version by tueftla, based on the Netzbasteln version

#include <FS.h>                   // File system, this needs to be first.
#include <ESP8266WiFi.h>          // ESP8266 Core WiFi Library
#include <DNSServer.h>            // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     // Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <Adafruit_NeoPixel.h>    // LED
#include <PubSubClient.h>         // MQTT client
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson
#include <Ticker.h>
#include <CapacitiveSensor.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

//---------------------------------------------------------
// Pins and configuration
#define SEND_PIN 4 // D2
#define RECEIVE_PIN 14 // D5
#define SENSOR_THRESHOLD 500
#define PIXEL_PIN 12 // D6
#define PIXEL_COUNT 16
#define LOOP_PERIOD 50

// Defaults
char mqttServer[40] = "farmer.cloudmqtt.com";
char mqttPort[40] = "16215";
char mqttUser[40] = "gbzpbcfr";
char mqttPassword[40] = "FHCs0mYaflrx";

unsigned int brightness = 255; // 0-255
unsigned int timeoutbrightness = 255;
bool lastSensorState = false;
long addtimer = 0;
//---------------------------------------------------------
bool shouldSaveConfig = false;
void save_config_callback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

WiFiManager wifiManager;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
CapacitiveSensor sensor = CapacitiveSensor(SEND_PIN, RECEIVE_PIN);
Adafruit_NeoPixel leds = Adafruit_NeoPixel(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800); // NEO_RGBW for Wemos Mini LED Modules, NEO_GRB for most Stripes 

uint16_t hue = 0; // 0-359
extern const uint8_t gamma8[];

Ticker swooshTicker;
unsigned int swooshTime;
uint16_t swooshHue = 240; // blue swoosh

String chipId = String(ESP.getChipId(), HEX);
char chipIdCharArr[7];

void setup() {
  // Initialize debug output
  Serial.begin(9600);
  
  // Initialize LEDs and swoosh animation (played during startup and configuration)
  leds.begin();
  leds.setBrightness(brightness);
  swooshTime = 0;
  swooshTicker.attach_ms(10, update_swoosh);
  
  // Read out chip ID and construct SSID for hotspot
  chipId.toUpperCase();
  chipId.toCharArray(chipIdCharArr, 7);
  String ssid = "SYNCENLIGHT-" + chipId;
  int ssidCharArrSize = ssid.length() + 1;
  char ssidCharArr[ssidCharArrSize];
  ssid.toCharArray(ssidCharArr, ssidCharArrSize);

  Serial.println("Hi!");
  Serial.print("Chip ID: ");
  Serial.println(chipId);

  // Read configuration from FS json.
  Serial.println("Mounting FS ...");
  // Clean FS, for testing
  //SPIFFS.format();
  if (SPIFFS.begin()) {
    Serial.println("Mounted file system.");
    if (SPIFFS.exists("/config.json")) {
      // File exists, reading and loading.
      Serial.println("Reading config file.");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("Opened config file.");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nParsed json.");
          strcpy(mqttServer, json["mqtt_server"]);
          strcpy(mqttPort, json["mqtt_port"]);
          strcpy(mqttUser, json["mqtt_user"]);
          strcpy(mqttPassword, json["mqtt_password"]);
        } else {
          Serial.println("Failed to load json config.");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("Failed to mount FS.");
  }
  //end read

  // The extra parameters to be configured.
  WiFiManagerParameter customMqttServer("Server", "MQTT Server", mqttServer, 40);
  WiFiManagerParameter customMqttPort("Port", "MQTT Port", mqttPort, 40);
  WiFiManagerParameter customMqttUser("User", "MQTT User", mqttUser, 40);
  WiFiManagerParameter customMqttPassword("Password", "MQTT Password", mqttPassword, 40);
  
  
  wifiManager.setSaveConfigCallback(save_config_callback);
  // Add all parameters.
  wifiManager.addParameter(&customMqttServer);
  wifiManager.addParameter(&customMqttPort);
  wifiManager.addParameter(&customMqttUser);
  wifiManager.addParameter(&customMqttPassword);

  wifiManager.autoConnect(ssidCharArr);

  // We are connected.
  Serial.println("WiFi Connected.");

  // Read updated parameters.
  strcpy(mqttServer, customMqttServer.getValue());
  strcpy(mqttPort, customMqttPort.getValue());
  strcpy(mqttUser, customMqttUser.getValue());
  strcpy(mqttPassword, customMqttPassword.getValue());

  // Save the custom parameters to FS.
  if (shouldSaveConfig) {
    Serial.println("Saving config.");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqttServer;
    json["mqtt_port"] = mqttPort;
    json["mqtt_user"] = mqttUser;
    json["mqtt_password"] = mqttPassword;
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("Failed to open config file for writing.");
    }
    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
  }
  // End save.

  // Start MQTT client.
  String s = String((char*)mqttPort);
  unsigned int p = s.toInt();
  mqttClient.setServer(mqttServer, p);
  mqttClient.setCallback(mqtt_callback);
  Serial.println("MQTT client started.");
  
  swooshTicker.detach();
}


void loop() {
  long startTime = millis();
  
  // Read capacitive sensor, if touched change color
  long sensorValue;
  sensorValue = sensor.capacitiveSensor(80);
 //after 30 minutes start lowering brightness of leds
  if (addtimer > 1000)
  {
    
    if (timeoutbrightness > 0)
    {
      //test 40000
      //addtimer = 41140;
      addtimer = 800;
      timeoutbrightness = timeoutbrightness - 17;
      leds.setBrightness(timeoutbrightness);
      Serial.println("timeout brightness: "+timeoutbrightness);
      //1430 - 1 min
      Serial.println("30 minutes");
      update_led();
    }
    
  }
  addtimer = addtimer + (millis()-startTime);
  Serial.println(addtimer);
  if (sensorValue > SENSOR_THRESHOLD) {
    //hue = hue + 3;
    //hue = (hue + 3) % 360;
    rainbow(15);
    hue = random(0, 255);
    update_led();
    leds.setBrightness(brightness);
    addtimer = 0;
    timeoutbrightness = 255;
    char payload[1];
    itoa(hue, payload, 10);
    mqttClient.publish("synclight", payload, true);
    
    Serial.print("New color: ");
    Serial.println(hue);
  } else {
    //if (last_sensor_state == true) {
    //  char payload[1];
    //  itoa(hue, payload, 10);
    //  mqttClient.publish("synclight", payload, true);
    //}
  }

  // For determining first loop after touch is released
  if (sensorValue > SENSOR_THRESHOLD) {
    lastSensorState = true;
  } else {
    lastSensorState = false;
  }

  // If not connected anymore try to reconnect
  if (!mqttClient.connected()) {
    swooshTicker.attach_ms(10, update_swoosh);
    mqtt_reconnect();
  }

  // Necessary to keep up MQTT connection
  mqttClient.loop();
  
  // Debug output
  Serial.print("Sensor value: ");
  Serial.print(sensorValue);
  Serial.print("\t");
  Serial.print("Processing time in loop: ");
  Serial.print(millis() - startTime);
  Serial.print("\n");
  
  int delayValue = LOOP_PERIOD - (millis() - startTime);
  if (delayValue > 0) {
    delay(delayValue);
  }
}


void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.print(" (length ");
  Serial.print(length);
  Serial.print(")");
  Serial.println();

  // Update color of LEDs
  if (length <= 3) {
    payload[length] = '\0';
    String s = String((char*)payload);
    unsigned int newHue = s.toInt();
    if (newHue >= 0 && newHue < 360) {
      hue = newHue;
      addtimer = 0;
      timeoutbrightness = 255;
      leds.setBrightness(brightness);
      update_led();
    }
  }
}

void mqtt_reconnect() {
  while (!mqttClient.connected()) {
    Serial.println("Connecting MQTT...");
    if (mqttClient.connect(chipIdCharArr, mqttUser, mqttPassword)) {
      swooshTicker.detach();
      Serial.println("MQTT connected.");
      mqttClient.subscribe("synclight", 1); // QoS level 1
    }
    else {
      Serial.print("Error, rc=");
      Serial.print(mqttClient.state());
      delay(5000);
    }
  }
}

void update_led() {
  uint32_t color = hsv_to_rgb(hue, 255, 255);
  for (uint16_t i=0; i < PIXEL_COUNT; i++) {
    leds.setPixelColor(i, color);
  }
  leds.show();
}

void update_swoosh() {
  swooshTime = swooshTime + 10;

  int value = (int) 127.5 * sin(2*3.14/1000 * swooshTime) + 127.5;
  for (int i = 0; i < PIXEL_COUNT; i++) {
    leds.setPixelColor(i, hsv_to_rgb(swooshHue, 255, value));
  }
  leds.show();
}


// hue: 0-359, sat: 0-255, val (lightness): 0-255
uint32_t hsv_to_rgb(unsigned int hue, unsigned int sat, unsigned int val) {
  int r, g, b, base;
  if (sat == 0) {
    r = g = b = val;
  }
  else {
    base = ((255 - sat) * val) >> 8;
    switch (hue / 60) {
      case 0:
        r = val;
        g = (((val - base) * hue) / 60) + base;
        b = base;
        break;
      case 1:
        r = (((val - base) * (60 - (hue % 60))) / 60) + base;
        g = val;
        b = base;
        break;
      case 2:
        r = base;
        g = val;
        b = (((val - base) * (hue % 60)) / 60) + base;
        break;
      case 3:
        r = base;
        g = (((val - base) * (60 - (hue % 60))) / 60) + base;
        b = val;
        break;
      case 4:
        r = (((val - base) * (hue % 60)) / 60) + base;
        g = base;
        b = val;
        break;
      case 5:
        r = val;
        g = base;
        b = (((val - base) * (60 - (hue % 60))) / 60) + base;
        break;
    }
  }
  
  return leds.Color(
    pgm_read_byte(&gamma8[r]),
    pgm_read_byte(&gamma8[g]),
    pgm_read_byte(&gamma8[b]));
}




// Gamma correction curve
// https://learn.adafruit.com/led-tricks-gamma-correction/the-quick-fix
const uint8_t PROGMEM gamma8[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255
};

uint32_t Wheel(byte WheelPos) {
  if(WheelPos < 85) {
    return leds.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } 
  else if(WheelPos < 170) {
    WheelPos -= 85;
    return leds.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } 
  else {
    WheelPos -= 170;
    return leds.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}

void rainbow(uint8_t wait) {
  uint16_t i, j;

  for(j=0; j<256; j++) {
    for(i=0; i<leds.numPixels(); i++) {
      leds.setPixelColor(i, Wheel((i*1+j) & 255));
    }
    leds.show();
    delay(wait);
  }}
  
