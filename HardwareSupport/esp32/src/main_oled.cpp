#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <time.h>

#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

// WiFi 配置
const char* ssid = "LHZX";
const char* password = "a12345678";
const wifi_power_t wifiOutputPower = WIFI_POWER_8_5dBm;

// Flask 服务器 API 地址
const char* courseApiUrl = "http://47.116.166.10:5000/api/course";
// VoiceHub API 地址
const char* voiceHubApiUrl = "http://47.116.166.10:5000/api/voicehub";

// OLED 初始化
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// 全局变量
String currentCourse = "加载中";
String nextCourse = "";
String currentTimeStr = "";
String weatherStr = "--";

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
String fullScheduleStr = "";
float marqueeOffset = 0.0f; // 改为 float，以支持亚像素级别的平滑移动
unsigned long lastMarqueeTime = 0;
String marqueeRenderStr = "";
int marqueeTextWidth = 0;

// 排期信息相关变量
String voiceHubStr = "";
bool voiceHubFetched = false;

// 动画与剩余时间状态
String prevCourseStr = "";
unsigned long animStartTime = 0;
bool isAnimating = false;
int currentRemainingMin = -1; // -1表示不显示倒计时
float currentCourseMarqueeOffset = 0.0f; // 改为 float
unsigned long lastCurrentCourseMarqueeTime = 0;

// 天气滚动状态
float weatherMarqueeOffset = 0.0f;
unsigned long lastWeatherMarqueeTime = 0;

int timeStringToMinutes(String t) {
  int colonIndex = t.indexOf(':');
  if (colonIndex > 0) {
    int h = t.substring(0, colonIndex).toInt();
    int m = t.substring(colonIndex + 1).toInt();
    return h * 60 + m;
  }
  return 0;
}

// 函数前置声明
void fetchCourseData();
void fetchVoiceHubData();
void updateDisplay();
void updateCurrentCourse(int currentMin);

// 为了防止阻塞跑马灯，我们将网络请求放在后台任务中
TaskHandle_t FetchTask;

