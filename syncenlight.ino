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
#define PIXEL_PIN 15 // D8
#define PIXEL_COUNT 4
#define LOOP_PERIOD 50

// Defaults
char mqttServer[40] = "";
char mqttPort[40] = "";
char mqttUser[40] = "";
char mqttPassword[40] = "";

unsigned int brightness = 255; // 0-255


//---------------------------------------------------------
bool shouldSaveConfig = false;
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


WiFiManager wifiManager;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
CapacitiveSensor sensor = CapacitiveSensor(SEND_PIN, RECEIVE_PIN);
Adafruit_NeoPixel leds = Adafruit_NeoPixel(2, PIXEL_PIN, NEO_GRB + NEO_KHZ800); // NEO_RGBW for Wemos Mini LED Modules, NEO_GRB for most Stripes 

uint16_t hue = 0; // 0-359
extern const uint8_t gamma8[];

bool buttonState;
bool lastButtonState;

Ticker swooshTicker;
unsigned int swoowshTime;
uint32_t blinkColor;


String chipId = String(ESP.getChipId(), HEX);
char chip_id_char_arr[7];



void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(".");
  Serial.println(".");
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  swooshTime = 0;
  
  leds.begin();
  leds.setBrightness(brightness);
  blinkColor = leds.Color(10, 10, 10);
  swooshTicker.attach_ms(10, update_swoosh);
  
  chipId.toUpperCase();
  chipId.toCharArray(chip_id_char_arr, 7);
  String ssid = "SYNCENLIGHT-" + chipId;
  int ssid_char_arr_size = ssid.length() + 1;
  char ssid_char_arr[ssid_char_arr_size];
  ssid.toCharArray(ssid_char_arr, ssid_char_arr_size);

  Serial.println("Hi!");
  Serial.print("Version: Syncenlight ");
  Serial.println(VERSION);
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
  
  
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  // Add all parameters.
  wifiManager.addParameter(&customMqttServer);
  wifiManager.addParameter(&customMqttPort);
  wifiManager.addParameter(&customMqttUser);
  wifiManager.addParameter(&customMqttPassword);


  // When button is pressed on start, go into config portal.
  if (digitalRead(BUTTON_PIN) == LOW) {
    blinkTicker.attach(0.1, blinkLed);
    wifiManager.startConfigPortal(ssid_char_arr);
    blinkTicker.attach(1, blinkLed);
  }
  else {
    wifiManager.autoConnect(ssid_char_arr);    
  }

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
  unsigned int p = s.toInt(s);
  mqttClient.setServer(mqttServer, p);
  mqttClient.setCallback(mqtt_callback);
  Serial.println("MQTT client started.");
}


void loop() {
  long startTime = millis();
  
  // Read capacitive sensor, if touched change color
  long sensorValue;
  sensorValue = sensor.capacitiveSensor(80);
  if (sensorValue > SENSOR_THRESHOLD) {
    hue = hue + 1;
    hue = (hue + 1) % 360;
    
    updateLed();
    
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
    last_sensor_state = true;
  } else {
    last_sensor_state = false;
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

  // Update
  if (length <= 3) {
    payload[length] = '\0';
    String s = String((char*)payload);
    unsigned int newHue = s.toInt();
    if (newHue >= 0 && newHue < 360) {
      hue = newHue;
      updateLed();
    }
  }
}


void mqtt_reconnect() {
  while (!mqttClient.connected()) {
    Serial.println("Connecting MQTT...");
    if (mqttClient.connect(chip_id_char_arr)) {
      swooshTicker.detach();
      Serial.println("MQTT connected.");
      mqttClient.subscribe(publish_topic, 1); // QoS level 1
    }
    else {
      Serial.print("Error, rc=");
      Serial.print(mqttClient.state());
      delay(5000);
    }
  }
}


void updateLed() {
  uint32_t color = hsv_to_rgb(hue, 255, 255);
  for (uint16_t i=0; i<leds.numPixels(); i++) {
    leds.setPixelColor(i, color);
  }
  leds.show();
}



void update_shwoosh() {
  swooshTime = swooshTime + 10;

  int value = (int) 127.5 * sin(2*3.14/1000 * swooshTime) + 127.5;
  for (int i = 0; i < PIXEL_COUNT; i++) {
    // hue = 240 is blue
    leds.setPixelColor(i, hsv_to_rgb(240, 255, value));
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
