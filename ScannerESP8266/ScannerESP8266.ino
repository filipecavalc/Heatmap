#ifdef ESP32
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#else
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <time.h>

ESP8266WiFiMulti wifiMulti;
#endif

#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>

const char* ssid     = "Nome da Rede";
const char* password = "Senha";

// Uncomment to set to learn mode
//#define MODE_LEARNING 1
#define LOCATION "LOCATIZAÇÃO do dispositivo usado somente quando ativo o MODE_LEARNING"

#define GROUP_NAME "NOME DA FAMILIA"

// Important! BLE + WiFi Support does not fit in standard partition table.
// Manual experimental changes are needed.
// See https://desire.giesecke.tk/index.php/2018/01/30/change-partition-size/
//#define USE_BLE 1
#define BLE_SCANTIME 5

#ifdef USE_BLE
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      // Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
    }
};
#endif

// Automagically disable BLE on ESP8266
#if defined(ESP8266)
#undef USE_BLE
#endif

// Enable Deepsleep for power saving. This will make it run less often.
// #define USE_DEEPSLEEP 1

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */

// 20 currently results in an interval of 45s
#define TIME_TO_SLEEP  20        /* Time ESP32 will go to sleep (in seconds) */

#ifdef ESP32
RTC_DATA_ATTR int bootCount = 0;
#endif

const char* host = "cloud.internalpositioning.com";
const char* ntpServer = "pool.ntp.org";

#define DEBUG 1

String chipIdStr;

/*
Method to print the reason by which ESP32
has been awaken from sleep
*/
void print_wakeup_reason(){
  #ifdef ESP32
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();
  ++bootCount;
  Serial.println("[ INFO ]\tBoot number: " + String(bootCount));

  switch(wakeup_reason)
  {
    case 1  : Serial.println("[ INFO ]\tWakeup caused by external signal using RTC_IO"); break;
    case 2  : Serial.println("[ INFO ]\tWakeup caused by external signal using RTC_CNTL"); break;
    case 3  : Serial.println("[ INFO ]\tWakeup caused by timer"); break;
    case 4  : Serial.println("[ INFO ]\tWakeup caused by touchpad"); break;
    case 5  : Serial.println("[ INFO ]\tWakeup caused by ULP program"); break;
    default : Serial.println("[ INFO ]\tWakeup was not caused by deep sleep"); break;
  }
  #endif
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  #ifdef USE_BLE
  Serial.println("Find3 ESP client by DatanoiseTV (WiFi + BLE support.)");
  #else
  Serial.println("Find3 ESP client by DatanoiseTV (WiFi support WITHOUT BLE.)");
  #endif

  print_wakeup_reason();
  #ifdef ESP32
  chipIdStr = String((uint32_t)(ESP.getEfuseMac()>>16));
  #else
  chipIdStr = String(ESP.getChipId());
  #endif

  Serial.print("[ INFO ]\tChipID is: ");
  Serial.println(chipIdStr);

  //wifiMulti.addAP(ssid, password);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println("[ INFO ]\tConnecting to WiFi..");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[ INFO ]\tWiFi connection established.");
    Serial.print("[ INFO ]\tIP address: ");
    Serial.println(WiFi.localIP());
    configTime(0, 0, ntpServer);
  } else {
    Serial.print("[ INFO ]Connection fail. ");
  }
}

unsigned long long getUnixTime() {
  #ifdef ESP32
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("[ ERROR ]\tFailed to obtain time via NTP. Retrying.");
    getUnixTime();
  }
  else
  {
    Serial.println("[ INFO ]\tSuccessfully obtained time via NTP.");
  }
  time(&now);
  unsigned long long uTime = (uintmax_t)now;
  return uTime * 1000UL;
  #else
  return 123456;
  #endif
}

void SubmitWiFi(void)
{
  String request;
  uint64_t chipid;

  DynamicJsonBuffer jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();
  root["d"] = chipIdStr;
  root["f"] = GROUP_NAME;
  JsonObject& data = root.createNestedObject("s");

  Serial.println("[ INFO ]\tWiFi scan starting..");
  int n = WiFi.scanNetworks(false, true);
  Serial.println("[ INFO ]\tWiFi Scan finished.");
  if (n == 0) {
    Serial.println("[ ERROR ]\tNo networks found");
  } else {
    Serial.print("[ INFO ]\t");
    Serial.print(n);
    Serial.println(" WiFi networks found.");
    JsonObject& wifi_network = data.createNestedObject("wifi");
    for (int i = 0; i < n; ++i) {
      wifi_network[WiFi.BSSIDstr(i)] = WiFi.RSSI(i);
    }

    #ifdef USE_BLE
    Serial.println("[ INFO ]\tBLE scan starting..");
    BLEDevice::init("");
    BLEScan* pBLEScan = BLEDevice::getScan(); // create new scan
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true); // active scan uses more power, but get results faster
    BLEScanResults foundDevices = pBLEScan->start(BLE_SCANTIME);

    Serial.print("[ INFO ]\t");
    Serial.print(foundDevices.getCount());
    Serial.println(" BLE devices found.");

    JsonObject& bt_network = data.createNestedObject("bluetooth");
    for(int i=0; i<foundDevices.getCount(); i++)
    {
      std::string mac = foundDevices.getDevice(i).getAddress().toString();
      bt_network[(String)mac.c_str()] = (int)foundDevices.getDevice(i).getRSSI();
    }
    #else
    Serial.println("[ INFO ]\tBLE scan skipped (BLE disabled)..");
    #endif // USE_BLE

    #ifdef MODE_LEARNING
      root["l"] = LOCATION;
    #endif

    root.printTo(request);

    #ifdef DEBUG
    Serial.println(request);
    #endif

    WiFiClientSecure client;
    const int httpsPort = 443;
    if (!client.connect(host, httpsPort)) {
      Serial.println("connection failed");
    }

    // We now create a URI for the request
    String url = "/data";

    Serial.print("[ INFO ]\tRequesting URL: ");
    Serial.println(url);

    // This will send the request to the server
    client.print(String("POST ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Content-Type: application/json\r\n" +
                 "Content-Length: " + request.length() + "\r\n\r\n" +
                 request +
                 "\r\n\r\n"
                );

    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 2500) {
        Serial.println("[ ERROR ]\tHTTP Client Timeout !");
        client.stop();
        return;
      }
    }

    // Check HTTP status
    char status[60] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
      Serial.print(F("[ ERROR ]\tUnexpected Response: "));
      Serial.println(status);
      return;
    }
    else
    {
      Serial.println(F("[ INFO ]\tGot a 200 OK."));
    }

   char endOfHeaders[] = "\r\n\r\n";
   if (!client.find(endOfHeaders)) {
    Serial.println(F("[ ERROR ]\t Invalid Response"));
    return;
   }
   else
   {
    Serial.println("[ INFO ]\tLooks like a valid response.");
   }

   Serial.println("[ INFO ]\tClosing connection.");
   Serial.println("=============================================================");

   #ifdef USE_DEEPSLEEP
   esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
   esp_deep_sleep_start();
   #endif
  }
}

void loop() {
  SubmitWiFi();
  yield();
}
