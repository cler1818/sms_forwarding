#include "sms_process.h"
#include "web_handlers.h"
#include "modem.h"
#include "push.h"
#include "config.h"

// 初始化长短信缓存
void initConcatBuffer() {
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    concatBuffer[i].inUse = false;
    concatBuffer[i].receivedParts = 0;
    for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
      concatBuffer[i].parts[j].valid = false;
      concatBuffer[i].parts[j].pushed = false;
      concatBuffer[i].parts[j].text = "";
    }
  }
}

// 查找或创建长短信缓存槽位
int findOrCreateConcatSlot(int refNumber, const char* sender, int totalParts) {
  // 先查找是否已存在
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    if (concatBuffer[i].inUse && 
        concatBuffer[i].refNumber == refNumber &&
        concatBuffer[i].sender.equals(sender)) {
      return i;
    }
  }
  
  // 查找空闲槽位
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    if (!concatBuffer[i].inUse) {
      concatBuffer[i].inUse = true;
      concatBuffer[i].refNumber = refNumber;
      concatBuffer[i].sender = String(sender);
      concatBuffer[i].totalParts = totalParts;
      concatBuffer[i].receivedParts = 0;
      concatBuffer[i].firstPartTime = millis();
      for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
        concatBuffer[i].parts[j].valid = false;
        concatBuffer[i].parts[j].pushed = false;
        concatBuffer[i].parts[j].text = "";
      }
      return i;
    }
  }
  
  // 没有空闲槽位，查找最老的槽位覆盖
  int oldestSlot = 0;
  unsigned long oldestTime = concatBuffer[0].firstPartTime;
  for (int i = 1; i < MAX_CONCAT_MESSAGES; i++) {
    if (concatBuffer[i].firstPartTime < oldestTime) {
      oldestTime = concatBuffer[i].firstPartTime;
      oldestSlot = i;
    }
  }
  
  // 覆盖最老的槽位
  logCaptureLn(String("⚠️ 长短信缓存已满，覆盖最老的槽位"));
  concatBuffer[oldestSlot].inUse = true;
  concatBuffer[oldestSlot].refNumber = refNumber;
  concatBuffer[oldestSlot].sender = String(sender);
  concatBuffer[oldestSlot].totalParts = totalParts;
  concatBuffer[oldestSlot].receivedParts = 0;
  concatBuffer[oldestSlot].firstPartTime = millis();
  for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
    concatBuffer[oldestSlot].parts[j].valid = false;
    concatBuffer[oldestSlot].parts[j].pushed = false;
    concatBuffer[oldestSlot].parts[j].text = "";
  }
  return oldestSlot;
}

// 合并长短信各分段
String assembleConcatSms(int slot) {
  String result = "";
  for (int i = 0; i < concatBuffer[slot].totalParts; i++) {
    if (concatBuffer[slot].parts[i].valid) {
      result += concatBuffer[slot].parts[i].text;
    }
  }
  return result;
}

static bool isConcatComplete(int slot) {
  return concatBuffer[slot].receivedParts >= concatBuffer[slot].totalParts;
}

static String localNotifySubject(const char* prefix) {
  String s = prefix;
  if (config.localPhone.length()) s += config.localPhone;
  return s;
}

static void forwardConcatPart(const char* sender, const char* text, const char* timestamp, int partNumber, int totalParts) {
  String body = "长短信分段 ";
  body += String(partNumber) + "/" + String(totalParts) + "，可能不完整\n";
  body += text;
  sendSMSToServer(sender, body.c_str(), timestamp);
  String mailBody = buildPushMessage(sender, body.c_str(), timestamp);
  String subject = localNotifySubject("短信通知");
  sendEmailNotification(subject.c_str(), mailBody.c_str());
}

static void forwardPendingConcatParts(int slot) {
  for (int i = 0; i < concatBuffer[slot].totalParts; i++) {
    if (concatBuffer[slot].parts[i].valid && !concatBuffer[slot].parts[i].pushed) {
      forwardConcatPart(concatBuffer[slot].sender.c_str(),
                        concatBuffer[slot].parts[i].text.c_str(),
                        concatBuffer[slot].timestamp.c_str(),
                        i + 1,
                        concatBuffer[slot].totalParts);
      concatBuffer[slot].parts[i].pushed = true;
    }
  }
}

// 清空长短信槽位
void clearConcatSlot(int slot) {
  concatBuffer[slot].inUse = false;
  concatBuffer[slot].receivedParts = 0;
  concatBuffer[slot].sender = "";
  concatBuffer[slot].timestamp = "";
  for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
    concatBuffer[slot].parts[j].valid = false;
    concatBuffer[slot].parts[j].pushed = false;
    concatBuffer[slot].parts[j].text = "";
  }
}