void fetchNetworkDataTask(void * pvParameters) {
  // 广播站排期只在刚启动时获取一次
  fetchVoiceHubData();
  
  for (;;) {
    fetchCourseData();
    
    // 等待30秒
    vTaskDelay(30000 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  
  // 等待串口连接，或者给充足的时间让监视器挂载
  // int waitCount = 0;
  // while (!Serial && waitCount < 30) {
  //   delay(100);
  //   waitCount++;
  // }
  // delay(2000); // 强制额外等待 2 秒，确保 PlatformIO 的 Monitor 完全打开
  
  Serial.println("\n\n=== Serial Started ===");
  
  // 提高 I2C 速率到 400kHz (Fast Mode) 甚至 800kHz 可以大幅提升屏幕刷新率，降低底层绘制耗时
  u8g2.setBusClock(800000);
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  Serial.println("Booting...");
  Serial.print("Powered By LaoShui @ 2026");

  // 初始化启动界面
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf); // 使用小号英文字体
  u8g2.drawStr(0, 10, "SYSTEM BOOTING...");
  u8g2.drawStr(0, 22, "INITIALIZING HARDWARE");
  u8g2.drawStr(0, 34, "MOUNTING FILESYSTEM");
  u8g2.drawStr(0, 46, "STARTING WLAN MAC...");
  u8g2.sendBuffer();

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(wifiOutputPower);
  WiFi.begin(ssid, password);
  
  int dotCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "SYSTEM BOOTING  [OK]");
    u8g2.drawStr(0, 22, "WLAN MAC INIT   [OK]");
    
    String connStr = "WIFI: " + String(ssid);
    u8g2.drawStr(0, 34, connStr.c_str());
    
    String dots = "";
    for(int i = 0; i < (dotCount % 4); i++) dots += ".";
    String authStr = "DHCP REQ" + dots;
    u8g2.drawStr(0, 46, authStr.c_str());
    
    u8g2.drawStr(0, 60, "AWAITING IP...");
    u8g2.sendBuffer();
    
    Serial.print(".");
    dotCount++;
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  u8g2.clearBuffer();
  u8g2.drawStr(0, 10, "SYSTEM BOOTING  [OK]");
  u8g2.drawStr(0, 22, "WLAN CONNECTED  [OK]");
  String ipStr = "IP:" + WiFi.localIP().toString();
  u8g2.drawStr(0, 34, ipStr.c_str());
  u8g2.drawStr(0, 46, "SYNCING NTP...");
  u8g2.sendBuffer();

  configTime(8 * 3600, 0, "ntp.aliyun.com", "ntp.ntsc.ac.cn", "cn.pool.ntp.org");
  
  // 等待时间同步完成，最多等10秒
  Serial.print("Waiting for NTP time sync ");
  int retry = 0;
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo) && retry < 20) {
    Serial.print(".");
    
    // 更新NTP同步动画
    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "SYSTEM BOOTING  [OK]");
    u8g2.drawStr(0, 22, "WLAN CONNECTED  [OK]");
    u8g2.drawStr(0, 34, ipStr.c_str());
    
    String dots = "";
    for(int i = 0; i < (retry % 4); i++) dots += ".";
    String ntpStr = "SYNC NTP" + dots;
    u8g2.drawStr(0, 46, ntpStr.c_str());
    u8g2.sendBuffer();
    
    delay(500);
    retry++;
  }
  Serial.println(retry < 20 ? " OK" : " Failed");

  u8g2.clearBuffer();
  u8g2.drawStr(0, 10, "SYSTEM BOOTING  [OK]");
  u8g2.drawStr(0, 22, "WLAN CONNECTED  [OK]");
  u8g2.drawStr(0, 34, ipStr.c_str());
  if (retry < 20) {
    u8g2.drawStr(0, 46, "NTP SYNC        [OK]");
  } else {
    u8g2.drawStr(0, 46, "NTP SYNC      [FAIL]");
  }
  u8g2.drawStr(0, 60, "STARTING DAEMON...");
  u8g2.sendBuffer();
  delay(800);

  // 创建后台网络请求任务，运行在核心 0 上 (Arduino 默认运行在核心 1)
  xTaskCreatePinnedToCore(
    fetchNetworkDataTask,   // 任务函数
    "FetchTask",            // 任务名称
    8192,                   // 堆栈大小（网络请求和JSON解析需要较大堆栈）
    NULL,                   // 任务参数
    1,                      // 任务优先级
    &FetchTask,             // 任务句柄
    0                       // 运行核心
  );
}

void fetchCourseData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return;
  }

  int maxRetries = 3; // 课程数据非常重要，增加重试次数
  int currentTry = 0;
  bool success = false;

  while (currentTry < maxRetries && !success) {
    currentTry++;
    HTTPClient http;
    http.setTimeout(5000); // 5秒超时
    http.begin(courseApiUrl);
    int httpCode = http.GET();
    Serial.print("Course GET (Try ");
    Serial.print(currentTry);
    Serial.print("): ");
    Serial.print(courseApiUrl);
    Serial.print(" -> ");
    Serial.println(httpCode);

    if (httpCode <= 0 || httpCode == 500) {
      Serial.println("Course fetch failed or 500, retrying...");
      http.end();
      if (currentTry < maxRetries) delay(1000);
      continue;
    }

    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, payload);
    
    if (err) {
      Serial.print("Course JSON error: ");
      Serial.println(err.c_str());
      http.end();
      if (currentTry < maxRetries) delay(1000);
      continue;
    }

    success = true; // 成功获取并解析

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

    if (doc.containsKey("courses") && doc["courses"].is<JsonArray>()) {
      JsonArray coursesArr = doc["courses"].as<JsonArray>();
      
      if (coursesArr.size() > 0) {
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
        
        // 强制重置滚动状态，让新课表立即显示
        marqueeOffset = -128.0f;
      }
    }

    Serial.print("Loaded ");
    Serial.print(courseCount);
    Serial.println(" courses.");
    http.end();
  }
}

