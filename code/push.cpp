#include "push.h"
#include "web_handlers.h"
#include "config.h"
#include "phone_location.h"
#include <HTTPClient.h>
#include <mbedtls/md.h>
#include <base64.h>
#include <sys/time.h>

static bool emailChannelValid(const String& server, const String& user, const String& pass, const String& sendTo) {
  return server.length() > 0 && user.length() > 0 && pass.length() > 0 && sendTo.length() > 0;
}

static void sendEmailTo(const String& server, int port, const String& user, const String& pass, const String& toAddr, const char* subject, const char* body) {
  auto statusCallback = [](SMTPStatus status) {
    logCaptureLn(String(status.text));
  };
  smtp.connect(server.c_str(), port, statusCallback);
  if (smtp.isConnected()) {
    smtp.authenticate(user.c_str(), pass.c_str(), readymail_auth_password);

    SMTPMessage msg;
    String from = "sms notify <"; from += user; from += ">";
    msg.headers.add(rfc822_from, from.c_str());
    String to = "your_email <"; to += toAddr; to += ">";
    msg.headers.add(rfc822_to, to.c_str());
    msg.headers.add(rfc822_subject, subject);
    msg.text.body(body);
    msg.timestamp = time(nullptr);
    smtp.send(msg);
    logCaptureLn(String("邮件发送完成: ") + toAddr);
  } else {
    logCaptureLn(String("邮件服务器连接失败: ") + toAddr);
  }
}

static void sendEmailChannel(const String& server, int port, const String& user, const String& pass, const String& sendTo, const char* subject, const char* body) {
  if (!emailChannelValid(server, user, pass, sendTo)) {
    return;
  }
  String recipients = sendTo;
  recipients.replace("\r", "\n");
  recipients.replace(",", "\n");
  recipients.replace("，", "\n");
  recipients.replace(";", "\n");
  recipients.replace("；", "\n");

  int start = 0;
  while (start <= (int)recipients.length()) {
    int end = recipients.indexOf('\n', start);
    if (end < 0) end = recipients.length();
    String toAddr = recipients.substring(start, end);
    toAddr.trim();
    if (toAddr.length() > 0) {
      sendEmailTo(server, port, user, pass, toAddr, subject, body);
      delay(100);
    }
    start = end + 1;
  }
}

// 发送邮件通知函数，支持3个完全独立的SMTP通道
void sendEmailNotification(const char* subject, const char* body) {
  bool hasEmail = emailChannelValid(config.smtpServer, config.smtpUser, config.smtpPass, config.smtpSendTo) ||
                  emailChannelValid(config.smtpServer2, config.smtpUser2, config.smtpPass2, config.smtpSendTo2) ||
                  emailChannelValid(config.smtpServer3, config.smtpUser3, config.smtpPass3, config.smtpSendTo3);
  if (!hasEmail) {
    logCaptureLn(String("邮件配置不完整，跳过发送"));
    return;
  }
  sendEmailChannel(config.smtpServer, config.smtpPort, config.smtpUser, config.smtpPass, config.smtpSendTo, subject, body);
  sendEmailChannel(config.smtpServer2, config.smtpPort2, config.smtpUser2, config.smtpPass2, config.smtpSendTo2, subject, body);
  sendEmailChannel(config.smtpServer3, config.smtpPort3, config.smtpUser3, config.smtpPass3, config.smtpSendTo3, subject, body);
}

static const char* pushTypeName(PushType type) {
  switch (type) {
    case PUSH_TYPE_BARK: return "Bark";
    case PUSH_TYPE_GET: return "GET";
    case PUSH_TYPE_DINGTALK: return "钉钉";
    case PUSH_TYPE_PUSHPLUS: return "PushPlus";
    case PUSH_TYPE_SERVERCHAN: return "Server酱";
    case PUSH_TYPE_CUSTOM: return "自定义";
    case PUSH_TYPE_FEISHU: return "飞书";
    case PUSH_TYPE_GOTIFY: return "Gotify";
    case PUSH_TYPE_TELEGRAM: return "Telegram";
    case PUSH_TYPE_POST_JSON: return "POST JSON";
    default: return "-";
  }
}

