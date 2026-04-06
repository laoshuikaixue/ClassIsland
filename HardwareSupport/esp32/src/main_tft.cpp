#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <time.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>  
#include <BLEServer.h>

#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"

bool isBluetoothMode = false;
String blePayload = "";
bool newBleData = false;

class MyServerCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string rxValue = pCharacteristic->getValue();
      if (rxValue.length() > 0) {
        for (int i = 0; i < rxValue.length(); i++) {
          if (rxValue[i] == '\n') {
            newBleData = true;
          } else {
            blePayload += rxValue[i];
          }
        }
      }
    }
};

const char* ssid = "LHZX";
const char* password = "a12345678";
const wifi_power_t wifiOutputPower = WIFI_POWER_8_5dBm;

const char* courseApiUrl = "http://47.116.166.10:5000/api/course";
const char* voiceHubApiUrl = "http://47.116.166.10:5000/api/voicehub";

constexpr int TFT_SCLK_PIN = 4;
constexpr int TFT_MOSI_PIN = 5;
constexpr int TFT_CS_PIN = 10;
constexpr int TFT_DC_PIN = 7;
constexpr int TFT_RST_PIN = 6;
constexpr int BLK_PIN = 8;
constexpr uint8_t TFT_INIT_MODE = INITR_144GREENTAB;
constexpr uint8_t TFT_ROTATION = 0;

Adafruit_ST7735 tft(&SPI, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
GFXcanvas16* canvas = nullptr;
GFXcanvas16* lyricCanvas = nullptr;
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

String currentCourse = "加载中";
String currentTimeStr = "";
String weatherStr = "--";
String fullScheduleStr = "";
String voiceHubStr = "";
bool voiceHubFetched = false;
int currentRemainingMin = -1;

const uint16_t COLOR_BG = 0x0000;
const uint16_t COLOR_FOOTER_BG = 0x0821;
const uint16_t COLOR_CYAN = 0x169D;
const uint16_t COLOR_GREEN = 0x26F0;
const uint16_t COLOR_PURPLE = 0xBE1F;
const uint16_t COLOR_YELLOW = 0xFFE0;
const uint16_t COLOR_BLUE = 0x653F;
const uint16_t COLOR_PINK = 0xF396;
const uint16_t COLOR_GRAY_300 = 0xD69A;
const uint16_t COLOR_GRAY_400 = 0xA514;
const uint16_t COLOR_GRAY_500 = 0x738E;
const uint16_t COLOR_GRAY_800 = 0x2124;
const uint16_t COLOR_WHITE = 0xFFFF;

enum Scenario {
  SCENARIO_LOADING,
  SCENARIO_NO_CLASSES,
  SCENARIO_PRE_CLASS,
  SCENARIO_IN_CLASS,
  SCENARIO_BREAK,
  SCENARIO_END_OF_DAY
};

Scenario currentScenario = SCENARIO_LOADING;
int currentCourseIndex = -1;
int nextCourseIndex = -1;
int progressPercent = 0;

struct Course {
  String name;
  String startTime;
  String endTime;
  int startMin;
  int endMin;
};

const int MAX_COURSES = 20;
Course dailyCourses[MAX_COURSES];
int courseCount = 0;

Course tomorrowCourses[MAX_COURSES];
int tomorrowCourseCount = 0;

float marqueeOffset = 0.0f;
unsigned long lastMarqueeTime = 0;
String marqueeRenderStr = "";
int marqueeTextWidth = 0;

float currentCourseMarqueeOffset = 0.0f;
unsigned long lastCurrentCourseMarqueeTime = 0;

float rainMarqueeOffset = 0.0f;
unsigned long lastRainMarqueeTime = 0;
String rainMarqueeRenderStr = "";
int rainMarqueeTextWidth = 0;

// BLE Heart Rate State
static BLEUUID heartRateServiceUUID("180d");
static BLEUUID heartRateCharUUID("2a37");

bool doConnect = false;
bool bleConnected = false;
bool doScan = false;
BLERemoteCharacteristic* pRemoteCharacteristic;
BLEAdvertisedDevice* myDevice;

int currentHeartRate = 0;
unsigned long lastHeartRateTime = 0;
bool heartBeatState = false;

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    if (length > 1) {
      // The first byte contains flags, 0th bit indicates 8-bit or 16-bit HR format
      if ((pData[0] & 0x01) == 0) {
        currentHeartRate = pData[1];
      } else if (length > 2) {
        currentHeartRate = (pData[2] << 8) | pData[1];
      }
      lastHeartRateTime = millis();
      heartBeatState = !heartBeatState; // Toggle for animation
    }
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    bleConnected = true;
    Serial.println("BLE Heart Rate Sensor Connected");
  }

  void onDisconnect(BLEClient* pclient) {
    bleConnected = false;
    currentHeartRate = 0;
    Serial.println("BLE Heart Rate Sensor Disconnected");
    doScan = true; // Restart scanning
  }
};

bool connectToServer() {
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    BLEClient*  pClient  = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());

    if (!pClient->connect(myDevice)) return false;
    
    BLERemoteService* pRemoteService = pClient->getService(heartRateServiceUUID);
    if (pRemoteService == nullptr) {
      pClient->disconnect();
      return false;
    }

    pRemoteCharacteristic = pRemoteService->getCharacteristic(heartRateCharUUID);
    if (pRemoteCharacteristic == nullptr) {
      pClient->disconnect();
      return false;
    }

    if(pRemoteCharacteristic->canNotify()) {
      pRemoteCharacteristic->registerForNotify(notifyCallback);
    }
    return true;
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(heartRateServiceUUID)) {
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = false;
    }
  }
};

unsigned long lastCourseFetchTime = 0;
unsigned long lastDisplayTime = 0;
unsigned long lastScheduleScrollTime = 0;

int timeStringToMinutes(String t) {
  int colonIndex = t.indexOf(':');
  if (colonIndex > 0) {
    int h = t.substring(0, colonIndex).toInt();
    int m = t.substring(colonIndex + 1).toInt();
    return h * 60 + m;
  }
  return 0;
}

SemaphoreHandle_t dataMutex = NULL;
bool voiceHubFetching = false;

void fetchCourseDataTask(void *pvParameters);
void fetchVoiceHubDataTask(void *pvParameters);
void updateDisplay();
void updateCurrentCourse(int currentMin);
String buildFooterText();
String fitTextToWidth(const String& text, int maxWidth);

void drawBootText(const String& line1, const String& line2, const String& line3, const String& line4, const String& line5 = "", const String& line6 = "") {
  if (canvas == nullptr) return;
  canvas->fillScreen(COLOR_BG);
  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2Fonts.setForegroundColor(COLOR_WHITE);
  u8g2Fonts.setCursor(2, 14);
  u8g2Fonts.print(line1);
  u8g2Fonts.setCursor(2, 30);
  u8g2Fonts.print(line2);
  u8g2Fonts.setCursor(2, 46);
  u8g2Fonts.print(line3);
  u8g2Fonts.setCursor(2, 62);
  u8g2Fonts.print(line4);
  if (line5.length() > 0) {
    u8g2Fonts.setCursor(2, 78);
    u8g2Fonts.print(line5);
  }
  if (line6.length() > 0) {
    u8g2Fonts.setCursor(2, 94);
    u8g2Fonts.print(line6);
  }
  tft.drawRGBBitmap(0, 0, canvas->getBuffer(), canvas->width(), canvas->height());
}

