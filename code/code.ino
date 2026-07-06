#include "globals.h"
#include "wifi_config.h"
#include "config.h"
#include "web_handlers.h"
#include "web_handlers.h"
#include "modem.h"
#include "web_handlers.h"
#include "push.h"
#include "web_handlers.h"
#include "sms_process.h"
#include "web_handlers.h"
#include "scheduler.h"

void syncNtpTime() {
  logCaptureLn(String("NTP sync..."));
  configTime(0, 0, "ntp.ntsc.ac.cn", "ntp.aliyun.com", "pool.ntp.org");
  int ntpRetry = 0;
  while (time(nullptr) < 100000 && ntpRetry < 100) {
    delay(1);
    server.handleClient();
    ntpRetry++;
  }
  if (time(nullptr) >= 100000) {
    timeSynced = true;
    logCaptureLn(String("NTP OK"));
    time_t now = time(nullptr);
    logCapture(String("UTC: "));
    logCaptureLn(String(now));
  } else {
    logCaptureLn(String("NTP fail"));
  }
}

void finishNetworkStartup() {
  configValid = isConfigValid();
  syncNtpTime();
  ssl_client.setInsecure();
  digitalWrite(LED_BUILTIN, LOW);

  if (configValid) {
    logCaptureLn(String("Send boot notice"));
    String subject = "开机";
    String body = getSystemOverview();
    sendNotifyAll(subject.c_str(), body.c_str());
  }

}

bool connectWifiCredential(const String& ssid, const String& pass, const String& label) {
  if (ssid.length() == 0) return false;

  logCaptureLn(String("WiFi try ") + label + ": " + ssid);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setScanMethod(WIFI_FAST_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  if (pass.length()) WiFi.begin(ssid.c_str(), pass.c_str());
  else WiFi.begin(ssid.c_str());

  unsigned long start = millis();
  const unsigned long WIFI_TIMEOUT = 15000;
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT) {
    blink_short(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    logCaptureLn(String("WiFi OK"));
    logCapture(String("IP: "));
    logCaptureLn(WiFi.localIP().toString());
    logCapture(String("RSSI: "));
    logCaptureLn(String(WiFi.RSSI()) + " dBm");
    inApConfigMode = false;
    return true;
  }

  logCaptureLn(String("WiFi fail: ") + ssid);
  WiFi.disconnect(false);
  delay(200);
  return false;
}

bool connectSavedWifis() {
  return connectWifiCredential(config.wifiSsid, config.wifiPass, "main") ||
         connectWifiCredential(config.wifiBackupSsid1, config.wifiBackupPass1, "A") ||
         connectWifiCredential(config.wifiBackupSsid2, config.wifiBackupPass2, "B");
}

void enterApConfigMode() {
  logCaptureLn(String("AP mode"));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  String mac = WiFi.macAddress();
  int n = WiFi.scanNetworks();
  scannedWifiListHtml = "";
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    String optionValue = ssid;
    optionValue.replace("&", "&amp;");
    optionValue.replace("\"", "&quot;");
    String optionText = optionValue;
    scannedWifiListHtml += "<option value=\"" + optionValue + "\">" + optionText + "</option>";
  }
  if (n <= 0) {
    scannedWifiListHtml = "<option value=\"\" disabled>未扫描到WiFi</option>";
  }
  WiFi.scanDelete();

  WiFi.mode(WIFI_AP);
  delay(500);
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  String macSuffix = mac.substring(mac.length() - 5);
  macSuffix.replace(":", "");
  String apSsid = "SMS_Forwarder_" + macSuffix;
  bool apSuccess = WiFi.softAP(apSsid.c_str(), "12345678");
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  if (apSuccess) {
    logCaptureLn(String("AP: ") + apSsid);
    logCaptureLn(String("AP ") + WiFi.softAPIP().toString());
  } else {
    logCaptureLn(String("AP fail"));
  }
  inApConfigMode = true;
}

bool attemptWifiConnection() {
  WiFi.softAPdisconnect(true);
  inApConfigMode = false;
  if (connectSavedWifis()) {
    finishNetworkStartup();
    return true;
  }
  enterApConfigMode();
  return false;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.begin(115200);
  // 缩短初始化延时，WiFi连接会处理自己的超时
  delay(200);
  Serial1.begin(115200, SERIAL_8N1, RXD, TXD);
  Serial1.setRxBufferSize(SERIAL_BUFFER_SIZE);
  while (Serial1.available()) Serial1.read();
  pinMode(MODEM_EN_PIN, OUTPUT);
  digitalWrite(MODEM_EN_PIN, HIGH);
  initConcatBuffer();
  loadConfig();
  configValid = false;

  // ---- WiFi 连接优化 ----
  // WiFi 在 HTTP 路由注册后连接，连接失败会进入热点配网模式。

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/save_wifi", HTTP_POST, handleSaveWifi);
  server.on("/tools", handleRoot);
  server.on("/sms", handleRoot);
  server.on("/sendsms", HTTP_POST, handleSendSms);
  server.on("/ping", HTTP_POST, handlePing);
  server.on("/query", handleQuery);
  server.on("/flight", handleFlightMode);
  server.on("/at", handleATCommand);
  server.on("/log", handleLog);
  server.on("/modem", handleModem);
  server.on("/wifi", handleWifi);
  server.onNotFound(handleRoot);

  bool wifiConnected = false;
  if (config.wifiSsid.length() == 0 &&
      config.wifiBackupSsid1.length() == 0 &&
      config.wifiBackupSsid2.length() == 0) {
    logCaptureLn(String("No WiFi"));
    enterApConfigMode();
  } else if (connectSavedWifis()) {
    wifiConnected = true;
  } else {
    enterApConfigMode();
  }

  server.begin();
  logCaptureLn(String("HTTP OK"));

  if (wifiConnected) {
    finishNetworkStartup();
  }
}

void loop() {
  server.handleClient();
  if (wifiConfigSubmitted && millis() - wifiConfigSubmittedTime > 1000) {
    wifiConfigSubmitted = false;
    attemptWifiConnection();
  }
  static unsigned long lastWifiRecoveryTime = 0;
  if (!inApConfigMode && WiFi.status() != WL_CONNECTED && millis() - lastWifiRecoveryTime > 60000) {
    lastWifiRecoveryTime = millis();
    logCaptureLn(String("WiFi lost, retry"));
    if (!connectSavedWifis()) {
      enterApConfigMode();
    }
  }
  if (!inApConfigMode && !configValid) {
    if (millis() - lastPrintTime >= 1000) {
      lastPrintTime = millis();
      logCaptureLn(String("⚠️ 请访问 " + getDeviceUrl() + " 配置系统参数"));
    }
  }
  checkConcatTimeout();
  if (Serial.available()) Serial1.write(Serial.read());
  checkSerial1URC();
  static bool modemInitStarted = false;
  if (!modemInitStarted && millis() > 8000) {
    modemInitStarted = true;
    modemInit();
  }
  if (modemInitStarted) modemBackgroundTask();
  checkScheduledSms();
  checkScheduledNotify();
}