String getEnabledPushNames() {
  String s = "";
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (isPushChannelValid(config.pushChannels[i])) {
      if (s.length()) s += ", ";
      s += pushTypeName(config.pushChannels[i].type);
    }
  }
  return s.length() ? s : String("-");
}

static String adminSmsListText() {
  String s = config.adminSmsWhitelist.length() ? config.adminSmsWhitelist : config.adminPhone;
  s.replace("\r", "");
  s.replace("\n", ", ");
  return s.length() ? s : String("-");
}

static String notifyTimeText(const char* fallback) {
  time_t now = time(nullptr);
  if (now >= 100000) {
    now += 8 * 3600;
    tm t;
    gmtime_r(&now, &t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d年%d月%d日 %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return String(buf);
  }
  return String(fallback && fallback[0] ? fallback : "-");
}

static String phoneDigitsOnly(const String& raw) {
  String s = "";
  for (unsigned int i = 0; i < raw.length(); i++) {
    char c = raw.charAt(i);
    if (c >= '0' && c <= '9') s += c;
  }
  return s;
}

static String normalizedChinaMobile(const String& raw) {
  String value = raw;
  value.trim();
  if (value.startsWith("+") && !value.startsWith("+86")) return "";
  String digits = phoneDigitsOnly(raw);
  if (digits.startsWith("86") && digits.length() >= 13) digits = digits.substring(2);
  if (digits.length() == 11 && digits.charAt(0) == '1') return digits;
  return "";
}

static String formatPhoneNumber(const char* raw, bool spaceAfterCountry) {
  String value = String(raw && raw[0] ? raw : "-");
  String mobile = normalizedChinaMobile(value);
  if (mobile.length()) return String("+86") + (spaceAfterCountry ? " " : "") + mobile;
  value.trim();
  if (value.startsWith("+")) return value;
  String digits = phoneDigitsOnly(value);
  if (digits.length()) return digits;
  return value.length() ? value : String("-");
}

static String phoneRegionText(const char* raw) {
  String mobile = normalizedChinaMobile(String(raw && raw[0] ? raw : ""));
  if (mobile.length()) {
    String loc = lookupPhoneLocationByPrefix((uint32_t)mobile.substring(0, 7).toInt());
    return loc.length() ? loc : String("未知归属地");
  }
  String value = String(raw && raw[0] ? raw : "");
  value.trim();
  if (value.startsWith("+86")) return "中国";
  if (value.startsWith("+1")) return "美国";
  if (value.startsWith("+81")) return "日本";
  if (value.startsWith("+82")) return "韩国";
  if (value.startsWith("+44")) return "英国";
  if (value.startsWith("+")) return "国外号码";
  return "中国";
}

String buildPushMessage(const char* sender, const char* message, const char* timestamp) {
  bool systemSender = sender && (strcmp(sender, "系统") == 0 || strncmp(sender, "系统通知：", strlen("系统通知：")) == 0);
  String content = String(message && message[0] ? message : "-");
  bool callMessage = content.indexOf("来电提醒") >= 0;
  content.replace("\r", "");
  String reason = "";
  String cleaned = "";
  int start = 0;
  while (start <= (int)content.length()) {
    int end = content.indexOf('\n', start);
    if (end < 0) end = content.length();
    String line = content.substring(start, end);
    String t = line;
    t.trim();
    if (t.startsWith("通知原因：")) reason = t.substring(String("通知原因：").length());
    else if (t.startsWith("通知原因:")) reason = t.substring(String("通知原因:").length());
    else if (t.startsWith("短信内容：")) line = t.substring(String("短信内容：").length());
    else if (t.startsWith("短信内容:")) line = t.substring(String("短信内容:").length());

    if (!(t.startsWith("通知原因：") || t.startsWith("通知原因:") ||
          t.startsWith("时间：") || t.startsWith("时间:") ||
          t.startsWith("发送者：") || t.startsWith("发送者:") ||
          t.startsWith("来信号码：") || t.startsWith("来信号码:") ||
          t.startsWith("来信归属地：") || t.startsWith("来信归属地:") ||
          t.startsWith("来电号码：") || t.startsWith("来电号码:") ||
          t.startsWith("来电归属地：") || t.startsWith("来电归属地:") ||
          t.startsWith("本机号码：") || t.startsWith("本机号码:"))) {
      if (cleaned.length()) cleaned += "\n";
      cleaned += line;
    }
    start = end + 1;
  }
  cleaned.trim();
  if (cleaned.length() == 0) cleaned = "-";

  if (callMessage) {
    String callDetail = cleaned;
    callDetail.replace("来电提醒：", "");
    callDetail.replace("来电提醒", "");
    callDetail.trim();
    String s = "来电号码：";
    s += formatPhoneNumber(sender, true);
    s += "\n来电归属地：";
    s += phoneRegionText(sender);
    s += "\n本机号码：";
    s += config.localPhone.length() ? config.localPhone : String("-");
    s += "\n来电提醒：\n\n";
    if (callDetail.length() && callDetail != "-") {
      s += callDetail;
    }
    s += "\n\n\n";
    s += notifyTimeText(timestamp);
    return s;
  }

  if (systemSender) {
    if (reason.length() == 0 && sender && strncmp(sender, "系统通知：", strlen("系统通知：")) == 0) {
      reason = String(sender).substring(String("系统通知：").length());
    }
    reason.trim();
    String s = "本机号码：";
    s += config.localPhone.length() ? config.localPhone : String("-");
    s += "\n发送者：系统通知";
    s += "\n通知原因：";
    s += reason.length() ? reason : String("系统事件");
    if (cleaned.length() && cleaned != "-") {
      s += "\n";
      s += cleaned;
    }
    s += "\n\n";
    s += notifyTimeText(timestamp);
    return s;
  } else {
    String s = "来信号码：";
    s += formatPhoneNumber(sender, false);
    s += "\n来信归属地：";
    s += phoneRegionText(sender);
    s += "\n本机号码：";
    s += config.localPhone.length() ? config.localPhone : String("-");
    s += "\n短信内容：\n\n";
    s += cleaned;
    s += "\n\n\n";
    s += notifyTimeText(timestamp);
    return s;
  }
}

static String notifyTitle(const char* sender, const char* message) {
  if (sender && strncmp(sender, "系统通知：", strlen("系统通知：")) == 0) return String(sender);
  if (sender && strcmp(sender, "系统") == 0) {
    return "系统通知";
  }
  String local = config.localPhone.length() ? config.localPhone : String("");
  if (message && strstr(message, "来电提醒")) return "电话通知" + local;
  return "短信通知" + local;
}

String getSystemOverview() {
  String s = "管理地址：" + getDeviceUrl() + "\n";
  s += "本机号码：" + (config.localPhone.length() ? config.localPhone : String("-")) + "\n";
  s += "管理员号码：" + adminSmsListText() + "\n";
  s += "推送通道：" + getEnabledPushNames() + "\n";
  s += "WiFi：" + WiFi.SSID() + "\n";
  s += "IP：" + WiFi.localIP().toString() + "\n";
  s += "信号：" + String(WiFi.RSSI()) + " dBm\n";
  s += "可用内存：" + String(ESP.getFreeHeap() / 1024) + " KB\n";
  s += "运行时长：" + String(millis() / 1000) + "秒\n";
  s += "模组状态：" + String(modemReady ? "已就绪" : "未插SIM卡/未注册");
  return s;
}

void sendNotifyAll(const char* subject, const char* body) {
  String raw = "通知原因：";
  raw += subject && subject[0] ? subject : "系统事件";
  raw += "\n";
  raw += body && body[0] ? body : "-";
  String title = "系统通知：";
  title += subject && subject[0] ? subject : "系统事件";
  String msg = buildPushMessage(title.c_str(), raw.c_str(), "");
  sendSMSToServer(title.c_str(), raw.c_str(), "");
  sendEmailNotification(title.c_str(), msg.c_str());
}

// URL编码辅助函数
String urlEncode(const String& str) {
  String encoded = "";
  char c;
  char code0;
  char code1;
  for (unsigned int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encoded += '+';
    } else if (isalnum(c)) {
      encoded += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) code0 = c - 10 + 'A';
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

// 钉钉签名函数（时间戳为UTC毫秒级）
String dingtalkSign(const String& secret, int64_t timestamp) {
  String stringToSign = String(timestamp) + "\n" + secret;
  
  uint8_t hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)secret.c_str(), secret.length());
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)stringToSign.c_str(), stringToSign.length());
  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);
  
  String base64Encoded = base64::encode(hmacResult, 32);
  return urlEncode(base64Encoded);
}

