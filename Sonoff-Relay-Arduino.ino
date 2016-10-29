
/*
   1MB flash sizee
   sonoff header
   1 - vcc 3v3
   2 - rx
   3 - tx
   4 - gnd
   5 - gpio 14
   esp8266 connections
   gpio  0 - button
   gpio 12 - relay
   gpio 13 - green led - active low
   gpio 14 - pin 5 on header
*/

#define SONOFF_BUTTON    0
#define SONOFF_RELAY    12
#define SONOFF_LED      13
#define SONOFF_INPUT    14

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include "FS.h"

#define MQTT_SERVER "192.168.1.3"
#define MQTT_PORT 1883
#define LIGHT_TOPIC_FMT "/light/basement/%s/relay"
char light_topic[45];
#define LIGHT_BASEMENT_TOPIC "/light/basement/relay"
#define LIGHT_ALL_TOPIC "/light/all/relay"
#define ALIVE_TOPIC_FMT "/alive/%s"
char alive_topic[30];
#define ERROR_TOPIC_FMT "/error/%s"
char error_topic[30];
#define BUTTON_TOPIC_FMT "/button/%s"
char button_topic[30];
WiFiClient espClient;
PubSubClient mqtt(espClient);

struct {
  char ssid[20];
  char password[20];
  char hostname[20];
} wifiConfig;

bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    Serial.println("Failed to parse config file");
    return false;
  }

  strncpy(wifiConfig.ssid, json["ssid"], 20);
  strncpy(wifiConfig.password, json["password"], 20);
  strncpy(wifiConfig.hostname, json["hostname"], 20);

  Serial.println("Loaded config:");
  Serial.println(wifiConfig.ssid);
  Serial.println(wifiConfig.password);
  Serial.println(wifiConfig.hostname);

  // Also configure topic names
  sprintf(light_topic, LIGHT_TOPIC_FMT, wifiConfig.hostname);
  sprintf(alive_topic, ALIVE_TOPIC_FMT, wifiConfig.hostname);
  sprintf(error_topic, ERROR_TOPIC_FMT, wifiConfig.hostname);
  sprintf(button_topic, BUTTON_TOPIC_FMT, wifiConfig.hostname);
  
  return true;
}
void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  SPIFFS.begin();
  if (!loadConfig()) {
    // Flash like a madman
    pinMode(SONOFF_LED, OUTPUT);
    while(true) {
      digitalWrite(SONOFF_LED, HIGH);
      delay(200);
      digitalWrite(SONOFF_LED, LOW);
      delay(200);
    }
  }
  SPIFFS.end();
  
  WiFi.mode(WIFI_STA);
  WiFi.hostname(wifiConfig.hostname);
  WiFi.begin(wifiConfig.ssid, wifiConfig.password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA.setHostname(wifiConfig.hostname);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(receiveMessage);

  pinMode(SONOFF_BUTTON, INPUT);
  attachInterrupt(SONOFF_BUTTON, onButtonChange, CHANGE);

  pinMode(SONOFF_LED, OUTPUT);
  pinMode(SONOFF_RELAY, OUTPUT);
  digitalWrite(SONOFF_RELAY, LOW);
}

bool buttonChange = true;
void onButtonChange() {
  buttonChange = true;
}

void loopMQTT() {
  while (!mqtt.connected()) {
    if (mqtt.connect(wifiConfig.hostname, alive_topic, 1, true, "0")) {
      Serial.println("connected to MQTT");
      mqtt.publish(alive_topic, "1", true);
      mqtt.subscribe(light_topic);
      mqtt.subscribe(LIGHT_BASEMENT_TOPIC);
      mqtt.subscribe(LIGHT_ALL_TOPIC);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  mqtt.loop();
}

void receiveMessage(char* topic, byte* payload, unsigned int length) {
  if (strcmp(light_topic, topic) == 0
    || strcmp(LIGHT_BASEMENT_TOPIC, topic) == 0
    || strcmp(LIGHT_ALL_TOPIC, topic) == 0) {
    if (length != 1) {
      mqtt.publish(error_topic, "Invalid payload");
      return;
    }
    if (payload[0]) {
      digitalWrite(SONOFF_RELAY, HIGH);
      digitalWrite(SONOFF_LED, LOW); // Active low
    } else {
      digitalWrite(SONOFF_RELAY, LOW);
      digitalWrite(SONOFF_LED, HIGH);
    }
  }
}

//int buttonState = 0;

void loop() {
  loopMQTT();
  ArduinoOTA.handle();
  if (buttonChange) {
    char buf[2];
    buf[0] = digitalRead(SONOFF_BUTTON);
    buf[1] = '\0';
    mqtt.publish(button_topic, buf);
    buttonChange = false;
  }
}

