/*
  Datalogger MQTT client - SubTopics

  Connects to an MQTT broker and uploads data.
  Uses realtime clock on the SAMD21 (MKR boards and Nano 33 IoT) to
  keep time. Breaks the sensor readings into separate
  MQTT subtopics.

  Works with MKR1010, MKR1000, Nano 33 IoT
  Uses the following libraries:
   http://librarymanager/All#WiFi101  // use this for MKR1000
   http://librarymanager/All#WiFiNINA  // use this for MKR1010, Nano 33 IoT
   http://librarymanager/All#ArduinoMqttClient
   http://librarymanager/All#Arduino_JSON
   http://librarymanager/All#RTCZero
   http://librarymanager/All#Adafruit_TCS34725 (for the sensor)

    created 13 Jun 2021
  by Tom Igoe
*/
// include required libraries and config files
#include <SPI.h>
#include <WiFi101.h>        // for MKR1000 modules
//#include <WiFiNINA.h>         // for MKR1010 modules and Nano 33 IoT modules
// realtime clock module on the SAMD21 processor:
#include <RTCZero.h>
#include <ArduinoMqttClient.h>
// I2C and light sensor libraries:
#include <Wire.h>
#include <Adafruit_TCS34725.h>
// network names and passwords:
#include "arduino_secrets.h"

// initialize WiFi connection using SSL
// (use WIFiClient and port number 1883 for unencrypted connections):
WiFiSSLClient wifi;
MqttClient mqttClient(wifi);
String addressString = "";

// details for MQTT client:
char broker[] = "public.cloud.shiftr.io";
int port = 8883;
char topic[] = "light-readings";
const char willTopic[] = "light-readings/will";
String clientID = "light-client-";
const char location[] = "home";

// initialize RTC:
RTCZero rtc;
unsigned long startTime = 0;

// timestamp for the sensor reading and send:
long lastSendTime = 0;

// interval between requests, in minutes:
int sendInterval = 1;
// time before broker should release the will, in ms:
long int keepAliveInterval =  sendInterval * 2 * 60 * 1000;
// initialize the light sensor:
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_614MS, TCS34725_GAIN_1X);
// number of successful readings that have been sent:
unsigned long readingCount = 0;

void setup() {
  Serial.begin(9600);              // initialize serial communication
  // if serial monitor is not open, wait 3 seconds:
  if (!Serial) delay(3000);
  // start the realtime clock:
  rtc.begin();
  // array for WiFi MAC address:
  byte mac[6];
  WiFi.macAddress(mac);
  for (int i = 5; i >= 0; i--) {
    if (mac[i] < 16) {
      // if the byte is less than 16, add a 0 placeholder:
      addressString += "0";
    }
    // add the hexadecimal representation of this byte
    // to the address string:
    addressString += String(mac[i], HEX);
  }

  // set the credentials for the MQTT client:
  // set a will message, used by the broker when the connection dies unexpectantly
  // you must know the size of the message before hand, and it must be set before connecting
  String willPayload = "sensor died";
  bool willRetain = true;
  int willQos = 1;
  // add location name to the client:
  clientID += location;
  mqttClient.setId(clientID);
  mqttClient.setUsernamePassword(SECRET_MQTT_USER, SECRET_MQTT_PASS);
  mqttClient.setKeepAliveInterval(keepAliveInterval);
  mqttClient.beginWill(willTopic, willPayload.length(), willRetain, willQos);
  mqttClient.print(willPayload);
  mqttClient.endWill();
}


void loop() {
  //if you disconnected from the network, reconnect:
  if (WiFi.status() != WL_CONNECTED) {
    connectToNetwork();
  }

  // if not connected to the broker, try to connect:
  if (!mqttClient.connected()) {
    Serial.println("reconnecting to broker");
    connectToBroker();
  } else {

    // If the client is not connected:  if (!client.connected()) {
    // and the request interval has passed:
    if (abs(rtc.getMinutes() - lastSendTime) >= sendInterval) {

      // read the sensor
      readSensor();
      readingCount++;
      // take note of the time you make your request:
      lastSendTime = rtc.getMinutes();
    }
  }
}
/*
  readSensor. You could replace this with any sensor, as long as
  you put the results into the body JSON object
*/
void readSensor() {
  // get lux and color temperature from sensor:
  uint16_t r, g, b, c, colorTemp, lux;
  tcs.getRawData(&r, &g, &b, &c);
  colorTemp = tcs.calculateColorTemperature_dn40(r, g, b, c);
  lux = tcs.calculateLux(r, g, b);

  // add the MAC address to the sensor as an ID:
  sendMessage("uid", addressString);
  // add the location:
  sendMessage("location", String(location));

  // update elements of request body JSON object:
  sendMessage("timeStamp", String(rtc.getEpoch()));
  sendMessage("lux", String(lux));
  sendMessage("ct", String(colorTemp));
  sendMessage("uptime", String(rtc.getEpoch() - startTime));
  sendMessage("readingCount", String(readingCount));
}

void sendMessage(String subTopic, String val) {
  String thisTopic = String(topic) + "/";
  thisTopic += subTopic;
  mqttClient.beginMessage(thisTopic);
  mqttClient.print(val);
  mqttClient.endMessage();
}

void connectToNetwork() {
  // try to connect to the network:
  while ( WiFi.status() != WL_CONNECTED) {
    Serial.println("Attempting to connect to: " + String(SECRET_SSID));
    //Connect to WPA / WPA2 network:
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    delay(2000);
  }
  Serial.println("connected.");

  // set the time from the network:
  unsigned long epoch;
  do {
    Serial.println("Attempting to get network time");
    epoch = WiFi.getTime();
    delay(2000);
  } while (epoch == 0);

  // set the RTC:
  rtc.setEpoch(epoch);
  if (startTime == 0) startTime = rtc.getEpoch();
  IPAddress ip = WiFi.localIP();
  Serial.print(ip);
  Serial.print("  Signal Strength: ");
  Serial.println(WiFi.RSSI());
}

boolean connectToBroker() {
  // if the MQTT client is not connected:
  if (!mqttClient.connect(broker, port)) {
    // print out the error message:
    Serial.print("MOTT connection failed. Error no: ");
    Serial.println(mqttClient.connectError());
    // return that you're not connected:
    return false;
  }
  // once you're connected, you can proceed:
  mqttClient.subscribe(topic);
  // return that you're connected:
  return true;
}