// 获取当前UTC毫秒级时间戳（用于钉钉签名）
int64_t getUtcMillis() {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) == 0) {
    return (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
  }
  // 如果获取失败，使用time()函数
  return (int64_t)time(nullptr) * 1000LL;
}

// JSON转义函数
String jsonEscape(const String& str) {
  String result = "";
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == '"') result += "\\\"";
    else if (c == '\\') result += "\\\\";
    else if (c == '\n') result += "\\n";
    else if (c == '\r') result += "\\r";
    else if (c == '\t') result += "\\t";
    else result += c;
  }
  return result;
}

// 发送单个推送通道
bool sendToChannel(const PushChannel& channel, const char* sender, const char* message, const char* timestamp) {
  if (!channel.enabled) return false;
  
  // 对于某些推送方式，URL可以为空（使用默认URL）
  bool needUrl = (channel.type == PUSH_TYPE_POST_JSON || channel.type == PUSH_TYPE_BARK || 
                  channel.type == PUSH_TYPE_GET || channel.type == PUSH_TYPE_DINGTALK || 
                  channel.type == PUSH_TYPE_CUSTOM);
  if (needUrl && channel.url.length() == 0) return false;
  
  HTTPClient http;
  http.setTimeout(2000);
  String channelName = channel.name.length() > 0 ? channel.name : ("通道" + String(channel.type));
  logCaptureLn(String("发送到推送通道: " + channelName));
  
  int httpCode = 0;
  String title = notifyTitle(sender, message);
  String titleEscaped = jsonEscape(title);
  String senderEscaped = jsonEscape(String(sender));
  String messageEscaped = jsonEscape(String(message));
  String timestampEscaped = jsonEscape(String(timestamp));
  
  switch (channel.type) {
    case PUSH_TYPE_POST_JSON: {
      // 标准POST JSON格式
      http.begin(channel.url);
      http.addHeader("Content-Type", "application/json");
      String jsonData = "{";
      jsonData += "\"sender\":\"" + senderEscaped + "\",";
      jsonData += "\"message\":\"" + messageEscaped + "\",";
      jsonData += "\"timestamp\":\"" + timestampEscaped + "\"";
      jsonData += "}";
      httpCode = http.POST(jsonData);
      break;
    }
    
    case PUSH_TYPE_BARK: {
      // Bark推送格式
      http.begin(channel.url);
      http.addHeader("Content-Type", "application/json");
      String jsonData = "{";
      jsonData += "\"title\":\"" + titleEscaped + "\",";
      jsonData += "\"body\":\"" + messageEscaped + "\"";
      jsonData += "}";
      httpCode = http.POST(jsonData);
      break;
    }
    
    case PUSH_TYPE_GET: {
      // GET请求，参数放URL里
      String getUrl = channel.url;
      if (getUrl.indexOf('?') == -1) {
        getUrl += "?";
      } else {
        getUrl += "&";
      }
      getUrl += "sender=" + urlEncode(String(sender));
      getUrl += "&message=" + urlEncode(String(message));
      getUrl += "&timestamp=" + urlEncode(String(timestamp));
      http.begin(getUrl);
      httpCode = http.GET();
      break;
    }
    
    case PUSH_TYPE_DINGTALK: {
      // 钉钉机器人
      String webhookUrl = channel.url;
      
      // 如果配置了secret，需要添加签名
      if (channel.key1.length() > 0) {
        // 获取UTC毫秒级时间戳（钉钉要求）
        int64_t ts = getUtcMillis();
        String sign = dingtalkSign(channel.key1, ts);
        if (webhookUrl.indexOf('?') == -1) {
          webhookUrl += "?";
        } else {
          webhookUrl += "&";
        }
        // 使用字符串拼接避免int64_t转换问题
        char tsBuf[21];
        snprintf(tsBuf, sizeof(tsBuf), "%lld", ts);
        webhookUrl += "timestamp=" + String(tsBuf) + "&sign=" + sign;
      }
      
      http.begin(webhookUrl);
      http.addHeader("Content-Type", "application/json");
      String jsonData = "{\"msgtype\":\"text\",\"text\":{\"content\":\"";
      jsonData += messageEscaped;
      jsonData += "\"}}";
      httpCode = http.POST(jsonData);
      break;
    }

    case PUSH_TYPE_PUSHPLUS: {
      // PushPlus
      String pushUrl = channel.url.length() > 0 ? channel.url : "http://www.pushplus.plus/send";
      http.begin(pushUrl);
      http.addHeader("Content-Type", "application/json");
      // PushPlus第二参数：wechat/app/extension按渠道处理，其他内容按群组topic处理。
      String channelValue = "wechat";
      String topicValue = "";
      if (channel.key2.length() > 0) {
          if (channel.key2 == "wechat" || channel.key2 == "extension" || channel.key2 == "app") {
              channelValue = channel.key2;
          } else {
              topicValue = channel.key2;
          }
      }
      String jsonData = "{";
      jsonData += "\"token\":\"" + channel.key1 + "\",";
      jsonData += "\"title\":\"" + titleEscaped + "\",";
      jsonData += "\"content\":\"" + messageEscaped + "\",";
      jsonData += "\"channel\":\"" + channelValue + "\"";
      if (topicValue.length()) {
        jsonData += ",\"topic\":\"" + jsonEscape(topicValue) + "\"";
      }
      jsonData += "}";
      httpCode = http.POST(jsonData);
      break;
    }

    case PUSH_TYPE_SERVERCHAN: {
      // Server酱
      String scUrl = channel.url.length() > 0 ? channel.url : ("https://sctapi.ftqq.com/" + channel.key1 + ".send");
      http.begin(scUrl);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      String postData = "title=" + urlEncode(title);
      postData += "&desp=" + urlEncode(String(message));
      httpCode = http.POST(postData);
      break;
    }
    
    case PUSH_TYPE_CUSTOM: {
      // 自定义模板
      if (channel.customBody.length() == 0) {
        logCaptureLn(String("自定义模板为空，跳过"));
        return false;
      }
      http.begin(channel.url);
      http.addHeader("Content-Type", "application/json");
      String body = channel.customBody;
      body.replace("{sender}", senderEscaped);
      body.replace("{message}", messageEscaped);
      body.replace("{timestamp}", timestampEscaped);
      httpCode = http.POST(body);
      break;
    }
    
    case PUSH_TYPE_FEISHU: {
      // 飞书机器人
      String webhookUrl = channel.url;
      String jsonData = "{";
      
      // 如果配置了secret，需要添加签名
      if (channel.key1.length() > 0) {
        // 飞书使用秒级时间戳
        int64_t ts = time(nullptr);
        // 飞书签名: base64(hmac-sha256(timestamp + "\n" + secret, secret))
        String stringToSign = String(ts) + "\n" + channel.key1;
        uint8_t hmacResult[32];
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
        mbedtls_md_hmac_starts(&ctx, (const unsigned char*)channel.key1.c_str(), channel.key1.length());
        mbedtls_md_hmac_update(&ctx, (const unsigned char*)stringToSign.c_str(), stringToSign.length());
        mbedtls_md_hmac_finish(&ctx, hmacResult);
        mbedtls_md_free(&ctx);
        String sign = base64::encode(hmacResult, 32);
        
        jsonData += "\"timestamp\":\"" + String(ts) + "\",";
        jsonData += "\"sign\":\"" + sign + "\",";
      }
      
      // 飞书消息体
      jsonData += "\"msg_type\":\"text\",";
      jsonData += "\"content\":{\"text\":\"";
      jsonData += messageEscaped;
      jsonData += "\"}}";
      
      http.begin(webhookUrl);
      http.addHeader("Content-Type", "application/json");
      httpCode = http.POST(jsonData);
      break;
    }
    
    case PUSH_TYPE_GOTIFY: {
      // Gotify 推送
      String gotifyUrl = channel.url;
      // 确保URL以/结尾
      if (!gotifyUrl.endsWith("/")) gotifyUrl += "/";
      gotifyUrl += "message?token=" + channel.key1;
      
      http.begin(gotifyUrl);
      http.addHeader("Content-Type", "application/json");
      String jsonData = "{";
      jsonData += "\"title\":\"" + titleEscaped + "\",";
      jsonData += "\"message\":\"" + messageEscaped + "\",";
      jsonData += "\"priority\":5";
      jsonData += "}";
      httpCode = http.POST(jsonData);
      break;
    }
    
    case PUSH_TYPE_TELEGRAM: {
      // Telegram Bot 推送
      // channel.key1 是 Chat ID, channel.key2 是 Bot Token
      String tgBaseUrl = channel.url.length() > 0 ? channel.url : "https://api.telegram.org";
      if (tgBaseUrl.endsWith("/")) tgBaseUrl.remove(tgBaseUrl.length() - 1);
      
      String tgUrl = tgBaseUrl + "/bot" + channel.key2 + "/sendMessage";
      http.begin(tgUrl);
      http.addHeader("Content-Type", "application/json");
      
      String jsonData = "{";
      jsonData += "\"chat_id\":\"" + channel.key1 + "\",";
      String text = messageEscaped;
      jsonData += "\"text\":\"" + text + "\"";
      jsonData += "}";
      
      httpCode = http.POST(jsonData);
      break;
    }
    
    default:
      logCaptureLn(String("未知推送类型"));
      return false;
  }
  
  bool ok = false;
  if (httpCode > 0) {
    logCaptureF("[%s] 响应码: %d\n", channelName.c_str(), httpCode);
    if (httpCode >= 200 && httpCode < 300) {
      String response = http.getString();
      logCaptureLn(String("响应: " + response));
      ok = true;
    }
  } else {
    logCaptureF("[%s] HTTP请求失败: %s\n", channelName.c_str(), http.errorToString(httpCode).c_str());
  }
  http.end();
  return ok;
}

