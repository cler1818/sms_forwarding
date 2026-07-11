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

volatile bool wifiDisconnectEventPending = false;
volatile uint8_t wifiDisconnectReason = 0;
volatile bool wifiGotIpEventPending = false;
bool networkStartupDone = false;
unsigned long networkStartupAt = 0;
bool wifiRecoveryIncident = false;
bool wifiRecoveryNoticePending = false;
unsigned long wifiDisconnectedAt = 0;
unsigned long wifiRecoveredAt = 0;
unsigned long lastWifiRecoveryNoticeAt = 0;
uint8_t wifiRecoveryReason = 0;

const unsigned long WIFI_RECOVERY_STABLE_MS = 60000;
const unsigned long WIFI_RECOVERY_NOTICE_COOLDOWN_MS = 600000;

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    wifiDisconnectReason = info.wifi_sta_disconnected.reason;
    wifiDisconnectEventPending = true;
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    wifiGotIpEventPending = true;
  }
}

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

  networkStartupDone = true;
  networkStartupAt = millis();
  wifiRecoveryIncident = false;
  wifiRecoveryNoticePending = false;

}

void checkWifiRecoveryNotice() {
  if (!wifiRecoveryNoticePending || WiFi.status() != WL_CONNECTED) return;
  if (millis() - wifiRecoveredAt < WIFI_RECOVERY_STABLE_MS) return;

  wifiRecoveryNoticePending = false;
  wifiRecoveryIncident = false;

  if (lastWifiRecoveryNoticeAt != 0 &&
      millis() - lastWifiRecoveryNoticeAt < WIFI_RECOVERY_NOTICE_COOLDOWN_MS) {
    logCaptureLn(String("WiFi recovery notice suppressed by cooldown"));
    return;
  }

  unsigned long offlineSeconds = (wifiRecoveredAt - wifiDisconnectedAt) / 1000;
  String body = "WiFi连接曾中断，现已恢复并稳定在线。\n";
  body += "WiFi：" + WiFi.SSID() + "\n";
  body += "IP：" + WiFi.localIP().toString() + "\n";
  body += "信号：" + String(WiFi.RSSI()) + " dBm\n";
  body += "BSSID：" + WiFi.BSSIDstr() + "\n";
  body += "信道：" + String(WiFi.channel()) + "\n";
  body += "离线时长：约 " + String(offlineSeconds) + " 秒\n";
  body += "断线原因代码：" + String(wifiRecoveryReason) + "\n";
  body += "说明：连接恢复后已连续稳定 60 秒。";

  logCaptureLn(String("WiFi stable, sending recovery notice"));
  sendNotifyAll("WiFi断线后已重新上线", body.c_str());
  lastWifiRecoveryNoticeAt = millis();
}

bool connectWifiCredential(const String& ssid, const String& pass, const String& label) {
  if (ssid.length() == 0) return false;

  logCaptureLn(String("WiFi try ") + label + ": " + ssid);
  WiFi.mode(inApConfigMode ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  if (pass.length()) WiFi.begin(ssid.c_str(), pass.c_str());
  else WiFi.begin(ssid.c_str());

  unsigned long start = millis();
  const unsigned long WIFI_TIMEOUT = 15000;
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT) {
    blink_short(200);
    server.handleClient();
  }

  if (WiFi.status() == WL_CONNECTED) {
    logCaptureLn(String("WiFi OK"));
    logCapture(String("IP: "));
    logCaptureLn(WiFi.localIP().toString());
    logCapture(String("RSSI: "));
    logCaptureLn(String(WiFi.RSSI()) + " dBm");
    logCaptureLn(String("BSSID: ") + WiFi.BSSIDstr() + ", channel: " + String(WiFi.channel()));
    inApConfigMode = false;
    return true;
  }

  logCaptureLn(String("WiFi fail: ") + ssid);
  WiFi.disconnect(false);
  delay(200);
  return false;
}