void fetchVoiceHubData() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  int maxRetries = 2; // 最大尝试次数 (包含一次重试)
  int currentTry = 0;
  bool success = false;

  while (currentTry < maxRetries && !success) {
    currentTry++;
    HTTPClient http;
    // 直接请求局域网 HTTP，不需要 WiFiClientSecure
    http.setTimeout(10000); 
    http.begin(voiceHubApiUrl);
    
    int httpCode = http.GET();
    Serial.print("VoiceHub GET (Try ");
    Serial.print(currentTry);
    Serial.print("): ");
    Serial.println(httpCode);
    
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = http.getString();
        
        if (payload.length() == 0) {
           Serial.println("VoiceHub proxy API returned empty payload");
           http.end();
           break; // payload为空通常是服务器返回问题，重试意义不大
        }

        DynamicJsonDocument doc(2048); // 经过服务器过滤的数据很小，2KB 足够了
        DeserializationError err = deserializeJson(doc, payload);
        
        if (!err) {
          if (doc["status"] == "success") {
            bool hasSchedule = false;
            JsonArray itemsArr = doc["data"].as<JsonArray>();
            
            if (itemsArr.size() > 0) {
              hasSchedule = true;
              String targetDate = doc["targetDate"].as<String>();
              
              voiceHubStr = "广播站排期 " + targetDate + ": ";
              
              int index = 1;
              for (JsonObject song : itemsArr) {
                String title = song["title"].as<String>();
                String artist = song["artist"].as<String>();
                String requester = song["requester"].as<String>();
                
                voiceHubStr += "#" + String(index) + " " + title + " - " + artist;
                if (requester.length() > 0) {
                  voiceHubStr += " - " + requester;
                }
                voiceHubStr += "  ";
                index++;
              }
              Serial.print("Loaded VoiceHub for date: ");
              Serial.println(targetDate);
            }
            
            if (!hasSchedule) {
              voiceHubStr = "暂无近期排期  ";
              Serial.println("VoiceHub returned no schedule.");
            }
            voiceHubFetched = true;
            success = true; // 成功解析并处理
          } else {
            Serial.print("VoiceHub proxy logic err: ");
            Serial.println(doc["message"].as<String>());
            success = true; // 业务逻辑错误，不需要重试
          }
        } else {
          Serial.print("VoiceHub proxy JSON err: ");
          Serial.println(err.c_str());
          success = true; // JSON解析错误，不需要重试
        }
      } else {
        Serial.print("VoiceHub proxy HTTP err (Not 200): ");
        Serial.println(httpCode);
        
        // 如果是 500 等服务器错误，允许重试
        if (httpCode >= 500) {
          if (currentTry < maxRetries) {
            Serial.println("Server error, retrying...");
            delay(1000);
          }
        } else {
          success = true; // 非200客户端错误(如404)，通常不需要重试
        }
      }
    } else {
      Serial.print("VoiceHub proxy HTTP err: ");
      Serial.println(httpCode);
      if (currentTry < maxRetries) {
        Serial.println("Retrying VoiceHub fetch...");
        delay(1000); // 重试前稍作等待
      }
    }
    http.end();
  }
}

