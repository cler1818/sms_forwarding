#include "config.h"
#include "web_handlers.h"
#include "wifi_config.h"

// 保存配置到NVS
void saveConfig() {
  preferences.begin("sms_config", false);
  preferences.putString("wifiSsid", config.wifiSsid);
  preferences.putString("wifiPass", config.wifiPass);
  preferences.putString("wifiBakSsid1", config.wifiBackupSsid1);
  preferences.putString("wifiBakPass1", config.wifiBackupPass1);
  preferences.putString("wifiBakSsid2", config.wifiBackupSsid2);
  preferences.putString("wifiBakPass2", config.wifiBackupPass2);
  preferences.putString("smtpServer", config.smtpServer);
  preferences.putInt("smtpPort", config.smtpPort);
  preferences.putString("smtpUser", config.smtpUser);
  preferences.putString("smtpPass", config.smtpPass);
  preferences.putString("smtpSendTo", config.smtpSendTo);
  preferences.putString("smtpServer2", config.smtpServer2);
  preferences.putInt("smtpPort2", config.smtpPort2);
  preferences.putString("smtpUser2", config.smtpUser2);
  preferences.putString("smtpPass2", config.smtpPass2);
  preferences.putString("smtpSendTo2", config.smtpSendTo2);
  preferences.putString("smtpServer3", config.smtpServer3);
  preferences.putInt("smtpPort3", config.smtpPort3);
  preferences.putString("smtpUser3", config.smtpUser3);
  preferences.putString("smtpPass3", config.smtpPass3);
  preferences.putString("smtpSendTo3", config.smtpSendTo3);
  preferences.putString("adminPhone", config.adminPhone);
  preferences.putString("adminSmsWL", config.adminSmsWhitelist);
  preferences.putString("localPhone", config.localPhone);
  preferences.putString("webUser", config.webUser);
  preferences.putString("webPass", config.webPass);
  preferences.putString("numBlkList", config.numberBlackList);
  preferences.putBool("schedEn", config.scheduledSms.enabled);
  preferences.putUChar("schedType", (uint8_t)config.scheduledSms.type);
  preferences.putString("schedPhone", config.scheduledSms.phone);
  preferences.putString("schedContent", config.scheduledSms.content);
  preferences.putUChar("schedHour", config.scheduledSms.hour);
  preferences.putUChar("schedMinute", config.scheduledSms.minute);
  preferences.putUChar("schedWeekday", config.scheduledSms.weekday);
  preferences.putUChar("schedMonthDay", config.scheduledSms.monthDay);
  preferences.putUInt("schedLastRun", config.scheduledSms.lastRunDayKey);
  preferences.putBool("notifyEn", config.scheduledNotify.enabled);
  preferences.putUChar("notifyType", (uint8_t)config.scheduledNotify.type);
  preferences.putString("notifyContent", config.scheduledNotify.content);
  preferences.putUChar("notifyHour", config.scheduledNotify.hour);
  preferences.putUChar("notifyMinute", config.scheduledNotify.minute);
  preferences.putUChar("notifyWeekday", config.scheduledNotify.weekday);
  preferences.putUChar("notifyMonthDay", config.scheduledNotify.monthDay);
  preferences.putUInt("notifyLastRun", config.scheduledNotify.lastRunDayKey);
  preferences.putBool("modRstEn", config.scheduledModemRestart.enabled);
  preferences.putUChar("modRstType", (uint8_t)config.scheduledModemRestart.type);
  preferences.putUChar("modRstMode", (uint8_t)config.scheduledModemRestart.mode);
  preferences.putUChar("modRstHour", config.scheduledModemRestart.hour);
  preferences.putUChar("modRstMin", config.scheduledModemRestart.minute);
  preferences.putUChar("modRstWeek", config.scheduledModemRestart.weekday);
  preferences.putUChar("modRstDay", config.scheduledModemRestart.monthDay);
  preferences.putUInt("modRstLast", config.scheduledModemRestart.lastRunDayKey);
  
  // 保存推送通道配置
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    String prefix = "push" + String(i);
    preferences.putBool((prefix + "en").c_str(), config.pushChannels[i].enabled);
    preferences.putUChar((prefix + "type").c_str(), (uint8_t)config.pushChannels[i].type);
    preferences.putString((prefix + "url").c_str(), config.pushChannels[i].url);
    preferences.putString((prefix + "name").c_str(), config.pushChannels[i].name);
    preferences.putString((prefix + "k1").c_str(), config.pushChannels[i].key1);
    preferences.putString((prefix + "k2").c_str(), config.pushChannels[i].key2);
    preferences.putString((prefix + "body").c_str(), config.pushChannels[i].customBody);
  }
  
  preferences.end();
  logCaptureLn(String("配置已保存"));
}