// 检查长短信超时并转发
void checkConcatTimeout() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    if (concatBuffer[i].inUse) {
      if (isConcatComplete(i)) {
        String fullText = assembleConcatSms(i);
        processSmsContent(concatBuffer[i].sender.c_str(), fullText.c_str(), concatBuffer[i].timestamp.c_str());
        clearConcatSlot(i);
      } else if (now - concatBuffer[i].firstPartTime >= 5000) {
        forwardPendingConcatParts(i);
      }
      if (concatBuffer[i].inUse && now - concatBuffer[i].firstPartTime >= CONCAT_TIMEOUT_MS) {
        logCaptureLn(String("⏰ 长短信超时，强制转发不完整消息"));
        logCaptureF("  参考号: %d, 已收到: %d/%d\n", 
                      concatBuffer[i].refNumber,
                      concatBuffer[i].receivedParts,
                      concatBuffer[i].totalParts);
        
        logCaptureLn(String("长短信未收齐，已清理缓存"));
        
        // 清空槽位
        clearConcatSlot(i);
      }
    }
  }
}

// 读取串口一行（含回车换行），返回行字符串，无新行时返回空
String readSerialLine(HardwareSerial& port) {
  static char lineBuf[SERIAL_BUFFER_SIZE];
  static int linePos = 0;

  while (port.available()) {
    char c = port.read();
    if (c == '\n') {
      lineBuf[linePos] = 0;
      String res = String(lineBuf);
      linePos = 0;
      return res;
    } else if (c != '\r') {  // 跳过\r
      if (linePos < SERIAL_BUFFER_SIZE - 1)
        lineBuf[linePos++] = c;
      else
        linePos = 0;  //超长报错保护，重头计
    }
  }
  return "";
}

// 检查字符串是否为有效的十六进制PDU数据
bool isHexString(const String& str) {
  if (str.length() == 0) return false;
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

// 检查发送者是否在号码黑名单中
bool isInNumberBlackList(const char* sender) {
  if (config.numberBlackList.length() == 0) return false;

  String originalSender = String(sender);
  bool has86 = originalSender.startsWith("+86");
  String strippedSender = has86 ? originalSender.substring(3) : "";

  int listLen = (int)config.numberBlackList.length();

  int start = 0;
  while (start <= listLen) {
    int end = config.numberBlackList.indexOf('\n', start);
    if (end == -1) end = listLen;

    String line = config.numberBlackList.substring(start, end);
    line.trim();

    if (line.length() > 0 && (line.equals(originalSender) || (has86 && line.equals(strippedSender)))) {
      return true;
    }

    start = end + 1;
  }

  return false;
}

static String normalizePhone(String phone) {
  phone.trim();
  if (phone.startsWith("+86")) phone = phone.substring(3);
  return phone;
}

bool isAdmin(const char* sender) {
  String senderStr = normalizePhone(String(sender));
  String list = config.adminSmsWhitelist;
  if (list.length() == 0) list = config.adminPhone;

  int start = 0;
  int count = 0;
  while (start <= (int)list.length() && count < 10) {
    int end = list.indexOf('\n', start);
    if (end == -1) end = list.length();
    String line = normalizePhone(list.substring(start, end));
    if (line.length() > 0) {
      count++;
      if (line.equals(senderStr)) return true;
    }
    start = end + 1;
  }
  return false;
}

struct RemoteConfigKV {
  String key[48];
  String value[48];
  int count;
  String command;
};

static int splitKeyLine(const String& line) {
  int p = line.indexOf("：");
  int q = line.indexOf(":");
  if (p < 0) return q;
  if (q < 0) return p;
  return p < q ? p : q;
}

static bool parseRemoteConfig(const String& cmd, RemoteConfigKV& kv) {
  kv.count = 0;
  kv.command = "";
  String body = cmd;
  body.trim();
  if (body.startsWith("配置：")) body = body.substring(String("配置：").length());
  else if (body.startsWith("配置:")) body = body.substring(String("配置:").length());
  else if (body.startsWith("CONFIG")) body = body.substring(String("CONFIG").length());
  else return false;
  body.trim();

  int start = 0;
  while (start <= (int)body.length() && kv.count < 48) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    String line = body.substring(start, end);
    line.trim();
    if (line.length()) {
      int pos = splitKeyLine(line);
      if (pos > 0) {
        kv.key[kv.count] = line.substring(0, pos);
        kv.value[kv.count] = line.substring(pos + (line.charAt(pos) == ':' ? 1 : String("：").length()));
        kv.key[kv.count].trim();
        kv.value[kv.count].trim();
        kv.count++;
      } else if (kv.command.length() == 0) {
        kv.command = line;
      }
    }
    start = end + 1;
  }
  return body.length() > 0;
}

static String kvGet(const RemoteConfigKV& kv, const char* a, const char* b = nullptr, const char* c = nullptr, const char* d = nullptr) {
  for (int i = 0; i < kv.count; i++) {
    if (kv.key[i] == a || (b && kv.key[i] == b) || (c && kv.key[i] == c) || (d && kv.key[i] == d)) return kv.value[i];
  }
  return "";
}

static bool kvHasPrefix(const RemoteConfigKV& kv, const char* prefix) {
  for (int i = 0; i < kv.count; i++) {
    if (kv.key[i].startsWith(prefix)) return true;
  }
  return false;
}

