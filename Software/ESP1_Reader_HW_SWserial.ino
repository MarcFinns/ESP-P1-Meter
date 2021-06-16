// MarcFinns 2021
//  Based on https://github.com/matthijskooijman/arduino-dsmr
//
// IMPORTANT: requires ESP 2.7.4 (not working with 3.0.0)
//

//#define SWSERIAL

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <dsmr.h>                 // https://github.com/matthijskooijman/arduino-dsmr
#include <ArduinoJson.h>
#include <ArduinoOTA.h>

#ifdef SWSERIAL
#include <SoftwareSerial.h>
#endif

#ifdef SWSERIAL
#define VERSION "2.0 SW UART"
#else
#define VERSION "2.0 HW UART"
#endif


#define HOSTNAME "ESPP1Meter"
#define MAXMESSAGELENGTH 3000
#define SERIAL_RX  5  // pin for SoftwareSerial RX
#define PORTAL_PIN 0

//===Change values from here===
#define POLLDELAY 5000
#define OTA_PASSWORD "P1MeterPWD"
//===Change values to here===

#define LEDSERIAL 12
#define LEDWEB 13
#define P1ENABLE 4
#define HTTP_REST_PORT 80

ESP8266WebServer httpRestServer(HTTP_REST_PORT);

#ifdef SWSERIAL
SoftwareSerial P1Serial;
#endif

#ifdef SWSERIAL
P1Reader reader(&P1Serial, 4);
#else
P1Reader reader(&Serial, 4);
#endif

DynamicJsonDocument doc(3072);
String jsonOutput = "{unavailable}";
unsigned long last, lastSuccessfulRead, errors = 0;

/**
   Define the data we're interested in, as well as the datastructure to
   hold the parsed data. This list shows all supported fields, remove
   any fields you are not using from the below list to make the parsing
   and printing code smaller.
   Each template argument below results in a field of the same name.
*/

using meterData = ParsedData <
                  /* String */ identification,
                  /* String */ p1_version,
                  /* String */ timestamp,
                  /* String */ equipment_id,
                  /* FixedValue */ energy_delivered_tariff1,
                  /* FixedValue */ energy_delivered_tariff2,
                  /* FixedValue */ energy_returned_tariff1,
                  /* FixedValue */ energy_returned_tariff2,
                  /* String */ electricity_tariff,
                  /* FixedValue */ power_delivered,
                  /* FixedValue */ power_returned,
                  /* FixedValue */ electricity_threshold,
                  /* uint8_t */ electricity_switch_position,
                  /* uint32_t */ electricity_failures,
                  /* uint32_t */ electricity_long_failures,
                  /* String */ electricity_failure_log,
                  /* uint32_t */ electricity_sags_l1,
                  /* uint32_t */ electricity_sags_l2,
                  /* uint32_t */ electricity_sags_l3,
                  /* uint32_t */ electricity_swells_l1,
                  /* uint32_t */ electricity_swells_l2,
                  /* uint32_t */ electricity_swells_l3,
                  /* String */ message_short,
                  /* String */ message_long,
                  /* FixedValue */ voltage_l1,
                  /* FixedValue */ voltage_l2,
                  /* FixedValue */ voltage_l3,
                  /* FixedValue */ current_l1,
                  /* FixedValue */ current_l2,
                  /* FixedValue */ current_l3,
                  /* FixedValue */ power_delivered_l1,
                  /* FixedValue */ power_delivered_l2,
                  /* FixedValue */ power_delivered_l3,
                  /* FixedValue */ power_returned_l1,
                  /* FixedValue */ power_returned_l2,
                  /* FixedValue */ power_returned_l3,
                  /* uint16_t */ gas_device_type,
                  /* String */ gas_equipment_id,
                  /* uint8_t */ gas_valve_position,
                  /* TimestampedFixedValue */ gas_delivered,
                  /* uint16_t */ thermal_device_type,
                  /* String */ thermal_equipment_id,
                  /* uint8_t */ thermal_valve_position,
                  /* TimestampedFixedValue */ thermal_delivered,
                  /* uint16_t */ water_device_type,
                  /* String */ water_equipment_id,
                  /* uint8_t */ water_valve_position,
                  /* TimestampedFixedValue */ water_delivered,
                  /* uint16_t */ slave_device_type,
                  /* String */ slave_equipment_id,
                  /* uint8_t */ slave_valve_position,
                  /* TimestampedFixedValue */ slave_delivered
                  >;



