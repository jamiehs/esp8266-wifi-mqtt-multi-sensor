// Flash with 4M (1M SPIFFS)

#include <FS.h>
#include <PubSubClient.h>        // https://github.com/knolleary/pubsubclient
#include <ESP8266WiFi.h>         // https://github.com/esp8266/Arduino
#include <DNSServer.h>           // https://github.com/esp8266/Arduino/tree/master/libraries/DNSServer
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>         // https://github.com/bblanchon/ArduinoJson
#include <SimpleDHT.h>           // https://github.com/winlinvip/SimpleDHT

#define MOTION_PIN 14
#define DHT11_PIN 12

char device_slug[40];
char mqtt_server[40];
char mqtt_port[6] = "1883";
char mqtt_user[40];
char mqtt_password[40];
String mac = WiFi.macAddress();
String topic = "motion";
boolean has_motion = false;
byte temperature = 0;
byte humidity = 0;

unsigned long previousDHT11Poll = 0;
const long DHT11Frequency = 60000;

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


WiFiClient espClient;
PubSubClient mqttClient(espClient);

void reconnect() {
  // Loop until we're reconnected
  while(!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    // if (mqttClient.connect("ESP8266Client")) {
    if (mqttClient.connect(mac.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("connected");
      // Subscribe to our globally defined topic
      mqttClient.subscribe(topic.c_str());
      // Say Hello
      mqttClient.publish(String(topic + "/status").c_str(), String("BOOTED").c_str(), true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// DHT11 Temperature Sensor
SimpleDHT11 dht11;

void setup() {
  pinMode(MOTION_PIN, INPUT);

  Serial.begin(9600);
  Serial.println();

  //clean FS, for testing
  // SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");
  Serial.print("MAC Address: ");
  Serial.println(mac);

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(device_slug, json["device_slug"]);
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_password, json["mqtt_password"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read


  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_device_slug("device_slug", "Device Slug", device_slug, 40);
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT User", mqtt_user, 40);
  WiFiManagerParameter custom_mqtt_password("password", "MQTT Password", mqtt_password, 40);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_device_slug);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);

  //reset settings - for testing
  // wifiManager.resetSettings();

  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(device_slug, custom_device_slug.getValue());
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  topic = topic + "/" + String(device_slug);

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["device_slug"] = device_slug;
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_password"] = mqtt_password;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());


  // MQTT Setup & Callback
  mqttClient.setServer(mqtt_server, atol(mqtt_port));

}

void loop() {
  unsigned long currentMillis = millis();

  if (!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop();

  if (digitalRead(MOTION_PIN)) {
    Serial.println("Motion.........................................");
    if (!has_motion) {
      has_motion = true;
      statusReport("ON");
    }
  } else {
    Serial.println("No Motion");
    if (has_motion) {
      has_motion = false;
      statusReport("OFF");
    }
  }


  if (currentMillis - previousDHT11Poll >= DHT11Frequency) {
    previousDHT11Poll = currentMillis;

    int err = SimpleDHTErrSuccess;
    if ((err = dht11.read(DHT11_PIN, &temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
      Serial.print("Read DHT11 failed, err="); Serial.print(err);
      return;
    }

    Serial.print("Sample OK: ");
    Serial.print((int)temperature); Serial.println(" *C, ");
    Serial.print(((int)temperature * 1.8) + 32); Serial.println(" *F, ");
    Serial.print((int)humidity); Serial.println(" %");
    Serial.println(" ");

    mqttClient.publish(String(topic + "/tempC").c_str(), String((int)temperature).c_str(), true);
    mqttClient.publish(String(topic + "/tempF").c_str(), String((int)(temperature * 1.8) + 32).c_str(), true);
    mqttClient.publish(String(topic + "/humidity_percent").c_str(), String((int)humidity).c_str(), true);
  }

  delay(80);
}

void statusReport(String status) {
  mqttClient.publish(String(topic + "/status").c_str(), status.c_str(), true);
}