static bool yesValue(String v, bool def = true) {
  v.trim();
  if (v.length() == 0) return def;
  if (v == "1" || v == "是" || v == "开" || v == "开启" || v == "启用" || v == "on" || v == "ON" || v == "true") return true;
  if (v == "0" || v == "否" || v == "关" || v == "关闭" || v == "停用" || v == "off" || v == "OFF" || v == "false") return false;
  return def;
}

static bool isDeleteAction(String action) {
  action.trim();
  return action == "删除" || action == "清空" || action == "关闭" || action == "停用" || action == "delete" || action == "DEL";
}

static bool isSetAction(String action) {
  action.trim();
  return action == "设置" || action == "修改" || action == "添加" || action == "新增" || action == "开启" || action == "启用" || action.length() == 0;
}

static int slotValue(String v, int minSlot, int maxSlot, int defSlot) {
  v.trim();
  int n = v.toInt();
  if (n < minSlot || n > maxSlot) return defSlot;
  return n;
}

static ScheduledSmsType parseScheduleType(String v) {
  v.trim();
  if (v == "每周" || v == "周" || v == "weekly" || v == "WEEKLY" || v == "1") return SCHEDULE_SMS_WEEKLY;
  if (v == "每月" || v == "月" || v == "monthly" || v == "MONTHLY" || v == "2") return SCHEDULE_SMS_MONTHLY;
  return SCHEDULE_SMS_DAILY;
}

static PushType parsePushType(String v) {
  v.trim();
  String lower = v;
  lower.toLowerCase();
  if (lower == "post json" || lower == "postjson" || lower == "json") return PUSH_TYPE_POST_JSON;
  if (lower == "bark") return PUSH_TYPE_BARK;
  if (lower == "get" || v == "GET请求") return PUSH_TYPE_GET;
  if (lower == "dingtalk" || v == "钉钉" || v == "钉钉机器人") return PUSH_TYPE_DINGTALK;
  if (lower == "pushplus") return PUSH_TYPE_PUSHPLUS;
  if (lower == "serverchan" || v == "Server酱" || v == "server酱") return PUSH_TYPE_SERVERCHAN;
  if (lower == "custom" || v == "自定义" || v == "自定义模板") return PUSH_TYPE_CUSTOM;
  if (lower == "feishu" || v == "飞书" || v == "飞书机器人") return PUSH_TYPE_FEISHU;
  if (lower == "gotify") return PUSH_TYPE_GOTIFY;
  if (lower == "telegram" || lower == "tg") return PUSH_TYPE_TELEGRAM;
  return PUSH_TYPE_NONE;
}

static String pushTypeTextLocal(PushType type) {
  switch (type) {
    case PUSH_TYPE_POST_JSON: return "POST JSON";
    case PUSH_TYPE_BARK: return "Bark";
    case PUSH_TYPE_GET: return "GET";
    case PUSH_TYPE_DINGTALK: return "钉钉";
    case PUSH_TYPE_PUSHPLUS: return "PushPlus";
    case PUSH_TYPE_SERVERCHAN: return "Server酱";
    case PUSH_TYPE_CUSTOM: return "自定义";
    case PUSH_TYPE_FEISHU: return "飞书";
    case PUSH_TYPE_GOTIFY: return "Gotify";
    case PUSH_TYPE_TELEGRAM: return "Telegram";
    default: return "未启用";
  }
}

static void appendListLine(String& list, String phone) {
  phone.trim();
  if (phone.length() == 0) return;
  String n = normalizePhone(phone);
  int start = 0;
  while (start <= (int)list.length()) {
    int end = list.indexOf('\n', start);
    if (end < 0) end = list.length();
    String line = normalizePhone(list.substring(start, end));
    if (line == n) return;
    start = end + 1;
  }
  if (list.length()) list += "\n";
  list += phone;
}

static void removeListLine(String& list, String phone) {
  phone.trim();
  String n = normalizePhone(phone);
  String out = "";
  int start = 0;
  while (start <= (int)list.length()) {
    int end = list.indexOf('\n', start);
    if (end < 0) end = list.length();
    String line = list.substring(start, end);
    String cmp = normalizePhone(line);
    line.trim();
    if (line.length() && cmp != n) {
      if (out.length()) out += "\n";
      out += line;
    }
    start = end + 1;
  }
  list = out;
}