void setup() {
  dataMutex = xSemaphoreCreateMutex();
  Serial.setRxBufferSize(2048);
  Serial.begin(115200);

  pinMode(BLK_PIN, OUTPUT);
  digitalWrite(BLK_PIN, HIGH);

  pinMode(TFT_CS_PIN, OUTPUT);
  digitalWrite(TFT_CS_PIN, HIGH);

  pinMode(TFT_RST_PIN, OUTPUT);
  digitalWrite(TFT_RST_PIN, HIGH);
  delay(20);
  digitalWrite(TFT_RST_PIN, LOW);
  delay(20);
  digitalWrite(TFT_RST_PIN, HIGH);
  delay(120);

  SPI.begin(TFT_SCLK_PIN, -1, TFT_MOSI_PIN, -1);

  Serial.println("\n\n=== Serial Started ===");
  Serial.println("Booting...");
  Serial.print("Powered By LaoShui @ 2026");

  tft.initR(TFT_INIT_MODE);
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("TFT init done");

  canvas = new GFXcanvas16(tft.width(), tft.height());
  lyricCanvas = new GFXcanvas16(128, 20);

  u8g2Fonts.begin(*canvas);
  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setForegroundColor(ST77XX_WHITE);
  u8g2Fonts.setBackgroundColor(ST77XX_BLACK);
  u8g2Fonts.setFontDirection(0);

  drawBootText("SYSTEM BOOTING...", "INITIALIZING HARDWARE", "MOUNTING FILESYSTEM", "STARTING WLAN MAC...");

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(wifiOutputPower);
  WiFi.begin(ssid, password);

  int dotCount = 0;
  // 20秒超时，约66次循环
  while (WiFi.status() != WL_CONNECTED && dotCount < 66) {
    String dots = "";
    for (int i = 0; i < (dotCount % 4); i++) {
      dots += ".";
    }
    drawBootText("SYSTEM BOOTING  [OK]", "WLAN MAC INIT   [OK]", "WIFI: " + String(ssid), "DHCP REQ" + dots);
    delay(300);
    dotCount++;
    Serial.print(".");
  }

  Serial.println();

  BLEDevice::init("ClassIsland_TFT");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    drawBootText("SYSTEM BOOTING  [OK]", "WLAN CONNECTED  [OK]", "IP: " + WiFi.localIP().toString(), "SYNCING NTP...");

    configTime(8 * 3600, 0, "ntp.aliyun.com", "ntp.ntsc.ac.cn", "cn.pool.ntp.org");

    int retry = 0;
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo) && retry < 20) {
      String dots = "";
      for (int i = 0; i < (retry % 4); i++) {
        dots += ".";
      }
      drawBootText("SYSTEM BOOTING  [OK]", "WLAN CONNECTED  [OK]", "IP: " + WiFi.localIP().toString(), "SYNC NTP" + dots);
      delay(500);
      retry++;
    }

    drawBootText("SYSTEM BOOTING  [OK]", "WLAN CONNECTED  [OK]", "IP: " + WiFi.localIP().toString(), retry < 20 ? "NTP SYNC        [OK]" : "NTP SYNC      [FAIL]", "STARTING DAEMON...");
    delay(800);

    voiceHubFetching = true;
    xTaskCreate(fetchVoiceHubDataTask, "fetchVoiceHub", 8192, NULL, 1, NULL);
    xTaskCreate(fetchCourseDataTask, "fetchCourse", 8192, NULL, 1, NULL);
    lastCourseFetchTime = millis();
  } else {
    isBluetoothMode = true;
    Serial.println("WiFi failed. Starting Bluetooth...");
    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    drawBootText("SYSTEM BOOTING  [OK]", "WLAN CONNECTED[FAIL]", "BLUETOOTH MAC   [OK]", "WAITING FOR DATA...");
    delay(2000);
    
    BLEServer *pServer = BLEDevice::createServer();
    BLEService *pService = pServer->createService(SERVICE_UUID);
    BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_WRITE
                                       );
    pCharacteristic->setCallbacks(new MyServerCallbacks());
    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
  }
  
  // Initialize BLE Client
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  doScan = true;
  
  delay(100);
}

void fetchCourseDataTask(void *pvParameters) {
  if (WiFi.status() != WL_CONNECTED) {
    vTaskDelete(NULL);
    return;
  }

  int maxRetries = 3;
  int currentTry = 0;

  while (currentTry < maxRetries) {
    currentTry++;

    HTTPClient http;
    http.setTimeout(5000);
    http.begin(courseApiUrl);
    int httpCode = http.GET();

    if (httpCode <= 0 || httpCode == 500) {
      http.end();
      if (currentTry < maxRetries) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
      }
      continue;
    }

    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      if (currentTry < maxRetries) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
      }
      continue;
    }

    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      if (doc.containsKey("weather")) {
        String weatherText = doc["weather"]["text"].as<String>();
        String temp = doc["weather"]["temp"].as<String>();
        weatherStr = weatherText + " " + temp + "℃";
        if (doc["weather"].containsKey("warning")) {
          String warningStr = doc["weather"]["warning"].as<String>();
          if (warningStr.length() > 0) {
            weatherStr += " " + warningStr;
          }
        }
        if (doc["weather"].containsKey("rain")) {
          String rainStr = doc["weather"]["rain"].as<String>();
          if (rainStr.length() > 0) {
            weatherStr += " " + rainStr;
          }
        }
      }

      courseCount = 0;
      fullScheduleStr = "";

      if (doc.containsKey("courses") && doc["courses"].is<JsonArray>()) {
        JsonArray coursesArr = doc["courses"].as<JsonArray>();
        for (JsonObject courseObj : coursesArr) {
          if (courseCount >= MAX_COURSES) {
            break;
          }
          dailyCourses[courseCount].name = courseObj["name"].as<String>();
          dailyCourses[courseCount].startTime = courseObj["startTime"].as<String>();
          dailyCourses[courseCount].endTime = courseObj["endTime"].as<String>();
          dailyCourses[courseCount].startMin = timeStringToMinutes(dailyCourses[courseCount].startTime);
          dailyCourses[courseCount].endMin = timeStringToMinutes(dailyCourses[courseCount].endTime);
          fullScheduleStr += dailyCourses[courseCount].name + " ";
          courseCount++;
        }
      }

      tomorrowCourseCount = 0;
      if (doc.containsKey("tomorrowCourses") && doc["tomorrowCourses"].is<JsonArray>()) {
        JsonArray coursesArr = doc["tomorrowCourses"].as<JsonArray>();
        for (JsonObject courseObj : coursesArr) {
          if (tomorrowCourseCount >= MAX_COURSES) {
            break;
          }
          tomorrowCourses[tomorrowCourseCount].name = courseObj["name"].as<String>();
          tomorrowCourses[tomorrowCourseCount].startTime = courseObj["startTime"].as<String>();
          tomorrowCourses[tomorrowCourseCount].endTime = courseObj["endTime"].as<String>();
          tomorrowCourses[tomorrowCourseCount].startMin = timeStringToMinutes(tomorrowCourses[tomorrowCourseCount].startTime);
          tomorrowCourses[tomorrowCourseCount].endMin = timeStringToMinutes(tomorrowCourses[tomorrowCourseCount].endTime);
          tomorrowCourseCount++;
        }
      }

      marqueeOffset = 0.0f;
      xSemaphoreGive(dataMutex);
    }
    
    vTaskDelete(NULL);
    return;
  }
  
  vTaskDelete(NULL);
}

void fetchVoiceHubDataTask(void *pvParameters) {
  if (WiFi.status() != WL_CONNECTED || voiceHubFetched) {
    voiceHubFetching = false;
    vTaskDelete(NULL);
    return;
  }

  HTTPClient http;
  http.setTimeout(10000);
  http.begin(voiceHubApiUrl);

  int httpCode = http.GET();
  if (httpCode <= 0) {
    http.end();
    voiceHubFetching = false;
    vTaskDelete(NULL);
    return;
  }

  String payload = http.getString();
  http.end();

  if (payload.length() == 0) {
    voiceHubFetching = false;
    vTaskDelete(NULL);
    return;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, payload);
  if (err || doc["status"] != "success") {
    voiceHubFetching = false;
    vTaskDelete(NULL);
    return;
  }

  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    JsonArray itemsArr = doc["data"].as<JsonArray>();
    if (itemsArr.size() == 0) {
      voiceHubStr = "暂无近期排期";
      voiceHubFetched = true;
      voiceHubFetching = false;
      xSemaphoreGive(dataMutex);
      vTaskDelete(NULL);
      return;
    }

    String targetDate = doc["targetDate"].as<String>();
    voiceHubStr = "广播站排期 " + targetDate + ": ";

    int index = 1;
    for (JsonObject song : itemsArr) {
      String title = song["title"].as<String>();
      String artist = song["artist"].as<String>();
      String requester = song["requester"].as<String>();
      voiceHubStr += "#" + String(index) + " " + title + "-" + artist;
      if (requester.length() > 0) {
        voiceHubStr += "-" + requester;
      }
      voiceHubStr += "  ";
      index++;
    }

    voiceHubFetched = true;
    voiceHubFetching = false;
    xSemaphoreGive(dataMutex);
  }
  
  vTaskDelete(NULL);
}

int currentDurationSec = -1;
int currentRemainingSec = -1;