// 发送短信到所有启用的推送通道
void sendSMSToServer(const char* sender, const char* message, const char* timestamp) {
  if (WiFi.status() != WL_CONNECTED) {
    logCaptureLn(String("WiFi未连接，跳过推送"));
    return;
  }
  
  bool hasEnabledChannel = false;
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (isPushChannelValid(config.pushChannels[i])) {
      hasEnabledChannel = true;
      break;
    }
  }
  
  if (!hasEnabledChannel) {
    logCaptureLn(String("没有启用的推送通道"));
    return;
  }
  
  String pushTime = notifyTimeText(timestamp);
  String pushMessage = buildPushMessage(sender, message, pushTime.c_str());
  bool failed[MAX_PUSH_CHANNELS] = {false};
  bool hasFailed = false;

  logCaptureLn(String("\n=== 开始多通道推送 ==="));
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (isPushChannelValid(config.pushChannels[i])) {
      bool ok = sendToChannel(config.pushChannels[i], sender, pushMessage.c_str(), pushTime.c_str());
      if (!ok) {
        failed[i] = true;
        hasFailed = true;
      }
      delay(20);
    }
  }
  for (int attempt = 1; hasFailed && attempt <= 3; attempt++) {
    logCaptureF("存在推送失败通道，3秒后第 %d 次重试\n", attempt);
    delay(3000);
    hasFailed = false;
    for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
      if (failed[i] && isPushChannelValid(config.pushChannels[i])) {
        bool ok = sendToChannel(config.pushChannels[i], sender, pushMessage.c_str(), pushTime.c_str());
        failed[i] = !ok;
        if (!ok) hasFailed = true;
        delay(20);
      }
    }
  }
  logCaptureLn(String("=== 多通道推送完成 ===\n"));
}