// 从NVS加载配置
void loadConfig() {
  preferences.begin("sms_config", true);
  config.wifiSsid = preferences.getString("wifiSsid", WIFI_SSID);
  config.wifiPass = preferences.getString("wifiPass", WIFI_PASS);
  config.wifiBackupSsid1 = preferences.getString("wifiBakSsid1", "");
  config.wifiBackupPass1 = preferences.getString("wifiBakPass1", "");
  config.wifiBackupSsid2 = preferences.getString("wifiBakSsid2", "");
  config.wifiBackupPass2 = preferences.getString("wifiBakPass2", "");
  config.smtpServer = preferences.getString("smtpServer", "");
  config.smtpPort = preferences.getInt("smtpPort", 465);
  config.smtpUser = preferences.getString("smtpUser", "");
  config.smtpPass = preferences.getString("smtpPass", "");
  config.smtpSendTo = preferences.getString("smtpSendTo", "");
  config.smtpServer2 = preferences.getString("smtpServer2", "");
  config.smtpPort2 = preferences.getInt("smtpPort2", 465);
  config.smtpUser2 = preferences.getString("smtpUser2", "");
  config.smtpPass2 = preferences.getString("smtpPass2", "");
  config.smtpSendTo2 = preferences.getString("smtpSendTo2", "");
  config.smtpServer3 = preferences.getString("smtpServer3", "");
  config.smtpPort3 = preferences.getInt("smtpPort3", 465);
  config.smtpUser3 = preferences.getString("smtpUser3", "");
  config.smtpPass3 = preferences.getString("smtpPass3", "");
  config.smtpSendTo3 = preferences.getString("smtpSendTo3", "");
  config.adminPhone = preferences.getString("adminPhone", "");
  config.adminSmsWhitelist = preferences.getString("adminSmsWL", "");
  config.localPhone = preferences.getString("localPhone", "");
  config.webUser = preferences.getString("webUser", DEFAULT_WEB_USER);
  config.webPass = preferences.getString("webPass", DEFAULT_WEB_PASS);
  config.numberBlackList = preferences.getString("numBlkList", "");
  config.scheduledSms.enabled = preferences.getBool("schedEn", false);
  config.scheduledSms.type = (ScheduledSmsType)preferences.getUChar("schedType", SCHEDULE_SMS_DAILY);
  config.scheduledSms.phone = preferences.getString("schedPhone", "");
  config.scheduledSms.content = preferences.getString("schedContent", "");
  config.scheduledSms.hour = preferences.getUChar("schedHour", 9);
  config.scheduledSms.minute = preferences.getUChar("schedMinute", 0);
  config.scheduledSms.weekday = preferences.getUChar("schedWeekday", 1);
  config.scheduledSms.monthDay = preferences.getUChar("schedMonthDay", 1);
  config.scheduledSms.lastRunDayKey = preferences.getUInt("schedLastRun", 0);
  config.scheduledNotify.enabled = preferences.getBool("notifyEn", false);
  config.scheduledNotify.type = (ScheduledSmsType)preferences.getUChar("notifyType", SCHEDULE_SMS_DAILY);
  config.scheduledNotify.content = preferences.getString("notifyContent", "");
  config.scheduledNotify.hour = preferences.getUChar("notifyHour", 9);
  config.scheduledNotify.minute = preferences.getUChar("notifyMinute", 0);
  config.scheduledNotify.weekday = preferences.getUChar("notifyWeekday", 1);
  config.scheduledNotify.monthDay = preferences.getUChar("notifyMonthDay", 1);
  config.scheduledNotify.lastRunDayKey = preferences.getUInt("notifyLastRun", 0);
  config.scheduledModemRestart.enabled = preferences.getBool("modRstEn", false);
  config.scheduledModemRestart.type = (ScheduledSmsType)preferences.getUChar("modRstType", SCHEDULE_SMS_DAILY);
  config.scheduledModemRestart.mode = (ScheduledModemRestartMode)preferences.getUChar("modRstMode", MODEM_RESTART_SOFT);
  config.scheduledModemRestart.hour = preferences.getUChar("modRstHour", 4);
  config.scheduledModemRestart.minute = preferences.getUChar("modRstMin", 0);
  config.scheduledModemRestart.weekday = preferences.getUChar("modRstWeek", 1);
  config.scheduledModemRestart.monthDay = preferences.getUChar("modRstDay", 1);
  config.scheduledModemRestart.lastRunDayKey = preferences.getUInt("modRstLast", 0);

  if (config.scheduledSms.type > SCHEDULE_SMS_MONTHLY) config.scheduledSms.type = SCHEDULE_SMS_DAILY;
  if (config.scheduledSms.hour > 23) config.scheduledSms.hour = 9;
  if (config.scheduledSms.minute > 59) config.scheduledSms.minute = 0;
  if (config.scheduledSms.weekday < 1 || config.scheduledSms.weekday > 7) config.scheduledSms.weekday = 1;
  if (config.scheduledSms.monthDay < 1 || config.scheduledSms.monthDay > 31) config.scheduledSms.monthDay = 1;
  if (config.scheduledNotify.type > SCHEDULE_SMS_MONTHLY) config.scheduledNotify.type = SCHEDULE_SMS_DAILY;
  if (config.scheduledNotify.hour > 23) config.scheduledNotify.hour = 9;
  if (config.scheduledNotify.minute > 59) config.scheduledNotify.minute = 0;
  if (config.scheduledNotify.weekday < 1 || config.scheduledNotify.weekday > 7) config.scheduledNotify.weekday = 1;
  if (config.scheduledNotify.monthDay < 1 || config.scheduledNotify.monthDay > 31) config.scheduledNotify.monthDay = 1;
  if (config.scheduledModemRestart.type > SCHEDULE_SMS_MONTHLY) config.scheduledModemRestart.type = SCHEDULE_SMS_DAILY;
  if (config.scheduledModemRestart.mode > MODEM_RESTART_HARD) config.scheduledModemRestart.mode = MODEM_RESTART_SOFT;
  if (config.scheduledModemRestart.hour > 23) config.scheduledModemRestart.hour = 4;
  if (config.scheduledModemRestart.minute > 59) config.scheduledModemRestart.minute = 0;
  if (config.scheduledModemRestart.weekday < 1 || config.scheduledModemRestart.weekday > 7) config.scheduledModemRestart.weekday = 1;
  if (config.scheduledModemRestart.monthDay < 1 || config.scheduledModemRestart.monthDay > 31) config.scheduledModemRestart.monthDay = 1;
  
  // 加载推送通道配置
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    String prefix = "push" + String(i);
    config.pushChannels[i].enabled = preferences.getBool((prefix + "en").c_str(), false);
    config.pushChannels[i].type = (PushType)preferences.getUChar((prefix + "type").c_str(), PUSH_TYPE_POST_JSON);
    config.pushChannels[i].url = preferences.getString((prefix + "url").c_str(), "");
    config.pushChannels[i].name = preferences.getString((prefix + "name").c_str(), "通道" + String(i + 1));
    config.pushChannels[i].key1 = preferences.getString((prefix + "k1").c_str(), "");
    config.pushChannels[i].key2 = preferences.getString((prefix + "k2").c_str(), "");
    config.pushChannels[i].customBody = preferences.getString((prefix + "body").c_str(), "");
  }
  
  // 兼容旧配置：如果有旧的httpUrl配置，迁移到第一个通道
  String oldHttpUrl = preferences.getString("httpUrl", "");
  if (oldHttpUrl.length() > 0 && !config.pushChannels[0].enabled) {
    config.pushChannels[0].enabled = true;
    config.pushChannels[0].url = oldHttpUrl;
    config.pushChannels[0].type = preferences.getUChar("barkMode", 0) != 0 ? PUSH_TYPE_BARK : PUSH_TYPE_POST_JSON;
    config.pushChannels[0].name = "迁移通道";
    logCaptureLn(String("已迁移旧HTTP配置到推送通道1"));
  }
  
  preferences.end();
  logCaptureLn(String("配置已加载"));
}