void updateCurrentCourse(int currentSecTotal) {
  if (courseCount == 0) {
    if (currentCourse == "加载中") {
      currentScenario = SCENARIO_LOADING;
      return;
    }
    currentScenario = SCENARIO_NO_CLASSES;
    return;
  }

  Scenario newScenario = SCENARIO_END_OF_DAY;
  int newCourseIndex = -1;
  int newNextCourseIndex = -1;
  int newProgressPercent = 100;
  int newRemainingSec = -1;
  int newDurationSec = -1;

  for (int i = 0; i < courseCount; i++) {
    int startSec = dailyCourses[i].startMin * 60;
    int endSec = dailyCourses[i].endMin * 60;

    if (currentSecTotal >= startSec && currentSecTotal <= endSec) {
      newScenario = SCENARIO_IN_CLASS;
      newCourseIndex = i;
      newNextCourseIndex = (i + 1 < courseCount) ? i + 1 : -1;
      
      newDurationSec = endSec - startSec;
      int elapsed = currentSecTotal - startSec;
      newProgressPercent = (newDurationSec > 0) ? (elapsed * 100 / newDurationSec) : 100;
      newRemainingSec = endSec - currentSecTotal;
      break;
    }
    
    if (i < courseCount - 1) {
      int nextStartSec = dailyCourses[i + 1].startMin * 60;
      if (currentSecTotal > endSec && currentSecTotal < nextStartSec) {
        newScenario = SCENARIO_BREAK;
        newNextCourseIndex = i + 1;
        
        newDurationSec = -1; // No duration for breaks
        newProgressPercent = 100;
        newRemainingSec = nextStartSec - currentSecTotal;
        break;
      }
    }

    if (i == 0 && currentSecTotal < startSec) {
      newScenario = SCENARIO_PRE_CLASS;
      newNextCourseIndex = 0;
      
      newDurationSec = -1; // No duration for pre-class
      newProgressPercent = 100;
      newRemainingSec = startSec - currentSecTotal;
      break;
    }
  }

  if (newScenario != currentScenario || newCourseIndex != currentCourseIndex || newNextCourseIndex != nextCourseIndex) {
    currentCourseMarqueeOffset = 0.0f;
    lastCurrentCourseMarqueeTime = 0;
  }

  currentScenario = newScenario;
  currentCourseIndex = newCourseIndex;
  nextCourseIndex = newNextCourseIndex;
  progressPercent = newProgressPercent;
  currentRemainingSec = newRemainingSec;
  currentDurationSec = newDurationSec;
}

String buildFooterText() {
  if (currentScenario == SCENARIO_LOADING) return "数据加载中...    ";
  if (voiceHubFetched && voiceHubStr.length() > 0 && voiceHubStr.indexOf("暂无") == -1) {
    return voiceHubStr + "    ";
  }
  return "暂无排期数据    ";
}

String fitTextToWidth(const String& text, int maxWidth) {
  if (u8g2Fonts.getUTF8Width(text.c_str()) <= maxWidth) {
    return text;
  }

  String result = text;
  while (result.length() > 0) {
    result.remove(result.length() - 1);
    String candidate = result + "...";
    if (u8g2Fonts.getUTF8Width(candidate.c_str()) <= maxWidth) {
      return candidate;
    }
  }

  return "...";
}

void drawSunIcon(int x, int y, uint16_t color) {
  canvas->fillCircle(x + 6, y + 6, 2, color);
  canvas->drawLine(x + 6, y + 1, x + 6, y + 2, color);
  canvas->drawLine(x + 6, y + 10, x + 6, y + 11, color);
  canvas->drawLine(x + 1, y + 6, x + 2, y + 6, color);
  canvas->drawLine(x + 10, y + 6, x + 11, y + 6, color);
  canvas->drawLine(x + 2, y + 2, x + 3, y + 3, color);
  canvas->drawLine(x + 10, y + 10, x + 9, y + 9, color);
  canvas->drawLine(x + 10, y + 2, x + 9, y + 3, color);
  canvas->drawLine(x + 2, y + 10, x + 3, y + 9, color);
}

void drawCloudIcon(int x, int y, uint16_t color) {
  canvas->fillCircle(x + 4, y + 8, 2, color);
  canvas->fillCircle(x + 8, y + 8, 2, color);
  canvas->fillCircle(x + 6, y + 6, 3, color);
  canvas->fillRect(x + 4, y + 6, 5, 5, color);
}

void drawRainIcon(int x, int y, uint16_t color) {
  drawCloudIcon(x, y - 1, color);
  canvas->drawLine(x + 4, y + 10, x + 3, y + 12, color);
  canvas->drawLine(x + 6, y + 10, x + 5, y + 12, color);
  canvas->drawLine(x + 8, y + 10, x + 7, y + 12, color);
}

void drawClockIcon(int x, int y, uint16_t color) {
  canvas->drawCircle(x + 6, y + 6, 4, color);
  canvas->drawLine(x + 6, y + 6, x + 6, y + 4, color);
  canvas->drawLine(x + 6, y + 6, x + 8, y + 6, color);
}

void drawBookIcon(int x, int y, uint16_t color) {
  canvas->drawRect(x + 2, y + 3, 4, 6, color);
  canvas->drawRect(x + 6, y + 3, 4, 6, color);
  canvas->drawLine(x + 2, y + 4, x + 5, y + 4, color);
  canvas->drawLine(x + 6, y + 4, x + 9, y + 4, color);
}

void drawRadioIcon(int x, int y, uint16_t color) {
  canvas->fillCircle(x + 6, y + 6, 1, color);
  canvas->drawPixel(x + 3, y + 4, color);
  canvas->drawPixel(x + 2, y + 5, color);
  canvas->drawPixel(x + 2, y + 6, color);
  canvas->drawPixel(x + 2, y + 7, color);
  canvas->drawPixel(x + 3, y + 8, color);
  canvas->drawPixel(x + 9, y + 4, color);
  canvas->drawPixel(x + 10, y + 5, color);
  canvas->drawPixel(x + 10, y + 6, color);
  canvas->drawPixel(x + 10, y + 7, color);
  canvas->drawPixel(x + 9, y + 8, color);
}

void drawSnowIcon(int x, int y, uint16_t color) {
  canvas->drawLine(x + 6, y + 2, x + 6, y + 10, color);
  canvas->drawLine(x + 2, y + 6, x + 10, y + 6, color);
  canvas->drawLine(x + 3, y + 3, x + 9, y + 9, color);
  canvas->drawLine(x + 3, y + 9, x + 9, y + 3, color);
}

void drawWindIcon(int x, int y, uint16_t color) {
  canvas->drawLine(x + 2, y + 4, x + 8, y + 4, color);
  canvas->drawLine(x + 8, y + 4, x + 9, y + 5, color);
  canvas->drawLine(x + 4, y + 7, x + 10, y + 7, color);
  canvas->drawLine(x + 10, y + 7, x + 9, y + 8, color);
  canvas->drawLine(x + 3, y + 10, x + 7, y + 10, color);
}

void drawFogIcon(int x, int y, uint16_t color) {
  canvas->drawLine(x + 3, y + 4, x + 9, y + 4, color);
  canvas->drawLine(x + 2, y + 7, x + 10, y + 7, color);
  canvas->drawLine(x + 4, y + 10, x + 8, y + 10, color);
}

void drawWarningIcon(int x, int y, uint16_t bgColor, uint16_t fgColor, const String& warningText) {
  // Draw a filled circle as background
  canvas->fillCircle(x + 6, y + 6, 6, bgColor);
  
  if (warningText.indexOf("风") != -1) {
    // Small wind icon inside
    canvas->drawLine(x + 3, y + 4, x + 7, y + 4, fgColor);
    canvas->drawLine(x + 7, y + 4, x + 8, y + 5, fgColor);
    canvas->drawLine(x + 5, y + 6, x + 9, y + 6, fgColor);
    canvas->drawLine(x + 9, y + 6, x + 8, y + 7, fgColor);
    canvas->drawLine(x + 4, y + 8, x + 7, y + 8, fgColor);
  } else if (warningText.indexOf("雾") != -1) {
    // Small fog icon inside
    canvas->drawLine(x + 4, y + 4, x + 8, y + 4, fgColor);
    canvas->drawLine(x + 3, y + 6, x + 9, y + 6, fgColor);
    canvas->drawLine(x + 4, y + 8, x + 8, y + 8, fgColor);
  } else if (warningText.indexOf("雪") != -1) {
    // Small snow icon inside
    canvas->drawLine(x + 6, y + 3, x + 6, y + 9, fgColor);
    canvas->drawLine(x + 3, y + 6, x + 9, y + 6, fgColor);
    canvas->drawLine(x + 4, y + 4, x + 8, y + 8, fgColor);
    canvas->drawLine(x + 4, y + 8, x + 8, y + 4, fgColor);
  } else if (warningText.indexOf("雨") != -1) {
    // Small rain icon inside
    canvas->drawLine(x + 4, y + 7, x + 3, y + 9, fgColor);
    canvas->drawLine(x + 6, y + 7, x + 5, y + 9, fgColor);
    canvas->drawLine(x + 8, y + 7, x + 7, y + 9, fgColor);
    canvas->drawLine(x + 4, y + 5, x + 8, y + 5, fgColor); // cloud base
  } else if (warningText.indexOf("雷") != -1) {
    // Lightning icon inside
    canvas->drawLine(x + 6, y + 3, x + 4, y + 6, fgColor);
    canvas->drawLine(x + 4, y + 6, x + 7, y + 6, fgColor);
    canvas->drawLine(x + 7, y + 6, x + 5, y + 9, fgColor);
  } else if (warningText.indexOf("冰雹") != -1) {
    // Hail icon inside
    canvas->drawPixel(x + 4, y + 4, fgColor);
    canvas->drawPixel(x + 8, y + 5, fgColor);
    canvas->drawPixel(x + 6, y + 7, fgColor);
    canvas->drawPixel(x + 5, y + 9, fgColor);
  } else if (warningText.indexOf("霜冻") != -1 || warningText.indexOf("寒潮") != -1) {
    // Frost/Cold wave (similar to snow/ice)
    canvas->drawLine(x + 4, y + 8, x + 8, y + 8, fgColor);
    canvas->drawLine(x + 6, y + 6, x + 6, y + 8, fgColor);
    canvas->drawLine(x + 4, y + 6, x + 6, y + 6, fgColor);
  } else if (warningText.indexOf("高温") != -1) {
    // High temp / Sun icon
    canvas->drawCircle(x + 6, y + 6, 2, fgColor);
    canvas->drawPixel(x + 6, y + 2, fgColor);
    canvas->drawPixel(x + 6, y + 10, fgColor);
    canvas->drawPixel(x + 2, y + 6, fgColor);
    canvas->drawPixel(x + 10, y + 6, fgColor);
  } else {
    // Fallback: Exclamation mark inside
    canvas->drawLine(x + 6, y + 3, x + 6, y + 6, fgColor);
    canvas->drawPixel(x + 6, y + 8, fgColor);
  }
}