void updateCurrentCourse(int currentMin) {
  if (courseCount == 0) {
    if (currentCourse == "加载中") {
       // 保持加载中状态，不要覆盖为今日无课
       return;
    }
    currentCourse = "今日无课";
    currentRemainingMin = -1;
    return;
  }

  String newCourse = "无课/放学";
  int newRemaining = -1;

  for (int i = 0; i < courseCount; i++) {
    // 判断是否在某节课中
    if (currentMin >= dailyCourses[i].startMin && currentMin <= dailyCourses[i].endMin) {
      newCourse = dailyCourses[i].name;
      newRemaining = dailyCourses[i].endMin - currentMin - 1;
      break;
    }
    // 判断是否在两节课之间的课间
    if (i < courseCount - 1) {
      if (currentMin > dailyCourses[i].endMin && currentMin < dailyCourses[i+1].startMin) {
        newCourse = "课间 -> " + dailyCourses[i+1].name;
        newRemaining = dailyCourses[i+1].startMin - currentMin - 1;
        break;
      }
    }
    // 早于第一节课
    if (i == 0 && currentMin < dailyCourses[0].startMin) {
      newCourse = "未上课 -> " + dailyCourses[0].name;
      newRemaining = dailyCourses[0].startMin - currentMin - 1;
      break;
    }
  }

  // 检测课程状态是否发生变化，触发动画
  if (currentCourse != "加载中" && currentCourse != newCourse) {
    prevCourseStr = currentCourse;
    animStartTime = millis();
    isAnimating = true;
    currentCourseMarqueeOffset = 0.0f; // 重置当前课程滚动
  }
  
  currentCourse = newCourse;
  currentRemainingMin = newRemaining;
}