/**
   This illustrates looping over all parsed fields using the
   ParsedData::applyEach method.

   When passed an instance of this Printer object, applyEach will loop
   over each field and call Printer::apply, passing a reference to each
   field in turn. This passes the actual field object, not the field
   value, so each call to Printer::apply will have a differently typed
   parameter.

   For this reason, Printer::apply is a template, resulting in one
   distinct apply method for each field used. This allows looking up
   things like Item::name, which is different for every field type,
   without having to resort to virtual method calls (which result in
   extra storage usage). The tradeoff is here that there is more code
   generated (but due to compiler inlining, it's pretty much the same as
   if you just manually printed all field names and values (with no
   cost at all if you don't use the Printer).
*/



struct jsonPrinter
{
  template<typename Item>

  void apply(Item &i)
  {
    if (i.present())
    {
      doc[Item::name]["value"] = String(i.val());
      doc[Item::name]["unit"] = String(Item::unit());

    }
  }
};


void setup()
{

  // Init GPIOs
  pinMode(LEDSERIAL, OUTPUT);
  pinMode(LEDWEB, OUTPUT);
  pinMode(P1ENABLE, OUTPUT);
  pinMode(PORTAL_PIN, INPUT);

  // Disable P1 Stream
  digitalWrite(P1ENABLE, LOW);

  // Turn ON LEDs
  digitalWrite(LEDSERIAL, LOW);
  digitalWrite(LEDWEB, LOW);

  // Init serial ports
  Serial.setRxBufferSize(2048);
  Serial.begin(115200);

#ifdef SWSERIAL
  P1Serial.begin(115200, SWSERIAL_8N1, SERIAL_RX, -1, false, 640);
#endif

#ifdef SWSERIAL
  Serial.println("\n\r\n\rESP1 Bridge");
  Serial.println(String(VERSION) + ", BUILT " + String(__DATE__ " " __TIME__));
  SWSERIAL Serial.println("Booting.......");
#endif

  // WiFi connect
  WiFiManager wifiManager;
  wifiManager.setDebugOutput(false);
  wifiManager.setTimeout(180);
  wifiManager.autoConnect(HOSTNAME);
  if (WiFi.status() != WL_CONNECTED)
  {
    //reset and try again...
    ESP.reset();
    delay(5000);
  }

  // Activate mDNS this is used to be able to connect to the server
  // with local DNS hostname esp8266.local
  if (MDNS.begin(HOSTNAME))
  {
#ifdef SWSERIAL
    Serial.println("MDNS responder started");
#endif
  }

  // Init web service

  // Set server routing
  restServerRouting();

  // Set not found response
  httpRestServer.onNotFound(handleNotFound);

  // Start web server
  httpRestServer.begin();

#ifdef SWSERIAL
  Serial.println("HTTP server started");
#endif
  // Init OTA

  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(HOSTNAME);

  // No authentication by default
  ArduinoOTA.setPassword((const char *)OTA_PASSWORD);


  ArduinoOTA.onStart([]() {
#ifdef SWSERIAL
    Serial.println("Start");
#endif
  });
  ArduinoOTA.onEnd([]() {
#ifdef SWSERIAL
    Serial.println("\nEnd");
#endif
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
  {
#ifdef SWSERIAL
    Serial.printf("Progress: % u % % \r", (progress / (total / 100)));
#endif
  });
  ArduinoOTA.onError([](ota_error_t error)
  {
#ifdef SWSERIAL
    Serial.printf("Error[ % u]: ", error);
    if (error == OTA_AUTH_ERROR)  Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)  Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)  Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
#endif
  });
  ArduinoOTA.begin();

  // Turn OFF LEDs
  digitalWrite(LEDSERIAL, HIGH);
  digitalWrite(LEDWEB, HIGH);

  // start a read right away
  reader.enable(true);

#ifdef SWSERIAL
  Serial.println("End setup - Free Heap bytes: " + String(ESP.getFreeHeap()));
#endif
}

// Serving P1 readings
void getMeterReadings()
{
  digitalWrite(LEDWEB, LOW);
#ifdef SWSERIAL
  Serial.println("Readings - Serving invoked");
#endif
  httpRestServer.send(200, "text / json", jsonOutput);
  digitalWrite(LEDWEB, HIGH);
#ifdef SWSERIAL
  Serial.println("Readings - Serving done");
#endif
}