String animPrevStatusText = "";
String animPrevMainText = "";
uint16_t animPrevStatusColor = COLOR_WHITE;
bool isStateAnimating = false;
unsigned long stateAnimStartTime = 0;
String lastStatusText = "";
String lastMainText = "";
uint16_t lastStatusColor = COLOR_WHITE;
float currentScheduleScrollY = 0.0f;

// Lyric State
char serialLine[4096];
size_t serialLinePos = 0;
String songName = "";
String artist = "";
String currentLyric = "";
String nextLyric = "";
String prevLyric = "";
String playedLyric = "";
String currentWord = "";
bool isPlaying = false;
uint32_t progressMs = 0;
uint32_t durationMs = 0;
uint32_t prevLyricStartMs = 0;
uint32_t prevLyricEndMs = 0;
uint32_t currentLyricStartMs = 0;
uint32_t currentLyricEndMs = 0;
uint32_t nextLyricStartMs = 0;
uint32_t nextLyricEndMs = 0;
uint32_t currentWordStartMs = 0;
uint32_t currentWordEndMs = 0;
uint32_t progressSyncAt = 0;
uint32_t lastLyricPacketAt = 0;
uint32_t lyricAnimStartMs = 0;
const uint32_t LYRIC_TIMEOUT_MS = 5000;

uint16_t currentCover[400]; // 20x20 RGB565
bool hasCover = false;

uint8_t hexCharToInt(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

uint32_t currentDisplayProgressMs() {
  uint32_t displayProgress = progressMs;
  if (isPlaying) {
    uint32_t elapsed = millis() - progressSyncAt;
    displayProgress += elapsed;
  }
  if (durationMs > 0 && displayProgress > durationMs) {
    displayProgress = durationMs;
  }
  return displayProgress;
}

void updateLyricFromJson(const char* line) {
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, line);
  if (err) return;
  
  if (doc.containsKey("type") && doc["type"] == "cover") {
    const char* hexStr = doc["data"] | "";
    int len = strlen(hexStr);
    int pixelCount = 0;
    for (int i = 0; i < len && i + 3 < len && pixelCount < 400; i += 4) {
      uint16_t color = (hexCharToInt(hexStr[i]) << 12) |
                       (hexCharToInt(hexStr[i+1]) << 8) |
                       (hexCharToInt(hexStr[i+2]) << 4) |
                       hexCharToInt(hexStr[i+3]);
      // Swap endianness if necessary for Adafruit_GFX
      // Adafruit_GFX drawRGBBitmap expects big-endian uint16_t arrays on little-endian MCU (ESP32 is little-endian)
      // Actually drawRGBBitmap can handle uint16_t* natively, so no swap needed here if we pass uint16_t*
      currentCover[pixelCount++] = color;
    }
    hasCover = (pixelCount > 0);
    return;
  }

  uint32_t newStartMs = doc["currentLyricStartMs"] | 0;
  if (newStartMs != currentLyricStartMs) {
    if (currentLyricStartMs != 0 && newStartMs > currentLyricStartMs && (newStartMs - currentLyricStartMs) < 30000) {
      prevLyric = currentLyric;
      prevLyricStartMs = currentLyricStartMs;
      prevLyricEndMs = currentLyricEndMs;
      lyricAnimStartMs = millis();
    } else {
      lyricAnimStartMs = 0;
    }
  }

  songName = doc["songName"] | "";
  artist = doc["artist"] | "";
  currentLyric = doc["currentLyric"] | "";
  nextLyric = doc["nextLyric"] | "";
  playedLyric = doc["playedLyric"] | "";
  currentWord = doc["currentWord"] | "";
  
  // Clean up newlines and tabs
  currentLyric.replace("\r", " "); currentLyric.replace("\n", " "); currentLyric.replace("\t", " ");
  nextLyric.replace("\r", " "); nextLyric.replace("\n", " "); nextLyric.replace("\t", " ");
  
  currentLyricStartMs = newStartMs;
  currentLyricEndMs = doc["currentLyricEndMs"] | 0;
  nextLyricStartMs = doc["nextLyricStartMs"] | 0;
  nextLyricEndMs = doc["nextLyricEndMs"] | 0;
  currentWordStartMs = doc["currentWordStartMs"] | 0;
  currentWordEndMs = doc["currentWordEndMs"] | 0;
  isPlaying = doc["isPlaying"] | false;
  progressMs = doc["progressMs"] | 0;
  durationMs = doc["durationMs"] | 0;
  lastLyricPacketAt = millis();
  progressSyncAt = lastLyricPacketAt;
}

void readSerialLines() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      if (serialLinePos > 0) {
        serialLine[serialLinePos] = '\0';
        updateLyricFromJson(serialLine);
        serialLinePos = 0;
      }
      continue;
    }
    if (c != '\r') {
      if (serialLinePos + 1 < sizeof(serialLine)) {
        serialLine[serialLinePos++] = c;
      } else {
        serialLinePos = 0;
      }
    }
  }
}

// Clock Animation State
char lastTimeChars[9] = "00:00:00";
unsigned long clockAnimStart[8] = {0};
bool isClockAnimating[8] = {false};

void drawArc(int x, int y, int r, int thickness, int startAngle, int endAngle, uint16_t color) {
  if (startAngle > endAngle) {
    int temp = startAngle;
    startAngle = endAngle;
    endAngle = temp;
  }
  for (int i = startAngle; i <= endAngle; i++) {
    float rad = i * PI / 180.0;
    float cosRad = cos(rad);
    float sinRad = sin(rad);
    for (int t = 0; t < thickness; t++) {
      int px = x + (r - t) * cosRad;
      int py = y + (r - t) * sinRad;
      canvas->drawPixel(px, py, color);
    }
  }
}

void drawHeartIcon(int x, int y, uint16_t color, bool big) {
  if (big) {
    canvas->fillCircle(x + 3, y + 3, 2, color);
    canvas->fillCircle(x + 7, y + 3, 2, color);
    canvas->fillTriangle(x + 1, y + 4, x + 9, y + 4, x + 5, y + 9, color);
  } else {
    canvas->drawPixel(x + 2, y + 2, color);
    canvas->drawPixel(x + 3, y + 2, color);
    canvas->drawPixel(x + 6, y + 2, color);
    canvas->drawPixel(x + 7, y + 2, color);
    canvas->drawLine(x + 1, y + 3, x + 4, y + 3, color);
    canvas->drawLine(x + 5, y + 3, x + 8, y + 3, color);
    canvas->drawLine(x + 2, y + 4, x + 7, y + 4, color);
    canvas->drawLine(x + 3, y + 5, x + 6, y + 5, color);
    canvas->drawLine(x + 4, y + 6, x + 5, y + 6, color);
    canvas->drawPixel(x + 5, y + 7, color);
  }
}