void updateDisplay() {
  struct tm timeinfo;
  int currentMin = 0;
  
  if (!getLocalTime(&timeinfo)) {
    currentTimeStr = "时间同步失败";
  } else {
    char timeStr[10];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    currentTimeStr = String(timeStr);
    currentMin = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  }

  updateCurrentCourse(currentMin);

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setCursor(0, 16);
  u8g2.print(currentTimeStr);

  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  
  // 天气信息滚动显示逻辑
  int weatherWidth = u8g2.getUTF8Width(weatherStr.c_str());
  int weatherStartX = 74;
  int maxWeatherWidth = 128 - weatherStartX;
  
  if (weatherWidth > maxWeatherWidth) {
    // 裁剪区域，防止覆盖左边的时间
    u8g2.setClipWindow(weatherStartX, 0, 128, 20);
    
    unsigned long now = millis();
    unsigned long dt = now - lastWeatherMarqueeTime;
    if (lastWeatherMarqueeTime == 0) dt = 0;
    lastWeatherMarqueeTime = now;
    
    weatherMarqueeOffset += (dt / 1000.0f) * 20.0f; // 每秒 20 像素的慢速滚动
    
    if (weatherMarqueeOffset > weatherWidth + 20) {
      weatherMarqueeOffset = -maxWeatherWidth;
    }
    
    u8g2.drawUTF8(weatherStartX - (int)weatherMarqueeOffset, 16, weatherStr.c_str());
    u8g2.setMaxClipWindow(); // 恢复全屏绘制
  } else {
    weatherMarqueeOffset = 0.0f;
    lastWeatherMarqueeTime = 0;
    u8g2.drawUTF8(weatherStartX, 16, weatherStr.c_str());
  }

  u8g2.drawLine(0, 20, 128, 20);

  // 显示剩余时间并计算可用宽度
  int remainWidth = 0;
  if (!isAnimating && currentRemainingMin >= 0) {
    int displayMin = currentRemainingMin;
    if (displayMin > 0) {
      String remainStr = "<" + String(displayMin) + "分钟";
      remainWidth = u8g2.getUTF8Width(remainStr.c_str());
      u8g2.setCursor(128 - remainWidth, 36); // 靠右对齐
      u8g2.print(remainStr);
    }
  }

  // 渲染当前课程 (带截断和滚动)
  u8g2.setCursor(0, 36);
  u8g2.print("当前: ");
  int prefixWidth = u8g2.getUTF8Width("当前: ");
  int maxCourseWidth = 128 - prefixWidth - remainWidth - 4; // 留点边距

  // 设定裁剪区域，防止覆盖剩余时间和左侧"当前："
  u8g2.setClipWindow(prefixWidth, 21, prefixWidth + maxCourseWidth, 43);

  // 处理切换动画与当前课程显示
  int yOffset = 0;
  if (isAnimating) {
    unsigned long elapsed = millis() - animStartTime;
    if (elapsed < 500) { // 500ms 动画时长
      float t = (float)elapsed / 500.0f;
      float easeOut = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
      yOffset = (int)(easeOut * 14.0f);
      
      u8g2.drawUTF8(prefixWidth, 36 - yOffset, prevCourseStr.c_str());
      u8g2.drawUTF8(prefixWidth, 36 + 14 - yOffset, currentCourse.c_str());
    } else {
      isAnimating = false;
      u8g2.drawUTF8(prefixWidth, 36, currentCourse.c_str());
    }
  } else {
    int courseStrWidth = u8g2.getUTF8Width(currentCourse.c_str());
    if (courseStrWidth > maxCourseWidth) {
      // 需要滚动，使用基于增量时间的积分算法
      unsigned long now = millis();
      unsigned long dt = now - lastCurrentCourseMarqueeTime;
      if (lastCurrentCourseMarqueeTime == 0) dt = 0;
      lastCurrentCourseMarqueeTime = now;
      
      // 每秒移动 30 像素
      currentCourseMarqueeOffset += (dt / 1000.0f) * 30.0f;
      
      if (currentCourseMarqueeOffset > courseStrWidth + 20) {
        currentCourseMarqueeOffset = -maxCourseWidth;
      }
      u8g2.drawUTF8(prefixWidth - (int)currentCourseMarqueeOffset, 36, currentCourse.c_str());
    } else {
      // 不需要滚动
      currentCourseMarqueeOffset = 0.0f;
      lastCurrentCourseMarqueeTime = 0;
      u8g2.drawUTF8(prefixWidth, 36, currentCourse.c_str());
    }
  }
  
  u8g2.setMaxClipWindow(); // 恢复全屏绘制

  // 底部滚动显示
  u8g2.drawLine(0, 44, 128, 44);
  
  String currentBottomStr = fullScheduleStr;
  if (voiceHubFetched && voiceHubStr.length() > 0 && voiceHubStr.indexOf("暂无") == -1) {
    currentBottomStr += "        " + voiceHubStr;
  }

  if (currentBottomStr.length() > 0) {
    currentBottomStr += "        ";
    if (currentBottomStr != marqueeRenderStr) {
      marqueeRenderStr = currentBottomStr;
      marqueeTextWidth = u8g2.getUTF8Width(marqueeRenderStr.c_str());
      if (marqueeOffset >= marqueeTextWidth) {
        marqueeOffset = 0;
      }
    }

    if (marqueeTextWidth > 128) {
      unsigned long now = millis();
      unsigned long dt = now - lastMarqueeTime;
      if (lastMarqueeTime == 0) dt = 0;
      lastMarqueeTime = now;
      
      // 每秒移动 60 像素（适中平滑速度）
      marqueeOffset += (dt / 1000.0f) * 60.0f;
      
      if (marqueeOffset >= marqueeTextWidth) {
        marqueeOffset -= marqueeTextWidth; // 防止重置时发生跳变
      }

      int intOffset = (int)marqueeOffset;
      u8g2.drawUTF8(-intOffset, 60, marqueeRenderStr.c_str());
      if (intOffset > 0) {
        u8g2.drawUTF8(-intOffset + marqueeTextWidth, 60, marqueeRenderStr.c_str());
      }
    } else {
      u8g2.drawUTF8(0, 60, currentBottomStr.c_str());
      lastMarqueeTime = 0;
    }
  } else {
    marqueeRenderStr = "";
    marqueeTextWidth = 0;
    marqueeOffset = 0.0f;
    lastMarqueeTime = 0;
    if (currentCourse == "加载中") {
      u8g2.drawUTF8(0, 60, "数据加载中...");
    } else {
      u8g2.drawUTF8(0, 60, "暂无数据");
    }
  }

  u8g2.sendBuffer();
}

void loop() {
  updateDisplay();
  delay(1);
}