bool refreshWifiScanList() {
  WiFi.scanDelete();
  delay(100);
  int n = WiFi.scanNetworks(false, true);
  if (n < 0) {
    logCaptureLn(String("WiFi scan failed: ") + String(n));
    scannedWifiListHtml = "<option value=\"\" disabled>扫描失败，请重试</option>";
    return false;
  }

  scannedWifiListHtml = "";
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    String optionValue = ssid;
    optionValue.replace("&", "&amp;");
    optionValue.replace("\"", "&quot;");
    optionValue.replace("<", "&lt;");
    optionValue.replace(">", "&gt;");
    optionValue.replace("'", "&#39;");
    String optionText = optionValue + " (" + String(WiFi.RSSI(i)) + " dBm)";
    scannedWifiListHtml += "<option value=\"" + optionValue + "\">" + optionText + "</option>";
  }
  if (n == 0) {
    scannedWifiListHtml = "<option value=\"\" disabled>未扫描到WiFi，可手动输入</option>";
  }
  logCaptureLn(String("WiFi scan found: ") + String(n));
  WiFi.scanDelete();
  return n > 0;
}

bool connectSavedWifis() {
  return connectWifiCredential(config.wifiSsid, config.wifiPass, "main") ||
         connectWifiCredential(config.wifiBackupSsid1, config.wifiBackupPass1, "A") ||
         connectWifiCredential(config.wifiBackupSsid2, config.wifiBackupPass2, "B");
}

void enterApConfigMode() {
  logCaptureLn(String("AP mode"));
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(false, false);
  delay(100);

  String mac = WiFi.macAddress();
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  String macSuffix = mac.substring(mac.length() - 5);
  macSuffix.replace(":", "");
  String apSsid = "SMS_Forwarder_" + macSuffix;
  bool apSuccess = WiFi.softAP(apSsid.c_str(), "12345678");
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  if (apSuccess) {
    logCaptureLn(String("AP: ") + apSsid);
    logCaptureLn(String("AP ") + WiFi.softAPIP().toString());
  } else {
    logCaptureLn(String("AP fail"));
  }
  inApConfigMode = true;
  refreshWifiScanList();
}

bool attemptWifiConnection() {
  if (connectSavedWifis()) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    inApConfigMode = false;
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
  WiFi.onEvent(onWiFiEvent);
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
  server.on("/wifi_scan", handleWifiScan);
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
  if (wifiDisconnectEventPending) {
    wifiDisconnectEventPending = false;
    logCaptureLn(String("WiFi disconnected, reason: ") + String(wifiDisconnectReason));
    if (networkStartupDone && millis() - networkStartupAt > 30000) {
      if (!wifiRecoveryIncident) wifiDisconnectedAt = millis();
      wifiRecoveryIncident = true;
      wifiRecoveryNoticePending = false;
      wifiRecoveryReason = wifiDisconnectReason;
    }
  }
  if (wifiGotIpEventPending) {
    wifiGotIpEventPending = false;
    logCaptureLn(String("WiFi got IP: ") + WiFi.localIP().toString());
    if (networkStartupDone && wifiRecoveryIncident) {
      wifiRecoveredAt = millis();
      wifiRecoveryNoticePending = true;
      logCaptureLn(String("WiFi recovered, waiting 60s stable before notice"));
    }
  }
  if (wifiConfigSubmitted && millis() - wifiConfigSubmittedTime > 1000) {
    wifiConfigSubmitted = false;
    attemptWifiConnection();
  }
  static unsigned long lastWifiRecoveryTime = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiRecoveryTime > 60000) {
    lastWifiRecoveryTime = millis();
    logCaptureLn(String(inApConfigMode ? "WiFi AP+STA background retry" : "WiFi lost, retry"));
    bool wasApMode = inApConfigMode;
    if (connectSavedWifis()) {
      if (wasApMode) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
      }
      inApConfigMode = false;
      if (!networkStartupDone) finishNetworkStartup();
    } else if (!wasApMode) {
      enterApConfigMode();
    }
    lastWifiRecoveryTime = millis();
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
  checkWifiRecoveryNotice();
}