void drawTimedLyricTFT(int y, const String& text, uint32_t startMs, uint32_t endMs, uint32_t displayProgress, bool isCurrentLine, bool shiftRight) {
  if (text.length() == 0) return;
  
  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
  int textWidth = u8g2Fonts.getUTF8Width(text.c_str());
  int offset = 0;
  
  int startX = shiftRight ? 26 : 6;
  int maxWidth = 128 - startX;
  
  // Auto scroll long lyrics
  if (textWidth > maxWidth && endMs > startMs) {
    int overflow = textWidth - maxWidth + 12; // 12px margin
    uint32_t progress = displayProgress;
    if (progress < startMs) progress = startMs;
    if (progress > endMs) progress = endMs;
    uint32_t numerator = (progress - startMs) * static_cast<uint32_t>(overflow);
    uint32_t denominator = endMs - startMs;
    offset = denominator > 0 ? static_cast<int>(numerator / denominator) : 0;
    if (offset < 0) offset = 0;
    if (offset > overflow) offset = overflow;
  }
  
  int drawX = startX - offset;
  
  if (isCurrentLine && (playedLyric.length() > 0 || currentWord.length() > 0)) {
    // Implement "brightness change" by drawing the played part in WHITE and unplayed in GRAY
    String played = playedLyric;
    String word = currentWord;
    String rest = text;
    
    if (rest.startsWith(played)) {
      rest = rest.substring(played.length());
    }
    if (rest.startsWith(word)) {
      rest = rest.substring(word.length());
    }
    
    int currentX = drawX;
    
    if (played.length() > 0) {
      u8g2Fonts.setForegroundColor(COLOR_WHITE);
      u8g2Fonts.setCursor(currentX, y);
      u8g2Fonts.print(played);
      currentX += u8g2Fonts.getUTF8Width(played.c_str());
    }
    
    if (word.length() > 0) {
      // The current word is transitioning, we draw it in CYAN to highlight it
      u8g2Fonts.setForegroundColor(COLOR_CYAN);
      u8g2Fonts.setCursor(currentX, y);
      u8g2Fonts.print(word);
      currentX += u8g2Fonts.getUTF8Width(word.c_str());
    }
    
    if (rest.length() > 0) {
      u8g2Fonts.setForegroundColor(COLOR_GRAY_500);
      u8g2Fonts.setCursor(currentX, y);
      u8g2Fonts.print(rest);
    }
    
    // Also keep the progress bar underneath for smooth pixel-perfect progress
    int w1 = u8g2Fonts.getUTF8Width(playedLyric.c_str());
    int w2 = u8g2Fonts.getUTF8Width(currentWord.c_str());
    int highlightW = w1;
    
    if (currentWordEndMs > currentWordStartMs) {
      uint32_t p = displayProgress;
      if (p < currentWordStartMs) p = currentWordStartMs;
      if (p > currentWordEndMs) p = currentWordEndMs;
      uint32_t num = (p - currentWordStartMs) * static_cast<uint32_t>(w2);
      uint32_t den = currentWordEndMs - currentWordStartMs;
      highlightW += static_cast<int>(num / den);
    }
    
    if (highlightW > 0) {
      lyricCanvas->fillRoundRect(drawX, y + 2, highlightW, 2, 1, COLOR_CYAN);
    }
  } else {
    // Normal drawing
    if (isCurrentLine) {
      u8g2Fonts.setForegroundColor(COLOR_WHITE);
    } else {
      u8g2Fonts.setForegroundColor(COLOR_GRAY_500);
    }
    u8g2Fonts.setCursor(drawX, y);
    u8g2Fonts.print(text);
    
    if (isCurrentLine && endMs > startMs) {
      uint32_t p = displayProgress;
      if (p < startMs) p = startMs;
      if (p > endMs) p = endMs;
      uint32_t num = (p - startMs) * static_cast<uint32_t>(textWidth);
      uint32_t den = endMs - startMs;
      int highlightW = static_cast<int>(num / den);
      if (highlightW > 0) {
        lyricCanvas->fillRoundRect(drawX, y + 2, highlightW, 2, 1, COLOR_CYAN);
      }
    }
  }
}