// 检查推送通道是否有效配置
bool isPushChannelValid(const PushChannel& ch) {
  if (!ch.enabled) return false;

  switch (ch.type) {
    case PUSH_TYPE_POST_JSON:
    case PUSH_TYPE_BARK:
    case PUSH_TYPE_GET:
    case PUSH_TYPE_DINGTALK:
    case PUSH_TYPE_FEISHU:
    case PUSH_TYPE_CUSTOM:
      return ch.url.length() > 0;
    case PUSH_TYPE_PUSHPLUS:
    case PUSH_TYPE_SERVERCHAN:
      return ch.key1.length() > 0;
    case PUSH_TYPE_GOTIFY:
      return ch.url.length() > 0 && ch.key1.length() > 0;
    case PUSH_TYPE_TELEGRAM:
      return ch.key1.length() > 0 && ch.key2.length() > 0;
    default:
      return false;
  }
}

bool isConfigValid() {
  bool emailValid = config.smtpServer.length() > 0 &&
                    config.smtpUser.length() > 0 &&
                    config.smtpPass.length() > 0 &&
                    config.smtpSendTo.length() > 0;
  emailValid = emailValid ||
               (config.smtpServer2.length() > 0 &&
                config.smtpUser2.length() > 0 &&
                config.smtpPass2.length() > 0 &&
                config.smtpSendTo2.length() > 0) ||
               (config.smtpServer3.length() > 0 &&
                config.smtpUser3.length() > 0 &&
                config.smtpPass3.length() > 0 &&
                config.smtpSendTo3.length() > 0);

  bool pushValid = false;
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (isPushChannelValid(config.pushChannels[i])) {
      pushValid = true;
      break;
    }
  }

  return emailValid || pushValid;
}

// 获取当前设备URL
String getDeviceUrl() {
  return "http://" + WiFi.localIP().toString() + "/";
}