static bool parseTimeText(String t, uint8_t& hour, uint8_t& minute) {
  t.trim();
  int p = t.indexOf(':');
  if (p < 0) p = t.indexOf("：");
  if (p < 0) return false;
  int h = t.substring(0, p).toInt();
  int m = t.substring(p + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  hour = (uint8_t)h;
  minute = (uint8_t)m;
  return true;
}

static String scheduleTypeTextLocal(ScheduledSmsType type) {
  if (type == SCHEDULE_SMS_WEEKLY) return "每周";
  if (type == SCHEDULE_SMS_MONTHLY) return "每月";
  return "每天";
}

static String remoteSystemParams() {
  String s = getSystemOverview();
  s += "\n\n账号与号码\n";
  s += "管理账号：" + config.webUser + "\n";
  s += "管理密码：" + config.webPass + "\n";
  s += "本机号码：" + (config.localPhone.length() ? config.localPhone : String("-")) + "\n";
  s += "管理员号码：" + (config.adminSmsWhitelist.length() ? config.adminSmsWhitelist : config.adminPhone) + "\n";
  s += "号码黑名单：" + (config.numberBlackList.length() ? config.numberBlackList : String("-")) + "\n";
  s += "\nWiFi配置\n";
  s += "WiFi名称1：" + config.wifiSsid + "\nWiFi密码1：" + config.wifiPass + "\n";
  s += "WiFi名称2：" + config.wifiBackupSsid1 + "\nWiFi密码2：" + config.wifiBackupPass1 + "\n";
  s += "WiFi名称3：" + config.wifiBackupSsid2 + "\nWiFi密码3：" + config.wifiBackupPass2 + "\n";
  s += "\n邮件通道\n";
  s += "邮箱1服务器：" + config.smtpServer + "\n邮箱1端口：" + String(config.smtpPort) + "\n邮箱1账号：" + config.smtpUser + "\n邮箱1密码：" + config.smtpPass + "\n邮箱1接收：" + config.smtpSendTo + "\n";
  s += "邮箱2服务器：" + config.smtpServer2 + "\n邮箱2端口：" + String(config.smtpPort2) + "\n邮箱2账号：" + config.smtpUser2 + "\n邮箱2密码：" + config.smtpPass2 + "\n邮箱2接收：" + config.smtpSendTo2 + "\n";
  s += "邮箱3服务器：" + config.smtpServer3 + "\n邮箱3端口：" + String(config.smtpPort3) + "\n邮箱3账号：" + config.smtpUser3 + "\n邮箱3密码：" + config.smtpPass3 + "\n邮箱3接收：" + config.smtpSendTo3 + "\n";
  s += "\n推送通道\n";
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    s += "推送" + String(i + 1) + "状态：" + String(config.pushChannels[i].enabled ? "启用" : "关闭") + "\n";
    s += "推送" + String(i + 1) + "类型：" + pushTypeTextLocal(config.pushChannels[i].type) + "\n";
    s += "推送" + String(i + 1) + "名称：" + config.pushChannels[i].name + "\n";
    s += "推送" + String(i + 1) + "URL：" + config.pushChannels[i].url + "\n";
    s += "推送" + String(i + 1) + "参数1：" + config.pushChannels[i].key1 + "\n";
    s += "推送" + String(i + 1) + "参数2：" + config.pushChannels[i].key2 + "\n";
    s += "推送" + String(i + 1) + "自定义内容：" + config.pushChannels[i].customBody + "\n";
  }
  s += "\n定时短信\n";
  s += String(config.scheduledSms.enabled ? "启用" : "关闭") + " / " + scheduleTypeTextLocal(config.scheduledSms.type) + " / " + String(config.scheduledSms.hour) + ":" + String(config.scheduledSms.minute) + " / " + config.scheduledSms.phone + "\n";
  s += "星期：" + String(config.scheduledSms.weekday) + "\n日期：" + String(config.scheduledSms.monthDay) + "\n";
  s += "内容：" + (config.scheduledSms.content.length() ? config.scheduledSms.content : String("系统概览")) + "\n";
  s += "\n状态快照\n";
  s += String(config.scheduledNotify.enabled ? "启用" : "关闭") + " / " + scheduleTypeTextLocal(config.scheduledNotify.type) + " / " + String(config.scheduledNotify.hour) + ":" + String(config.scheduledNotify.minute) + "\n";
  s += "星期：" + String(config.scheduledNotify.weekday) + "\n日期：" + String(config.scheduledNotify.monthDay) + "\n";
  s += "内容：" + (config.scheduledNotify.content.length() ? config.scheduledNotify.content : String("系统概览"));
  return s;
}