void updateDisplay() {
  if (canvas == nullptr) return;

  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    struct tm timeinfo;
    int currentSecTotal = 0;

  if (!getLocalTime(&timeinfo)) {
    currentTimeStr = "未同步";
  } else {
    char timeStr[10];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    currentTimeStr = String(timeStr);
    currentSecTotal = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
  }

  updateCurrentCourse(currentSecTotal);

  canvas->fillScreen(COLOR_BG);
  bool showLyrics = (lastLyricPacketAt > 0 && millis() - lastLyricPacketAt < LYRIC_TIMEOUT_MS);

  // Section 3: Bottom Section (Schedule) (74 - 107)
  // Auto-scrolling logic for the entire schedule
  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
  
  unsigned long nowScroll = millis();
  unsigned long dtScroll = nowScroll - lastScheduleScrollTime;
  if (lastScheduleScrollTime == 0) dtScroll = 0;
  lastScheduleScrollTime = nowScroll;

  // 12 pixels per second scroll speed
  float scrollStep = (dtScroll / 1000.0f) * 12.0f;

  if (currentScenario == SCENARIO_END_OF_DAY) {
    if (tomorrowCourseCount > 0) {
      // Calculate total height of tomorrow's schedule
      float totalScheduleHeight = tomorrowCourseCount * 18.0f;
      
      if (tomorrowCourseCount > 2) {
        currentScheduleScrollY += scrollStep;
        if (currentScheduleScrollY >= totalScheduleHeight) {
          currentScheduleScrollY -= totalScheduleHeight;
        }
      } else {
        currentScheduleScrollY = 0.0f;
      }

      for (int i = 0; i < tomorrowCourseCount * 2; i++) {
        int idx = i % tomorrowCourseCount;
        int y = 74 + 4 + i * 18 - (int)currentScheduleScrollY;
        
        if (y < 56 || y > 108) continue;

        int textY = y + 12;
        u8g2Fonts.setForegroundColor(COLOR_GRAY_300);
        u8g2Fonts.setCursor(6, textY);
        u8g2Fonts.print("明日 " + tomorrowCourses[idx].startTime);
        u8g2Fonts.setCursor(4 + 66, textY);
        u8g2Fonts.print(fitTextToWidth(tomorrowCourses[idx].name, 128 - 8 - 66));
      }
    } else {
      u8g2Fonts.setForegroundColor(COLOR_GRAY_400);
      u8g2Fonts.setCursor(6, 74 + 16);
      u8g2Fonts.print("明日无课安排");
    }
  } else if (currentScenario == SCENARIO_NO_CLASSES) {
    u8g2Fonts.setForegroundColor(COLOR_GRAY_400);
    u8g2Fonts.setCursor(6, 74 + 16);
    u8g2Fonts.print("今日无课安排");
  } else {
    // Regular course list scrolling
    float totalScheduleHeight = courseCount * 18.0f;
    
    if (courseCount > 2) {
      currentScheduleScrollY += scrollStep;
      
      // When we've scrolled past the entire original list, reset to 0
      if (currentScheduleScrollY >= totalScheduleHeight) {
        currentScheduleScrollY -= totalScheduleHeight;
      }
    } else {
      currentScheduleScrollY = 0.0f;
    }

    // Draw the list twice to create the wrap-around effect
    for (int i = 0; i < courseCount * 2; i++) {
      int idx = i % courseCount;
      int y = 74 + 2 + i * 18 - (int)currentScheduleScrollY;
      
      if (y < 56 || y > 108) continue;
      
      int textY = y + 12;
      
      bool isCurrent = (idx == currentCourseIndex);
      bool isPast = (idx < currentCourseIndex) || (currentCourseIndex == -1 && idx < nextCourseIndex);

      if (isCurrent) {
        canvas->fillRoundRect(4, y, 128 - 8, 16, 2, COLOR_FOOTER_BG);
        u8g2Fonts.setForegroundColor(COLOR_WHITE);
      } else if (isPast) {
        u8g2Fonts.setForegroundColor(COLOR_GRAY_500);
      } else {
        u8g2Fonts.setForegroundColor(COLOR_GRAY_300);
      }
      u8g2Fonts.setCursor(6, textY);
      u8g2Fonts.print(dailyCourses[idx].startTime);
      u8g2Fonts.setCursor(4 + 36, textY);
      u8g2Fonts.print(fitTextToWidth(dailyCourses[idx].name, 128 - 8 - 36));
    }
  }

  // Section 1: Top Bar (0 - 21)
  // We draw this later to overlap the course list scrolling up!
  // But wait, the section order matters.
  // Actually, let's just clear the section 2 area (22-73) right after we draw the schedule,
  // to cut off the top overflowing part.
  canvas->fillRect(0, 22, 128, 52, COLOR_BG);
  canvas->drawLine(0, 73, 127, 73, COLOR_GRAY_800);

  // Re-draw Top Bar background just in case
  canvas->fillRect(0, 0, 128, 22, COLOR_BG);
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2Fonts.setForegroundColor(COLOR_WHITE);

  // Animated Flip Clock logic
  const char* curTimeChars = currentTimeStr.c_str();
  int clockX = 4;
  for (int i = 0; i < currentTimeStr.length() && i < 8; i++) {
    char c = curTimeChars[i];
    int charWidth = u8g2Fonts.getUTF8Width(String(c).c_str());
    
    if (c != lastTimeChars[i]) {
      isClockAnimating[i] = true;
      clockAnimStart[i] = millis();
      lastTimeChars[i] = c;
    }

    if (isClockAnimating[i]) {
      unsigned long elapsed = millis() - clockAnimStart[i];
      if (elapsed < 200) {
        float t = (float)elapsed / 200.0f;
        int yOffset = (int)(t * 16.0f); // Scroll up effect
        
        // Simplified approach: just slide the new character in from the bottom.
        u8g2Fonts.setForegroundColor(COLOR_WHITE);
        u8g2Fonts.setCursor(clockX, 16 + 16 - yOffset);
        u8g2Fonts.print(String(c));
      } else {
        isClockAnimating[i] = false;
        u8g2Fonts.setForegroundColor(COLOR_WHITE);
        u8g2Fonts.setCursor(clockX, 16);
        u8g2Fonts.print(String(c));
      }
    } else {
      u8g2Fonts.setForegroundColor(COLOR_WHITE);
      u8g2Fonts.setCursor(clockX, 16);
      u8g2Fonts.print(String(c));
    }
    
    clockX += charWidth + 1; // 1px spacing
  }

  String tempText = "24°";
  String wText = weatherStr;
  String warningText = "";
  String rainText = "";
  uint16_t warningColor = COLOR_WHITE;

  int spaceIdx = weatherStr.indexOf(' ');
  if (spaceIdx > 0) {
    wText = weatherStr.substring(0, spaceIdx);
    int secondSpaceIdx = weatherStr.indexOf(' ', spaceIdx + 1);
    
    if (secondSpaceIdx > 0) {
      tempText = weatherStr.substring(spaceIdx + 1, secondSpaceIdx);
      String remainingText = weatherStr.substring(secondSpaceIdx + 1);
      
      int pipeIdx = remainingText.indexOf(" | ");
      if (pipeIdx > 0) {
        warningText = remainingText.substring(0, pipeIdx);
        rainText = remainingText.substring(pipeIdx + 3);
      } else {
        if (remainingText.indexOf("预警") != -1) {
          warningText = remainingText;
        } else {
          rainText = remainingText;
        }
      }
      
      if (warningText.indexOf("黄") != -1) warningColor = COLOR_YELLOW;
      else if (warningText.indexOf("橙") != -1) warningColor = 0xFD20; // Orange
      else if (warningText.indexOf("红") != -1) warningColor = 0xF800; // Red
      else if (warningText.indexOf("蓝") != -1) warningColor = COLOR_BLUE;
      else warningColor = COLOR_WHITE;
    } else {
      tempText = weatherStr.substring(spaceIdx + 1);
    }
    tempText.replace("℃", "°");
  }

  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
  int tempW = u8g2Fonts.getUTF8Width(tempText.c_str());
  int weatherX = 128 - 4 - tempW;

  u8g2Fonts.setForegroundColor(COLOR_WHITE);
  u8g2Fonts.setCursor(weatherX, 16);
  u8g2Fonts.print(tempText);

  int iconX = weatherX - 14;
  if (wText.indexOf("雪") != -1) {
    drawSnowIcon(iconX, 4, COLOR_WHITE);
  } else if (wText.indexOf("雾") != -1) {
    drawFogIcon(iconX, 4, COLOR_GRAY_300);
  } else if (wText.indexOf("风") != -1) {
    drawWindIcon(iconX, 4, COLOR_CYAN);
  } else if (wText.indexOf("雨") != -1) {
    drawRainIcon(iconX, 4, COLOR_BLUE);
  } else if (wText.indexOf("云") != -1 || wText.indexOf("阴") != -1) {
    drawCloudIcon(iconX, 4, COLOR_GRAY_300);
  } else {
    drawSunIcon(iconX, 4, COLOR_YELLOW);
  }

  if (warningText.length() > 0) {
    // Draw the warning icon to the left of the weather icon
    int warningIconX = iconX - 16; 
    uint16_t fgColor = (warningColor == COLOR_YELLOW || warningColor == COLOR_WHITE) ? COLOR_BG : COLOR_WHITE;
    drawWarningIcon(warningIconX, 4, warningColor, fgColor, warningText);
  }
  
  if (rainText.length() > 0) {
    if (rainText != rainMarqueeRenderStr) {
      rainMarqueeRenderStr = rainText;
      u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
      rainMarqueeTextWidth = u8g2Fonts.getUTF8Width(rainMarqueeRenderStr.c_str());
      rainMarqueeOffset = 0.0f;
      lastRainMarqueeTime = millis();
    }

    unsigned long now = millis();
    unsigned long dt = now - lastRainMarqueeTime;
    lastRainMarqueeTime = now;

    int rainMaxWidth = 128 - (warningText.length() > 0 ? 56 : 40); // 留出左侧时钟和右侧图标的空间
    int rainGap = 20;
    int totalRainW = rainMarqueeTextWidth + rainGap;

    if (rainMarqueeTextWidth > rainMaxWidth) {
      rainMarqueeOffset += (dt / 1000.0f) * 15.0f; // 15像素/秒滚动
      if (rainMarqueeOffset >= totalRainW) {
        rainMarqueeOffset -= totalRainW;
      }
    } else {
      rainMarqueeOffset = 0.0f;
    }

    int intOffset = (int)rainMarqueeOffset;
    int rainX = 64 - rainMaxWidth / 2;
    if (rainMarqueeTextWidth <= rainMaxWidth) {
      rainX = 64 - rainMarqueeTextWidth / 2; // 居中
    }

    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2Fonts.setForegroundColor(COLOR_CYAN);

    u8g2Fonts.setCursor(rainX - intOffset, 22 + 12);
    u8g2Fonts.print(rainMarqueeRenderStr);

    if (rainMarqueeTextWidth > rainMaxWidth && intOffset > rainMarqueeTextWidth - rainMaxWidth) {
      u8g2Fonts.setCursor(rainX - intOffset + totalRainW, 22 + 12);
      u8g2Fonts.print(rainMarqueeRenderStr);
    }
    
    // 手动遮挡溢出的文字
    // 左侧遮挡 (时钟区域)
    canvas->fillRect(0, 22, rainX, 14, COLOR_BG);
    // 右侧遮挡 (如果有预警图标，或者天气图标区域)
    canvas->fillRect(rainX + rainMaxWidth, 22, 128 - (rainX + rainMaxWidth), 14, COLOR_BG);
  }

  canvas->drawLine(0, 21, 127, 21, COLOR_GRAY_800);

  // Section 2: Middle Section (22 - 73)
  // Background was already cleared above, but we can do it again if needed
  // canvas->fillRect(0, 22, 128, 52, COLOR_BG);
  String statusText;
  uint16_t statusColor;
  String mainText;
  String timeRangeText;
  String remainingText;
  uint16_t progressColor;
  
  // Calculate total day progress for circular arc
  int totalDayProgressPercent = -1;
  if (courseCount > 0) {
    if (currentScenario == SCENARIO_END_OF_DAY) {
      totalDayProgressPercent = 100;
    } else if (currentScenario == SCENARIO_NO_CLASSES || currentScenario == SCENARIO_PRE_CLASS) {
      totalDayProgressPercent = 0;
    } else {
      // Calculate based on index
      int currentIndex = (currentCourseIndex >= 0) ? currentCourseIndex : nextCourseIndex;
      totalDayProgressPercent = (currentIndex * 100) / courseCount;
    }
  }

  if (currentScenario == SCENARIO_IN_CLASS) {
    statusText = "正在上课";
    statusColor = COLOR_CYAN;
    mainText = dailyCourses[currentCourseIndex].name;
    timeRangeText = dailyCourses[currentCourseIndex].startTime + " - " + dailyCourses[currentCourseIndex].endTime;
    
    if (currentRemainingSec <= 60 && currentRemainingSec >= 0) {
      remainingText = String(currentRemainingSec) + "s";
    } else {
      int remMin = (currentRemainingSec + 59) / 60;
      int durMin = currentDurationSec / 60;
      remainingText = "-" + String(remMin) + "m/" + String(durMin) + "m";
    }
    progressColor = COLOR_CYAN;
  } else if (currentScenario == SCENARIO_BREAK) {
    statusText = "课间休息";
    statusColor = COLOR_GREEN;
    mainText = "下一节: " + dailyCourses[nextCourseIndex].name;
    timeRangeText = dailyCourses[nextCourseIndex].startTime + " 开始";
    
    if (currentRemainingSec <= 60 && currentRemainingSec >= 0) {
      remainingText = String(currentRemainingSec) + "s";
    } else {
      int remMin = (currentRemainingSec + 59) / 60;
      remainingText = "-" + String(remMin) + "m";
    }
    progressColor = COLOR_CYAN;
  } else if (currentScenario == SCENARIO_PRE_CLASS) {
    statusText = "未上课";
    statusColor = COLOR_GREEN;
    mainText = "下一节: " + dailyCourses[nextCourseIndex].name;
    timeRangeText = dailyCourses[nextCourseIndex].startTime + " 开始";
    
    if (currentRemainingSec <= 60 && currentRemainingSec >= 0) {
      remainingText = String(currentRemainingSec) + "s";
    } else {
      int remMin = (currentRemainingSec + 59) / 60;
      remainingText = "-" + String(remMin) + "m";
    }
    progressColor = COLOR_CYAN;
  } else if (currentScenario == SCENARIO_END_OF_DAY) {
    statusText = "今日课程结束";
    statusColor = COLOR_PURPLE;
    mainText = "辛苦了，好好休息";
    timeRangeText = "已完成 " + String(courseCount) + " 节课";
    remainingText = "";
    progressColor = COLOR_PURPLE;
  } else if (currentScenario == SCENARIO_NO_CLASSES) {
    statusText = "今日无课";
    statusColor = COLOR_GRAY_400;
    mainText = "享受自由时光";
    timeRangeText = "";
    remainingText = "";
    progressColor = COLOR_GRAY_800;
  } else {
    statusText = "加载中...";
    statusColor = COLOR_GRAY_400;
    mainText = "正在获取数据";
    timeRangeText = "";
    remainingText = "";
    progressColor = COLOR_GRAY_800;
  }

  if (lastMainText != "" && (lastMainText != mainText || lastStatusText != statusText)) {
    isStateAnimating = true;
    stateAnimStartTime = millis();
    animPrevStatusText = lastStatusText;
    animPrevMainText = lastMainText;
    animPrevStatusColor = lastStatusColor;
    currentCourseMarqueeOffset = 0.0f;
  }
  lastStatusText = statusText;
  lastMainText = mainText;
  lastStatusColor = statusColor;

  int yOffset = 0;
  if (isStateAnimating) {
    unsigned long elapsed = millis() - stateAnimStartTime;
    if (elapsed < 500) {
      float t = (float)elapsed / 500.0f;
      float easeOut = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
      yOffset = (int)(easeOut * 24.0f);
    } else {
      isStateAnimating = false;
    }
  }

  if (isStateAnimating) {
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2Fonts.setForegroundColor(animPrevStatusColor);
    u8g2Fonts.setCursor(4, 22 + 12 - yOffset);
    u8g2Fonts.print(animPrevStatusText);

    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setForegroundColor(COLOR_WHITE);
    u8g2Fonts.setCursor(4, 22 + 30 - yOffset);
    u8g2Fonts.print(animPrevMainText);

    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2Fonts.setForegroundColor(statusColor);
    u8g2Fonts.setCursor(4, 22 + 12 + 24 - yOffset);
    u8g2Fonts.print(statusText);

    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setForegroundColor(COLOR_WHITE);
    u8g2Fonts.setCursor(4, 22 + 30 + 24 - yOffset);
    u8g2Fonts.print(mainText);
  } else {
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2Fonts.setForegroundColor(statusColor);
    u8g2Fonts.setCursor(4, 22 + 12);
    u8g2Fonts.print(statusText);

    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2Fonts.setForegroundColor(COLOR_WHITE);
    
    int mainTextWidth = u8g2Fonts.getUTF8Width(mainText.c_str());
    int maxMainTextWidth = 120;
    
    if (mainTextWidth > maxMainTextWidth) {
      unsigned long now = millis();
      unsigned long dt = now - lastCurrentCourseMarqueeTime;
      if (lastCurrentCourseMarqueeTime == 0) dt = 0;
      lastCurrentCourseMarqueeTime = now;
      
      currentCourseMarqueeOffset += (dt / 1000.0f) * 30.0f;
      if (currentCourseMarqueeOffset > mainTextWidth + 20) {
        currentCourseMarqueeOffset = -maxMainTextWidth;
      }
      
      u8g2Fonts.setCursor(4 - (int)currentCourseMarqueeOffset, 22 + 30);
      u8g2Fonts.print(mainText);
      
      canvas->fillRect(0, 22 + 14, 4, 18, COLOR_BG);
      canvas->fillRect(124, 22 + 14, 4, 18, COLOR_BG);
    } else {
      currentCourseMarqueeOffset = 0.0f;
      lastCurrentCourseMarqueeTime = 0;
      u8g2Fonts.setCursor(4, 22 + 30);
      u8g2Fonts.print(mainText);
    }
  }

  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2Fonts.setForegroundColor(COLOR_GRAY_400);
  
  if (!isStateAnimating) {
    if (timeRangeText.length() > 0) {
      if (currentScenario == SCENARIO_IN_CLASS) {
        // Hide time range for IN_CLASS
      } else {
        drawClockIcon(4, 22 + 35, COLOR_GRAY_400);
        u8g2Fonts.setCursor(4 + 14, 22 + 44);
        u8g2Fonts.print(timeRangeText);
      }
    }

    if (remainingText.length() > 0) {
      if (currentScenario == SCENARIO_IN_CLASS) {
        int remWidth = u8g2Fonts.getUTF8Width(remainingText.c_str());
        int startX = (128 - (14 + remWidth)) / 2;
        drawClockIcon(startX, 22 + 34, COLOR_GRAY_400);
        u8g2Fonts.setCursor(startX + 14, 22 + 44);
        u8g2Fonts.print(remainingText);
      } else {
        int remWidth = u8g2Fonts.getUTF8Width(remainingText.c_str());
        u8g2Fonts.setCursor(128 - 4 - remWidth, 22 + 44);
        u8g2Fonts.print(remainingText);
      }
    }

    canvas->fillRoundRect(4, 22 + 48, 128 - 8, 2, 1, COLOR_GRAY_800);
    int pw = (128 - 8) * progressPercent / 100;
    
    if (currentScenario == SCENARIO_IN_CLASS) {
      // IN_CLASS: Progress bar grows from Left to Right
      if (pw > 0) canvas->fillRoundRect(4, 22 + 48, pw, 2, 1, progressColor);
    } else if (currentScenario == SCENARIO_BREAK || currentScenario == SCENARIO_PRE_CLASS) {
      // NOT IN_CLASS: Progress bar shrinks from Right to Left
      // Here progressPercent represents how much time has passed in the break
      // So (100 - progressPercent) is how much is remaining
      int remainingPw = (128 - 8) * (100 - progressPercent) / 100;
      if (remainingPw > 0) {
        // Draw from the right side towards the left
        int startX = 4 + (128 - 8) - remainingPw;
        canvas->fillRoundRect(startX, 22 + 48, remainingPw, 2, 1, progressColor);
      }
    } else {
      // Other scenarios (End of day, No classes): full bar
      if (pw > 0) canvas->fillRoundRect(4, 22 + 48, pw, 2, 1, progressColor);
    }
  }
  
  // Draw Circular Progress Bar for Total Day Progress (Top Right of Middle Section)
  if (totalDayProgressPercent >= 0) {
    int arcX = 128 - 14;
    int arcY = 22 + 14;
    int arcR = 8;
    int arcThickness = 2;
    
    // Draw background track
    drawArc(arcX, arcY, arcR, arcThickness, 0, 360, COLOR_GRAY_800);
    
    // Draw filled track
    if (totalDayProgressPercent > 0) {
      int endAngle = -90 + (totalDayProgressPercent * 360) / 100;
      if (endAngle > 270) endAngle = 270;
      drawArc(arcX, arcY, arcR, arcThickness, -90, endAngle, progressColor);
    }
  }

  // Draw Heart Rate if available
  if (bleConnected && currentHeartRate > 0) {
    // If heart rate data is fresh (within 3 seconds)
    if (millis() - lastHeartRateTime < 3000) {
      int hrX = 128 - 44; 
      int hrY = 22 + 6;  // Move further up
      
      u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
      u8g2Fonts.setForegroundColor(COLOR_PINK);
      
      // Draw animated heart
      bool isBeat = heartBeatState;
      drawHeartIcon(hrX - 12, hrY, COLOR_PINK, isBeat);
      
      // Draw HR value
      String hrStr = String(currentHeartRate);
      u8g2Fonts.setCursor(hrX, hrY + 10);
      u8g2Fonts.print(hrStr);
    }
  }

  canvas->drawLine(0, 73, 127, 73, COLOR_GRAY_800);

  if (showLyrics) {
    lyricCanvas->fillScreen(COLOR_FOOTER_BG);
    lyricCanvas->drawLine(0, 0, 127, 0, COLOR_GRAY_800);

    u8g2Fonts.begin(*lyricCanvas);

    uint32_t currentDispMs = currentDisplayProgressMs();
    int yOffset = 0;
    if (lyricAnimStartMs > 0) {
      uint32_t elapsed = millis() - lyricAnimStartMs;
      if (elapsed < 300) {
        float t = (float)elapsed / 300.0f;
        float invT = 1.0f - t;
        float easeOut = 1.0f - (invT * invT * invT);
        yOffset = (int)((1.0f - easeOut) * 16.0f);
      } else {
        lyricAnimStartMs = 0;
      }
    }

    int baseTextY = 16; // 124 - 108

    if (lyricAnimStartMs > 0 && prevLyric.length() > 0) {
      drawTimedLyricTFT(
        baseTextY - 16 + yOffset,
        prevLyric,
        prevLyricStartMs,
        prevLyricEndMs,
        currentDispMs,
        false,
        hasCover
      );
    }

    drawTimedLyricTFT(
      baseTextY + yOffset,
      currentLyric,
      currentLyricStartMs,
      currentLyricEndMs,
      currentDispMs,
      true,
      hasCover
    );

    u8g2Fonts.begin(*canvas);

    if (hasCover) {
      lyricCanvas->fillRect(0, 0, 26, 20, COLOR_FOOTER_BG);
      lyricCanvas->drawRGBBitmap(2, 0, currentCover, 20, 20);
    }

    canvas->drawRGBBitmap(0, 108, lyricCanvas->getBuffer(), 128, 20);
  } else {
    canvas->fillRect(0, 108, 128, 20, COLOR_FOOTER_BG);
    
    String footerText = buildFooterText();
    if (footerText != marqueeRenderStr) {
      marqueeRenderStr = footerText;
      u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
      marqueeTextWidth = u8g2Fonts.getUTF8Width(marqueeRenderStr.c_str());
      marqueeOffset = 0.0f;
      lastMarqueeTime = 0;
    }

    unsigned long now = millis();
    unsigned long dt = now - lastMarqueeTime;
    if (lastMarqueeTime == 0) dt = 0;
    lastMarqueeTime = now;

    int footerWidth = 104;
    int marqueeGap = 32;
    int totalMarqueeW = marqueeTextWidth + marqueeGap;

    if (marqueeTextWidth > footerWidth) {
      marqueeOffset += (dt / 1000.0f) * 24.0f;
      if (marqueeOffset >= totalMarqueeW) {
        marqueeOffset -= totalMarqueeW;
      }
    } else {
      marqueeOffset = 0.0f;
    }

    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2Fonts.setForegroundColor(COLOR_GRAY_300);
    int footerY = 122;
    int footerLeft = 24;

    if (marqueeTextWidth > footerWidth) {
      int intOffset = (int)marqueeOffset;
      u8g2Fonts.setCursor(footerLeft - intOffset, footerY);
      u8g2Fonts.print(marqueeRenderStr);
      
      if (intOffset > marqueeTextWidth - footerWidth) {
        u8g2Fonts.setCursor(footerLeft - intOffset + totalMarqueeW, footerY);
        u8g2Fonts.print(marqueeRenderStr);
      }
    } else {
      u8g2Fonts.setCursor(footerLeft, footerY);
      u8g2Fonts.print(marqueeRenderStr);
    }

    canvas->fillRect(0, 108, 24, 20, COLOR_FOOTER_BG);
    canvas->drawLine(0, 108, 127, 108, COLOR_GRAY_800);
    drawRadioIcon(6, 112, COLOR_PINK);
  }

  tft.drawRGBBitmap(0, 0, canvas->getBuffer(), canvas->width(), canvas->height());
    xSemaphoreGive(dataMutex);
  }
}

