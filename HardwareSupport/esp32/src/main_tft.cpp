#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <time.h>

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

void fetchCourseData();
void fetchVoiceHubData();
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
  while (WiFi.status() != WL_CONNECTED) {
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

  fetchVoiceHubData();
  fetchCourseData();
  lastCourseFetchTime = millis();
  delay(500);
}

void fetchCourseData() {
  if (WiFi.status() != WL_CONNECTED) {
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
        delay(1000);
      }
      continue;
    }

    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      if (currentTry < maxRetries) {
        delay(1000);
      }
      continue;
    }

    if (doc.containsKey("weather")) {
      String weatherText = doc["weather"]["text"].as<String>();
      String temp = doc["weather"]["temp"].as<String>();
      weatherStr = weatherText + " " + temp + "℃";
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
    return;
  }
}

void fetchVoiceHubData() {
  if (WiFi.status() != WL_CONNECTED || voiceHubFetched) {
    return;
  }

  HTTPClient http;
  http.setTimeout(10000);
  http.begin(voiceHubApiUrl);

  int httpCode = http.GET();
  if (httpCode <= 0) {
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  if (payload.length() == 0) {
    return;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, payload);
  if (err || doc["status"] != "success") {
    return;
  }

  JsonArray itemsArr = doc["data"].as<JsonArray>();
  if (itemsArr.size() == 0) {
    voiceHubStr = "暂无近期排期";
    voiceHubFetched = true;
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

String animPrevStatusText = "";
String animPrevMainText = "";
uint16_t animPrevStatusColor = COLOR_WHITE;
bool isStateAnimating = false;
unsigned long stateAnimStartTime = 0;
String lastStatusText = "";
String lastMainText = "";
uint16_t lastStatusColor = COLOR_WHITE;
float currentScheduleScrollY = 0.0f;

void updateDisplay() {
  if (canvas == nullptr) return;

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
        
        // Wrap around vertically if needed
        if (y < 74 - 18) continue;
        if (y > 128 + 18) continue;

        if (y > 40 && y < 128) {
          u8g2Fonts.setForegroundColor(COLOR_GRAY_300);
          u8g2Fonts.setCursor(6, y + 12);
          u8g2Fonts.print("明日 " + tomorrowCourses[idx].startTime);
          u8g2Fonts.setCursor(4 + 66, y + 12);
          u8g2Fonts.print(fitTextToWidth(tomorrowCourses[idx].name, 128 - 8 - 66));
        }
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
      
      // Skip items that are too far out of bounds
      if (y > 128 + 18) continue; 
      if (y < 74 - 18) continue;  
      
      if (y > 40 && y < 128) {
        bool isCurrent = (idx == currentCourseIndex);
        bool isPast = (idx < currentCourseIndex) || (currentCourseIndex == -1 && idx < nextCourseIndex);

        if (isCurrent) {
          canvas->fillRoundRect(4, y, 128 - 8, 16, 2, COLOR_GRAY_800);
          u8g2Fonts.setForegroundColor(COLOR_WHITE);
        } else if (isPast) {
          u8g2Fonts.setForegroundColor(COLOR_GRAY_500);
        } else {
          u8g2Fonts.setForegroundColor(COLOR_GRAY_300);
        }
        u8g2Fonts.setCursor(6, y + 12);
        u8g2Fonts.print(dailyCourses[idx].startTime);
        u8g2Fonts.setCursor(4 + 36, y + 12);
        u8g2Fonts.print(fitTextToWidth(dailyCourses[idx].name, 128 - 8 - 36));
      }
    }
  }

  // Section 1: Top Bar (0 - 21)
  canvas->fillRect(0, 0, 128, 22, COLOR_BG);
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2Fonts.setForegroundColor(COLOR_WHITE);
  u8g2Fonts.setCursor(4, 16);
  u8g2Fonts.print(currentTimeStr);

  String tempText = "24°";
  String wText = weatherStr;
  int spaceIdx = weatherStr.indexOf(' ');
  if (spaceIdx > 0) {
    wText = weatherStr.substring(0, spaceIdx);
    tempText = weatherStr.substring(spaceIdx + 1);
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

  canvas->drawLine(0, 21, 127, 21, COLOR_GRAY_800);

  // Section 2: Middle Section (22 - 73)
  canvas->fillRect(0, 22, 128, 52, COLOR_BG);
  String statusText;
  uint16_t statusColor;
  String mainText;
  String timeRangeText;
  String remainingText;
  uint16_t progressColor;

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
    if (pw > 0) canvas->fillRoundRect(4, 22 + 48, pw, 2, 1, progressColor);
  }

  canvas->drawLine(0, 73, 127, 73, COLOR_GRAY_800);

  // Section 4: Radio Marquee (108 - 127)
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

  tft.drawRGBBitmap(0, 0, canvas->getBuffer(), canvas->width(), canvas->height());
}

void loop() {
  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (!voiceHubFetched) {
      fetchVoiceHubData();
    }
    if (now - lastCourseFetchTime >= 30000UL) {
      fetchCourseData();
      lastCourseFetchTime = now;
    }
  }

  if (now - lastDisplayTime >= 100UL) {
    updateDisplay();
    lastDisplayTime = now;
  }

  delay(1);
}