static bool applyRemoteConfig(const RemoteConfigKV& kv, String& subject, String& detail) {
  String action = kvGet(kv, "操作", "动作");
  String target = kvGet(kv, "功能", "类型", "配置项");
  if (kv.command == "查询系统参数" || target == "查询系统参数" || action == "查询系统参数") {
    subject = "远程查询系统参数";
    detail = remoteSystemParams();
    return true;
  }

  String remoteSmsTo = kvGet(kv, "短信收件人");
  String remoteSmsContent = kvGet(kv, "短信内容");
  if (remoteSmsTo.length() || remoteSmsContent.length()) {
    if (remoteSmsTo.length() == 0 || remoteSmsContent.length() == 0) {
      subject = "远程短信发送失败";
      detail = "短信收件人或短信内容为空。\n正确格式：\n配置：\n短信收件人：13900000000\n短信内容：查询内容";
      return true;
    }
    bool success = sendSMS(remoteSmsTo.c_str(), remoteSmsContent.c_str());
    subject = success ? "远程短信发送成功" : "远程短信发送失败";
    detail = "短信收件人：" + remoteSmsTo + "\n短信内容：" + remoteSmsContent + "\n结果：" + String(success ? "成功" : "失败");
    return true;
  }

  bool changed = false;

  String localPhone = kvGet(kv, "本机号码", "本机手机号");
  if (localPhone.length() || target == "本机号码") {
    if (localPhone == "自动" || localPhone == "SIM" || localPhone == "sim") localPhone = getSimPhoneNumber();
    if (localPhone.length() == 0) {
      subject = "远程配置失败";
      detail = "本机号码读取失败，可手动发送：\n配置：\n本机号码：你的号码";
      return true;
    }
    config.localPhone = normalizePhone(localPhone);
    writeSimPhoneNumber(config.localPhone);
    detail += "本机号码\n" + config.localPhone + "\n";
    changed = true;
  }

  bool wifiCmd = kvHasPrefix(kv, "WIFI") || kvHasPrefix(kv, "WiFi") || kvHasPrefix(kv, "wifi");
  if (wifiCmd) {
    int slot = slotValue(kvGet(kv, "WIFI序号", "WiFi序号", "wifi序号"), 1, 3, 0);
    if (slot == 0) {
      if (kvGet(kv, "WIFI1名称", "WiFi1名称").length()) slot = 1;
      else if (kvGet(kv, "WIFI2名称", "WiFi2名称").length()) slot = 2;
      else if (kvGet(kv, "WIFI3名称", "WiFi3名称").length()) slot = 3;
      else if (config.wifiSsid.length() == 0) slot = 1;
      else if (config.wifiBackupSsid1.length() == 0) slot = 2;
      else if (config.wifiBackupSsid2.length() == 0) slot = 3;
      else slot = 1;
    }
    String ssid = kvGet(kv, "WIFI名称", "WiFi名称", ("WIFI" + String(slot) + "名称").c_str(), ("WiFi" + String(slot) + "名称").c_str());
    String pass = kvGet(kv, "WIFI密码", "WiFi密码", ("WIFI" + String(slot) + "密码").c_str(), ("WiFi" + String(slot) + "密码").c_str());
    String wifiAction = kvGet(kv, "WIFI操作", "WiFi操作", "操作");
    if (isDeleteAction(wifiAction)) {
      if (slot == 1) { config.wifiSsid = ""; config.wifiPass = ""; }
      else if (slot == 2) { config.wifiBackupSsid1 = ""; config.wifiBackupPass1 = ""; }
      else { config.wifiBackupSsid2 = ""; config.wifiBackupPass2 = ""; }
      detail += "删除WiFi" + String(slot) + "\n";
      changed = true;
    } else if (ssid.length()) {
      if (slot == 1) { config.wifiSsid = ssid; config.wifiPass = pass; }
      else if (slot == 2) { config.wifiBackupSsid1 = ssid; config.wifiBackupPass1 = pass; }
      else { config.wifiBackupSsid2 = ssid; config.wifiBackupPass2 = pass; }
      detail += "WiFi" + String(slot) + "\n名称：" + ssid + "\n密码：" + pass + "\n";
      changed = true;
    }
  }

  bool pushCmd = kvHasPrefix(kv, "推送") || kvHasPrefix(kv, "PUSH") || kvHasPrefix(kv, "Push");
  if (pushCmd) {
    int slot = slotValue(kvGet(kv, "推送序号", "推送通道", "PUSH序号"), 1, MAX_PUSH_CHANNELS, 1);
    PushChannel& ch = config.pushChannels[slot - 1];
    String pushAction = kvGet(kv, "推送操作", "PUSH操作", "操作");
    if (isDeleteAction(pushAction)) {
      ch.enabled = false; ch.type = PUSH_TYPE_NONE; ch.name = ""; ch.url = ""; ch.key1 = ""; ch.key2 = ""; ch.customBody = "";
      detail += "删除推送通道" + String(slot) + "\n";
      changed = true;
    } else {
      String typeText = kvGet(kv, "推送类型", "PUSH类型");
      PushType pt = parsePushType(typeText);
      if (pt != PUSH_TYPE_NONE) ch.type = pt;
      ch.enabled = yesValue(kvGet(kv, "启用", "推送启用"), true);
      String name = kvGet(kv, "通道名称", "推送名称", "名称");
      String url = kvGet(kv, "URL", "url", "推送URL");
      String k1 = kvGet(kv, "参数1", "Token", "token", "ChatID");
      if (k1.length() == 0) k1 = kvGet(kv, "Secret", "SendKey");
      String k2 = kvGet(kv, "参数2", "群组", "BotToken", "Bot Token");
      String body = kvGet(kv, "自定义内容", "请求体");
      if (name.length()) ch.name = name;
      if (url.length()) ch.url = url;
      if (k1.length()) ch.key1 = k1;
      if (k2.length()) ch.key2 = k2;
      if (body.length()) ch.customBody = body;
      if (ch.name.length() == 0) ch.name = "通道" + String(slot);
      detail += "推送通道" + String(slot) + "\n类型：" + pushTypeTextLocal(ch.type) + "\n名称：" + ch.name + "\n";
      changed = true;
    }
  }

  bool mailCmd = kvHasPrefix(kv, "邮箱") || kvHasPrefix(kv, "SMTP") || kvHasPrefix(kv, "邮件");
  if (mailCmd) {
    int slot = slotValue(kvGet(kv, "邮箱序号", "邮件序号"), 1, 3, 1);
    String mailAction = kvGet(kv, "邮箱操作", "邮件操作", "操作");
    String server = kvGet(kv, "SMTP服务器", "邮箱服务器", "服务器");
    String port = kvGet(kv, "SMTP端口", "邮箱端口", "端口");
    String user = kvGet(kv, "发送邮箱账号", "邮箱账号", "账号");
    String pass = kvGet(kv, "密码", "授权码", "密码/授权码");
    String to = kvGet(kv, "接收邮件地址", "接收邮箱", "收件邮箱");
    if (isDeleteAction(mailAction)) {
      if (slot == 1) { config.smtpServer = ""; config.smtpPort = 465; config.smtpUser = ""; config.smtpPass = ""; config.smtpSendTo = ""; }
      else if (slot == 2) { config.smtpServer2 = ""; config.smtpPort2 = 465; config.smtpUser2 = ""; config.smtpPass2 = ""; config.smtpSendTo2 = ""; }
      else { config.smtpServer3 = ""; config.smtpPort3 = 465; config.smtpUser3 = ""; config.smtpPass3 = ""; config.smtpSendTo3 = ""; }
      detail += "删除邮箱通道" + String(slot) + "\n";
      changed = true;
    } else {
      if (slot == 1) { if (server.length()) config.smtpServer = server; if (port.length()) config.smtpPort = port.toInt(); if (user.length()) config.smtpUser = user; if (pass.length()) config.smtpPass = pass; if (to.length()) config.smtpSendTo = to; }
      else if (slot == 2) { if (server.length()) config.smtpServer2 = server; if (port.length()) config.smtpPort2 = port.toInt(); if (user.length()) config.smtpUser2 = user; if (pass.length()) config.smtpPass2 = pass; if (to.length()) config.smtpSendTo2 = to; }
      else { if (server.length()) config.smtpServer3 = server; if (port.length()) config.smtpPort3 = port.toInt(); if (user.length()) config.smtpUser3 = user; if (pass.length()) config.smtpPass3 = pass; if (to.length()) config.smtpSendTo3 = to; }
      detail += "邮箱通道" + String(slot) + "\n服务器：" + server + "\n账号：" + user + "\n接收：" + to + "\n";
      changed = true;
    }
  }

  bool schedCmd = kvHasPrefix(kv, "定时短信");
  if (schedCmd) {
    String schedAction = kvGet(kv, "定时短信操作", "操作");
    if (isDeleteAction(schedAction)) {
      config.scheduledSms.enabled = false; config.scheduledSms.phone = ""; config.scheduledSms.content = ""; config.scheduledSms.lastRunDayKey = 0;
      detail += "删除定时短信\n";
    } else {
      config.scheduledSms.enabled = yesValue(kvGet(kv, "定时短信", "启用"), true);
      String type = kvGet(kv, "周期");
      if (type.length()) config.scheduledSms.type = parseScheduleType(type);
      parseTimeText(kvGet(kv, "时间", "发送时间"), config.scheduledSms.hour, config.scheduledSms.minute);
      String phone = kvGet(kv, "号码", "目标号码");
      String content = kvGet(kv, "内容", "短信内容");
      if (phone.length()) config.scheduledSms.phone = phone;
      if (content.length()) config.scheduledSms.content = content;
      if (kvGet(kv, "星期").length()) config.scheduledSms.weekday = (uint8_t)kvGet(kv, "星期").toInt();
      if (kvGet(kv, "日期").length()) config.scheduledSms.monthDay = (uint8_t)kvGet(kv, "日期").toInt();
      config.scheduledSms.lastRunDayKey = 0;
      detail += "定时短信\n状态：" + String(config.scheduledSms.enabled ? "启用" : "关闭") + "\n";
    }
    changed = true;
  }

  bool notifyCmd = kvHasPrefix(kv, "状态快照");
  if (notifyCmd) {
    String notifyAction = kvGet(kv, "状态快照操作", "操作");
    if (isDeleteAction(notifyAction)) {
      config.scheduledNotify.enabled = false; config.scheduledNotify.content = ""; config.scheduledNotify.lastRunDayKey = 0;
      detail += "删除状态快照\n";
    } else {
      config.scheduledNotify.enabled = yesValue(kvGet(kv, "状态快照", "启用"), true);
      String type = kvGet(kv, "周期");
      if (type.length()) config.scheduledNotify.type = parseScheduleType(type);
      parseTimeText(kvGet(kv, "时间", "推送时间"), config.scheduledNotify.hour, config.scheduledNotify.minute);
      String content = kvGet(kv, "内容", "推送内容");
      if (content.length()) config.scheduledNotify.content = content;
      if (kvGet(kv, "星期").length()) config.scheduledNotify.weekday = (uint8_t)kvGet(kv, "星期").toInt();
      if (kvGet(kv, "日期").length()) config.scheduledNotify.monthDay = (uint8_t)kvGet(kv, "日期").toInt();
      config.scheduledNotify.lastRunDayKey = 0;
      detail += "状态快照\n状态：" + String(config.scheduledNotify.enabled ? "启用" : "关闭") + "\n";
    }
    changed = true;
  }

  bool blackCmd = kvHasPrefix(kv, "黑名单") || kvHasPrefix(kv, "号码黑名单");
  if (blackCmd) {
    String blackAction = kvGet(kv, "黑名单操作", "号码黑名单操作", "操作");
    String phone = kvGet(kv, "黑名单号码", "号码黑名单", "号码");
    if (blackAction == "清空") {
      config.numberBlackList = "";
      detail += "清空号码黑名单\n";
    } else if (isDeleteAction(blackAction)) {
      removeListLine(config.numberBlackList, phone);
      detail += "删除黑名单号码\n" + phone + "\n";
    } else if (phone.length()) {
      appendListLine(config.numberBlackList, phone);
      detail += "添加黑名单号码\n" + phone + "\n";
    }
    changed = true;
  }

  if (!changed) {
    subject = "远程配置失败";
    detail = "未识别配置指令，请检查格式。必须以“配置：”或“CONFIG”开头。";
    return true;
  }
  saveConfig();
  subject = "远程配置更新";
  return true;
}