// Serving system settings
void getSettings()
{
#ifdef SWSERIAL
  Serial.println("Settings - Serving invoked");
#endif

  DynamicJsonDocument doc(512);

  doc["version"] = String(VERSION) + ", BUILT " + String(__DATE__ " " __TIME__);
  doc["mac"] = WiFi.macAddress();
  doc["ssid"] = WiFi.SSID();
  doc["signalStrengh(dBm)"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["gateway"] = WiFi.gatewayIP().toString();
  doc["netmask"] = WiFi.subnetMask().toString();
  doc["uptime(s)"] = millis() / 1000.0;
  doc["sinceLastRead(s)"] = (millis() - lastSuccessfulRead) / 1000.0;
  doc["parseErrors"] = errors;
  doc["freeHeap(bytes)"] = ESP.getFreeHeap();

  if (httpRestServer.arg("chipInfo") == "true")
  {
    doc["chipId"] = ESP.getChipId();
    doc["flashChipId"] = ESP.getFlashChipId();
    doc["flashChipSize"] = ESP.getFlashChipSize();
    doc["flashChipRealSize"] = ESP.getFlashChipRealSize();
  }

  String buf;
  serializeJsonPretty(doc, buf);
  httpRestServer.send(200, F("application/json"), buf);
#ifdef SWSERIAL
  Serial.println("Settings - Serving done");
#endif
}

void doReboot()
{
#ifdef SWSERIAL
  Serial.println("Reboot - Action invoked");
#endif
  httpRestServer.send(200, F("application/json"), "{rebooting}");
  yield();
  delay(1000);
  ESP.restart();
}

// Define routing
void restServerRouting()
{

  httpRestServer.on("/", HTTP_GET, []()
  {
    httpRestServer.send(200, F("text/html"),
                        String(HOSTNAME) + " " + String(VERSION) + ", BUILT " + String(__DATE__ " " __TIME__) + " (c) MFINI 2021. Accepted commands: settings, readMeter, reboot" );
  });

  httpRestServer.on(F("/readMeter"), HTTP_GET, getMeterReadings);
  httpRestServer.on(F("/settings"), HTTP_GET, getSettings);
  httpRestServer.on(F("/reboot"), HTTP_GET, doReboot);
}

// Manage not found URL
void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += httpRestServer.uri();
  message += "\nMethod: ";
  message += (httpRestServer.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += httpRestServer.args();
  message += "\n";
  for (uint8_t i = 0; i < httpRestServer.args(); i++)
  {
    message += " " + httpRestServer.argName(i) + ": " + httpRestServer.arg(i) + "\n";
  }
  httpRestServer.send(404, "text / plain", message);
}

void loop()
{
  // Handle on-demand configuration portal
  // is configuration portal requested?
  if ( digitalRead(PORTAL_PIN) == LOW )
  {
    WiFiManager wifiManager;
    wifiManager.setDebugOutput(false);
    wifiManager.setTimeout(180);
    WiFi.disconnect(true);

    if (!wifiManager.startConfigPortal(HOSTNAME))
    {
      //reset and try again...
      ESP.reset();
      delay(5000);
    }
  }


  // Allow the reader to check the serial buffer regularly
  reader.loop();
  ArduinoOTA.handle();
  httpRestServer.handleClient();

  // Every POLLDELAY msec, fire off a one-off reading
  unsigned long now = millis();

  if (now - last > POLLDELAY)
  {
#ifdef SWSERIAL
    Serial.println("Refreshing...");
#endif
    reader.enable(true);
    last = now;
    digitalWrite(LEDSERIAL, LOW);
  }

  if (reader.available())
  {
    meterData data;
#ifdef SWSERIAL
    Serial.println("Reader is available");
#endif
    String err;

    if (reader.parse(&data, &err))
    {
      jsonOutput = "";
      doc.clear();

#ifdef SWSERIAL
      Serial.println("Parsing");
#endif
      data.applyEach(jsonPrinter());

      yield();

      serializeJsonPretty(doc, jsonOutput);
#ifdef SWSERIAL
      Serial.println(jsonOutput);
#endif
      lastSuccessfulRead = millis();
#ifdef SWSERIAL
      Serial.println("Finished parsing - Free Heap bytes: " + String(ESP.getFreeHeap()));
#endif
    }
    else
    {
      // Parser error
      errors ++;

#ifdef SWSERIAL
      // Print error
      Serial.println("Parser error");
      Serial.println(err);
#endif
      //jsonOutput = " {unavailable}";
      err = "";
    }
    digitalWrite(LEDSERIAL, HIGH);
  }

}
