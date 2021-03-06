#include <FS.h>                   // this needs to be first, or it all crashes and burns...
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>     // https://github.com/Links2004/arduinoWebSockets/releases 
#include <ArduinoJson.h>          // https://arduinojson.org/ or install via Arduino library manager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <DoubleResetDetector.h>  // https://github.com/datacute/DoubleResetDetector/tree/master/src
#include <Ticker.h>               // https://github.com/esp8266/Arduino/tree/master/libraries/Ticker
#include <SimpleTimer.h>
#include <StreamString.h>

#define DRD_TIMEOUT 1
#define DRD_ADDRESS 0
#define HEARTBEAT_INTERVAL 300000 // 5 Minutes

char a_k[37];
char f_dID[25];

uint64_t heartbeatTimestamp = 0;

bool isConnected = false;
bool shouldSaveConfig = false;
bool RelayState = 0;
bool SwitchReset = true;

const int TacSwitch = 10;        // GPIO pin for hardware momentary switch. Pin 0 on Sonoff
const int PIN_LED = 2;           // Onboard LED I/O pin 2 on ESP8266 12-E Module
const int RelayPin = 5;          // GPIO pin for the Switch

SimpleTimer timer;
WebSocketsClient webSocket;
Ticker flipper;
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

void setPowerStateOnServer(String deviceId, String value);

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void turnOn(String deviceId) {
  if (deviceId == (f_dID))
  {
    Serial.print("Light On: ");
    digitalWrite(RelayPin, HIGH);
    setPowerStateOnServer(f_dID, "ON");
    RelayState = 1;
    Serial.println(deviceId);
  }
  else {
    Serial.print("Unknown Device: ");
    Serial.println(deviceId);
  }
}

void turnOff(String deviceId) {
  if (deviceId == (f_dID))
  {
    Serial.print("Light Off: ");
    digitalWrite(RelayPin, LOW);
    setPowerStateOnServer(f_dID, "OFF");
    RelayState = 0;
    Serial.println(deviceId);
  }
  else {
    Serial.print("Unknown Device: ");
    Serial.println(deviceId);
  }
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      isConnected = false;
      Serial.printf("[WSc] Webservice disconnected from sinric.com!\n");
      break;
    case WStype_CONNECTED: {
        isConnected = true;
        Serial.printf("[WSc] Service connected to sinric.com at url: %s\n", payload);
        Serial.printf("Waiting for commands from sinric.com ...\n");
      }
      break;
    case WStype_TEXT: {
        Serial.printf("[WSc] get text: %s\n", payload);
        // Example payloads

        // For Light device type
        // {"deviceId": xxxx, "action": "setPowerState", value: "ON"} // https://developer.amazon.com/docs/device-apis/alexa-powercontroller.html
        // {"deviceId": xxxx, "action": "AdjustBrightness", value: 3} // https://developer.amazon.com/docs/device-apis/alexa-brightnesscontroller.html
        // {"deviceId": xxxx, "action": "setBrightness", value: 42} // https://developer.amazon.com/docs/device-apis/alexa-brightnesscontroller.html
        // {"deviceId": xxxx, "action": "SetColor", value: {"hue": 350.5,  "saturation": 0.7138, "brightness": 0.6501}} // https://developer.amazon.com/docs/device-apis/alexa-colorcontroller.html
        // {"deviceId": xxxx, "action": "DecreaseColorTemperature"} // https://developer.amazon.com/docs/device-apis/alexa-colortemperaturecontroller.html
        // {"deviceId": xxxx, "action": "IncreaseColorTemperature"} // https://developer.amazon.com/docs/device-apis/alexa-colortemperaturecontroller.html
        // {"deviceId": xxxx, "action": "SetColorTemperature", value: 2200} // https://developer.amazon.com/docs/device-apis/alexa-colortemperaturecontroller.html

        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject((char*)payload);
        String deviceId = json ["deviceId"];
        String action = json ["action"];

        if (action == "setPowerState") {
          String value = json ["value"];
          if (value == "ON") {
            turnOn(deviceId);
          } else {
            turnOff(deviceId);
          }
        }
        else if (action == "setBrightness") {

        }
        else if (action == "AdjustBrightness") {

        }
        else if (action == "test") {
          Serial.println("[WSc] received test command from sinric.com");
        }
      }
      break;
    case WStype_BIN:
      Serial.printf("[WSc] get binary length: %u\n", length);
      break;
  }
}

void setPowerStateOnServer(String deviceId, String value) {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["deviceId"] = deviceId;
  root["action"] = "setPowerState";
  root["value"] = value;
  StreamString databuf;
  root.printTo(databuf);

  webSocket.sendTXT(databuf);
}

void ButtonCheck() {
  if (digitalRead(TacSwitch) == LOW && SwitchReset == true) {
    Serial.println("Hardware switch activated");

    if (RelayState) {
      turnOff(f_dID);
    } else {
      turnOn(f_dID);
    }
    SwitchReset = false;
    delay(50);
  }
  else if (digitalRead(TacSwitch)) {
    SwitchReset = true;
  }
}

void flip() {
  int state = digitalRead(PIN_LED);
  digitalWrite(PIN_LED, !state);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  pinMode(RelayPin, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(TacSwitch, INPUT_PULLUP);
  delay(10);
  digitalWrite(RelayPin, LOW);
  digitalWrite(PIN_LED, LOW);

  timer.setInterval(100, ButtonCheck);

  if (drd.detectDoubleReset()) {
    Serial.println("Double Reset Detected");
    WiFiManager wifiManager;
    SPIFFS.format();
    delay(1000);
    wifiManager.resetSettings();
    delay(1000);
    {
      ESP.reset();
      delay(5000);
    }
  }

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(a_k, json["API key"]);
          strcpy(f_dID, json["Device Id"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  // Parameters
  WiFiManagerParameter custom_f_dh("Device");
  WiFiManagerParameter custom_a_kh("Sinric API");
  WiFiManagerParameter custom_a_k("API key:", "Key", a_k, 37);
  WiFiManagerParameter custom_f_dID("Device Id:", "Id", f_dID, 25);

  //WiFiManager
  flipper.attach(0.2, flip);
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_a_kh);
  wifiManager.addParameter(&custom_a_k);
  wifiManager.addParameter(&custom_f_dh);
  wifiManager.addParameter(&custom_f_dID);

  wifiManager.autoConnect("Sinric");
  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  digitalWrite(PIN_LED, HIGH);
  flipper.detach();

  //if you get here you have connected to the WiFi
  Serial.println("You Are Now Connected!!");

  //read updated parameters
  strcpy(a_k, custom_a_k.getValue());
  strcpy(f_dID, custom_f_dID.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["API key"] = a_k;
    json["Device Id"] = f_dID;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("Your Local IP is:");
  Serial.println(WiFi.localIP());

  // server address, port and URL
  webSocket.begin("iot.sinric.com", 80, "/");

  // event handler
  webSocket.onEvent(webSocketEvent);
  webSocket.setAuthorization("apikey", a_k);

  // try again every 5000ms if connection has failed
  webSocket.setReconnectInterval(5000);   // If you see 'class WebSocketsClient' has no member named 'setReconnectInterval' error update arduinoWebSockets
}

void loop() {
  timer.run();
  drd.loop();
  webSocket.loop();

  if (isConnected) {
    uint64_t now = millis();

    // Send heartbeat in order to avoid disconnections during ISP resetting IPs over night. Thanks @MacSass
    if ((now - heartbeatTimestamp) > HEARTBEAT_INTERVAL) {
      heartbeatTimestamp = now;
      webSocket.sendTXT("H");
    }
  }
}