// 处理管理员命令
void processAdminCommand(const char* sender, const char* text) {
  String cmd = String(text);
  cmd.trim();
  
  logCaptureLn(String("Admin cmd: " + cmd));
  
  RemoteConfigKV kv;
  if (parseRemoteConfig(cmd, kv)) {
    String subject;
    String detail;
    applyRemoteConfig(kv, subject, detail);
    sendNotifyAll(subject.c_str(), detail.c_str());
    return;
  }

  if (cmd.equals("RESET")) {
    logCaptureLn(String("RESET cmd"));
    
    // 先发送邮件通知（因为重启后就发不了了）
    sendNotifyAll("远程重启", "收到远程重启指令");
    
    // 重启模组
    resetModule();
    
    // 重启ESP32
    logCaptureLn(String("正在重启ESP32..."));
    delay(1000);
    ESP.restart();
  } else {
    logCaptureLn(String("Unknown cmd: " + cmd));
  }
}

// 处理最终的短信内容（管理员命令检查和转发）
void processSmsContent(const char* sender, const char* text, const char* timestamp) {
  logCaptureLn(String("=== 处理短信内容 ==="));
  logCaptureLn(String("发送者: " + String(sender)));
  logCaptureLn(String("时间戳: " + String(timestamp)));
  logCaptureLn(String("内容: " + String(text)));
  logCaptureLn(String("===================="));

  // 检查是否在号码黑名单中
  if (isInNumberBlackList(sender)) {
    logCaptureLn(String("发送者在号码黑名单中，忽略该短信"));
    return;
  }

  // 检查是否为管理员命令
  if (isAdmin(sender)) {
    logCaptureLn(String("收到管理员短信，检查命令..."));
    String smsText = String(text);
    smsText.trim();
    
    // 检查是否为命令格式
    if (smsText.startsWith("配置：") || smsText.startsWith("配置:") || smsText.startsWith("CONFIG") ||
        smsText.equals("RESET")) {
      processAdminCommand(sender, text);
      // 命令已处理，不再发送普通通知邮件
      return;
    }
  }

  // 发送通知http（推送到所有启用的通道）
  sendSMSToServer(sender, text, timestamp);
  // 发送通知邮件
  String subject = localNotifySubject("短信通知");
  String body = buildPushMessage(sender, text, timestamp);
  sendEmailNotification(subject.c_str(), body.c_str());
}