void loop() {
  readSerialLines();

  if (isBluetoothMode) {
    if (newBleData) {
      String payload = blePayload;
      newBleData = false;
      blePayload = "";
      
      DynamicJsonDocument doc(4096);
      DeserializationError err = deserializeJson(doc, payload);
      
      if (!err) {
        if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
          if (doc.containsKey("timestamp")) {
            long ts = doc["timestamp"].as<long>();
            struct timeval tv;
            tv.tv_sec = ts + 8 * 3600;
            tv.tv_usec = 0;
            settimeofday(&tv, NULL);
          }

          if (doc.containsKey("weather")) {
            String weatherText = doc["weather"]["text"].as<String>();
            String temp = doc["weather"]["temp"].as<String>();
            weatherStr = weatherText + " " + temp + "℃";
            
            // 预警和降雨信息用特殊的分割符或者按顺序拼接
            String warningStr = "";
            if (doc["weather"].containsKey("warning")) {
              warningStr = doc["weather"]["warning"].as<String>();
            }
            
            String rainStr = "";
            if (doc["weather"].containsKey("rain")) {
              rainStr = doc["weather"]["rain"].as<String>();
            }

            if (warningStr.length() > 0 && rainStr.length() > 0) {
              weatherStr += " " + warningStr + " | " + rainStr;
            } else if (warningStr.length() > 0) {
              weatherStr += " " + warningStr;
            } else if (rainStr.length() > 0) {
              weatherStr += " " + rainStr;
            }
          }

          if (doc.containsKey("courses") && doc["courses"].is<JsonArray>()) {
            JsonArray coursesArr = doc["courses"].as<JsonArray>();
            courseCount = 0;
            fullScheduleStr = "";
            
            for (JsonObject courseObj : coursesArr) {
              if (courseCount >= MAX_COURSES) break;
              
              dailyCourses[courseCount].name = courseObj["name"].as<String>();
              dailyCourses[courseCount].startTime = courseObj["startTime"].as<String>();
              dailyCourses[courseCount].endTime = courseObj["endTime"].as<String>();
              dailyCourses[courseCount].startMin = timeStringToMinutes(dailyCourses[courseCount].startTime);
              dailyCourses[courseCount].endMin = timeStringToMinutes(dailyCourses[courseCount].endTime);
              
              fullScheduleStr += dailyCourses[courseCount].name + " ";
              courseCount++;
            }
          }

          if (doc.containsKey("tomorrowCourses") && doc["tomorrowCourses"].is<JsonArray>()) {
            JsonArray coursesArr = doc["tomorrowCourses"].as<JsonArray>();
            tomorrowCourseCount = 0;
            
            for (JsonObject courseObj : coursesArr) {
              if (tomorrowCourseCount >= MAX_COURSES) break;
              
              tomorrowCourses[tomorrowCourseCount].name = courseObj["name"].as<String>();
              tomorrowCourses[tomorrowCourseCount].startTime = courseObj["startTime"].as<String>();
              tomorrowCourses[tomorrowCourseCount].endTime = courseObj["endTime"].as<String>();
              tomorrowCourses[tomorrowCourseCount].startMin = timeStringToMinutes(tomorrowCourses[tomorrowCourseCount].startTime);
              tomorrowCourses[tomorrowCourseCount].endMin = timeStringToMinutes(tomorrowCourses[tomorrowCourseCount].endTime);
              
              tomorrowCourseCount++;
            }
          }
          
          if (doc.containsKey("voiceHub")) {
            String vh = doc["voiceHub"].as<String>();
            if (vh.length() > 0) {
              voiceHubStr = vh;
              voiceHubFetched = true;
            } else {
              voiceHubStr = "暂无近期排期";
              voiceHubFetched = true;
            }
          }
          
          marqueeOffset = 0.0f;
          xSemaphoreGive(dataMutex);
        }
      }
    }
  }

  unsigned long now = millis();

  // BLE Scan & Connect handling
  if (doConnect) {
    if (connectToServer()) {
      Serial.println("Connected to the BLE Server.");
    } else {
      Serial.println("Failed to connect to the BLE server.");
    }
    doConnect = false;
  }

  if (doScan && !bleConnected) {
    BLEDevice::getScan()->start(5, false);
    doScan = false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!voiceHubFetched && !voiceHubFetching) {
      voiceHubFetching = true;
      xTaskCreate(fetchVoiceHubDataTask, "fetchVoiceHub", 8192, NULL, 1, NULL);
    }
    if (now - lastCourseFetchTime >= 300000UL || lastCourseFetchTime == 0) { // 5 minutes
      lastCourseFetchTime = now == 0 ? 1 : now;
      xTaskCreate(fetchCourseDataTask, "fetchCourse", 8192, NULL, 1, NULL);
    }
  }

  if (now - lastDisplayTime >= 100UL) {
    updateDisplay();
    lastDisplayTime = now;
  }

  delay(1);
}