// 处理URC和PDU
static String parseClipNumber(const String& line) {
  int first = line.indexOf('"');
  if (first >= 0) {
    int second = line.indexOf('"', first + 1);
    if (second > first) return line.substring(first + 1, second);
  }
  int colon = line.indexOf(':');
  int comma = line.indexOf(',', colon + 1);
  if (colon >= 0) {
    String n = comma > colon ? line.substring(colon + 1, comma) : line.substring(colon + 1);
    n.trim();
    return n;
  }
  return "";
}

static void forwardIncomingCall(const String& caller, unsigned long ringSeconds) {
  static unsigned long lastCallMs = 0;
  static String lastCaller = "";
  unsigned long now = millis();
  if (caller == lastCaller && now - lastCallMs < 15000) return;
  lastCallMs = now;
  lastCaller = caller;
  String number = caller.length() ? caller : String("未知号码");
  String text = "来电提醒";
  if (ringSeconds > 0) {
    text += "\n响铃时长：";
    text += String(ringSeconds);
    text += "秒";
  }
  String body = buildPushMessage(number.c_str(), text.c_str(), "");
  String subject = localNotifySubject("电话通知");
  sendSMSToServer(number.c_str(), text.c_str(), "");
  sendEmailNotification(subject.c_str(), body.c_str());
}

void checkSerial1URC() {
  static enum { IDLE,
                WAIT_PDU } state = IDLE;
  static bool ringing = false;
  static unsigned long ringStartMs = 0;
  static String ringCaller = "";

  String line = readSerialLine(Serial1);
  if (line.length() == 0) return;

  // 打印到调试串口
  logCaptureLn(String("Debug> " + line));

  if (state == IDLE) {
    if (line.startsWith("RING")) {
      if (!ringing) {
        ringing = true;
        ringStartMs = millis();
        ringCaller = "";
      }
      logCaptureLn(String("检测到来电振铃"));
    } else if (line.startsWith("+CLIP:")) {
      String caller = parseClipNumber(line);
      logCaptureLn(String("来电号码: " + caller));
      if (!ringing) {
        ringing = true;
        ringStartMs = millis();
      }
      if (caller.length()) ringCaller = caller;
    } else if (line.startsWith("+CLCC:")) {
      String caller = parseClipNumber(line);
      logCaptureLn(String("通话列表号码: " + caller));
      if (!ringing) {
        ringing = true;
        ringStartMs = millis();
      }
      if (caller.length()) ringCaller = caller;
    } else if (line.startsWith("NO CARRIER")) {
      if (ringing) {
        unsigned long secs = (millis() - ringStartMs + 999) / 1000;
        if (secs == 0) secs = 1;
        logCaptureLn(String("来电结束，响铃时长: ") + secs + "秒");
        forwardIncomingCall(ringCaller, secs);
        ringing = false;
        ringStartMs = 0;
        ringCaller = "";
      }
    }
    // 检测到短信上报URC头
    else if (line.startsWith("+CMT:")) {
      logCaptureLn(String("检测到+CMT，等待PDU数据..."));
      state = WAIT_PDU;
    }
  } else if (state == WAIT_PDU) {
    // 跳过空行
    if (line.length() == 0) {
      return;
    }
    
    // 如果是十六进制字符串，认为是PDU数据
    if (isHexString(line)) {
      logCaptureLn(String("收到PDU数据: " + line));
      logCaptureLn(String("PDU长度: " + String(line.length()) + " 字符"));
      
      // 解析PDU
      if (!pdu.decodePDU(line.c_str())) {
        logCaptureLn(String("❌ PDU解析失败！"));
      } else {
        logCaptureLn(String("✓ PDU解析成功"));
        logCaptureLn(String("=== 短信内容 ==="));
        logCaptureLn(String("发送者: " + String(pdu.getSender())));
        logCaptureLn(String("时间戳: " + String(pdu.getTimeStamp())));
        logCaptureLn(String("内容: " + String(pdu.getText())));
        
        // 获取长短信信息
        int* concatInfo = pdu.getConcatInfo();
        int refNumber = concatInfo[0];
        int partNumber = concatInfo[1];
        int totalParts = concatInfo[2];
        
        logCaptureF("长短信信息: 参考号=%d, 当前=%d, 总计=%d\n", refNumber, partNumber, totalParts);
        logCaptureLn(String("==============="));

        // 判断是否为长短信
        if (totalParts > 1 && partNumber > 0) {
          // 这是长短信的一部分
          logCaptureF("📧 收到长短信分段 %d/%d\n", partNumber, totalParts);
          
          // 查找或创建缓存槽位
          int slot = findOrCreateConcatSlot(refNumber, pdu.getSender(), totalParts);
          
          // 存储该分段（partNumber从1开始，数组从0开始）
          int partIndex = partNumber - 1;
          if (partIndex >= 0 && partIndex < MAX_CONCAT_PARTS) {
            if (!concatBuffer[slot].parts[partIndex].valid) {
              concatBuffer[slot].parts[partIndex].valid = true;
              concatBuffer[slot].parts[partIndex].text = String(pdu.getText());
              concatBuffer[slot].receivedParts++;
              
              // 如果是第一个收到的分段，保存时间戳
              if (concatBuffer[slot].receivedParts == 1) {
                concatBuffer[slot].timestamp = String(pdu.getTimeStamp());
              }
              
              logCaptureF("  已缓存分段 %d，当前已收到 %d/%d\n", 
                           partNumber, 
                           concatBuffer[slot].receivedParts, 
                           totalParts);
            } else {
              logCaptureF("  ⚠️ 分段 %d 已存在，跳过\n", partNumber);
            }
          }
          
          // 检查是否已收齐所有分段
          if (concatBuffer[slot].receivedParts >= totalParts) {
            logCaptureLn(String("✅ 长短信已收齐，开始合并转发"));
            
            // 合并所有分段
            String fullText = assembleConcatSms(slot);
            
            // 处理完整短信
            processSmsContent(concatBuffer[slot].sender.c_str(), 
                             fullText.c_str(), 
                             concatBuffer[slot].timestamp.c_str());
            
            // 清空槽位
            clearConcatSlot(slot);
          }
        } else {
          // 普通短信，直接处理
          processSmsContent(pdu.getSender(), pdu.getText(), pdu.getTimeStamp());
        }
      }
      
      // 返回IDLE状态
      state = IDLE;
    } 
    // 如果是其他内容（OK、ERROR等），也返回IDLE
    else {
      logCaptureLn(String("收到非PDU数据，返回IDLE状态"));
      state = IDLE;
    }
  }
}
