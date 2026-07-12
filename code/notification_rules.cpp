#include "notification_rules.h"
#include "globals.h"

static char lowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static uint32_t regexSteps = 0;
static const uint32_t REGEX_STEP_LIMIT = 30000;

static bool isWordChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

static int tokenLength(const char* pattern) {
  if (!pattern[0]) return 0;
  if (pattern[0] == '\\') return pattern[1] ? 2 : 1;
  if (pattern[0] == '[') {
    int i = 1;
    if (pattern[i] == '^') i++;
    while (pattern[i]) {
      if (pattern[i] == '\\' && pattern[i + 1]) i += 2;
      else if (pattern[i++] == ']') return i;
    }
  }
  return 1;
}

static bool classMatches(const char* token, int len, char value) {
  bool negate = len > 2 && token[1] == '^';
  bool matched = false;
  int i = negate ? 2 : 1;
  int end = len - 1;
  while (i < end) {
    char first = token[i];
    if (first == '\\' && i + 1 < end) {
      char kind = token[i + 1];
      if ((kind == 'd' && value >= '0' && value <= '9') ||
          (kind == 'w' && isWordChar(value)) ||
          (kind == 's' && (value == ' ' || value == '\t' || value == '\r' || value == '\n')) ||
          (kind != 'd' && kind != 'w' && kind != 's' && lowerAscii(kind) == lowerAscii(value))) {
        matched = true;
      }
      i += 2;
      continue;
    }
    if (i + 2 < end && token[i + 1] == '-') {
      char last = token[i + 2];
      char v = lowerAscii(value);
      if (v >= lowerAscii(first) && v <= lowerAscii(last)) matched = true;
      i += 3;
      continue;
    }
    if (lowerAscii(first) == lowerAscii(value)) matched = true;
    i++;
  }
  return negate ? !matched : matched;
}

static bool tokenMatches(const char* token, int len, char value) {
  if (!value) return false;
  if (token[0] == '.') return true;
  if (token[0] == '[') return classMatches(token, len, value);
  if (token[0] == '\\' && len >= 2) {
    char kind = token[1];
    if (kind == 'd') return value >= '0' && value <= '9';
    if (kind == 'w') return isWordChar(value);
    if (kind == 's') return value == ' ' || value == '\t' || value == '\r' || value == '\n';
    return lowerAscii(kind) == lowerAscii(value);
  }
  return lowerAscii(token[0]) == lowerAscii(value);
}

static bool matchHere(const char* pattern, const char* text);

static bool matchStar(const char* token, int len, const char* rest, const char* text) {
  const char* cursor = text;
  while (*cursor && tokenMatches(token, len, *cursor)) cursor++;
  while (true) {
    if (matchHere(rest, cursor)) return true;
    if (cursor == text) break;
    cursor--;
  }
  return false;
}

static bool matchHere(const char* pattern, const char* text) {
  if (++regexSteps > REGEX_STEP_LIMIT) return false;
  if (!pattern[0]) return true;
  if (pattern[0] == '$' && !pattern[1]) return !text[0];

  int len = tokenLength(pattern);
  if (len <= 0) return true;
  char quantifier = pattern[len];
  const char* rest = pattern + len + ((quantifier == '*' || quantifier == '+' || quantifier == '?') ? 1 : 0);

  if (quantifier == '*') return matchStar(pattern, len, rest, text);
  if (quantifier == '+') {
    if (!tokenMatches(pattern, len, *text)) return false;
    return matchStar(pattern, len, rest, text + 1);
  }
  if (quantifier == '?') {
    if (matchHere(rest, text)) return true;
    return tokenMatches(pattern, len, *text) && matchHere(rest, text + 1);
  }
  return tokenMatches(pattern, len, *text) && matchHere(rest, text + 1);
}

static bool matchAlternative(const String& pattern, const String& text) {
  const char* p = pattern.c_str();
  const char* t = text.c_str();
  if (p[0] == '^') return matchHere(p + 1, t);
  do {
    if (matchHere(p, t)) return true;
  } while (*t++);
  return false;
}

bool notificationRegexValid(const String& pattern, String* error) {
  if (pattern.length() == 0) {
    if (error) *error = "正则表达式不能为空";
    return false;
  }
  bool escaped = false;
  bool inClass = false;
  bool hasToken = false;
  bool lastWasQuantifier = false;
  bool alternativeHasContent = false;
  for (unsigned int i = 0; i < pattern.length(); i++) {
    char c = pattern[i];
    if (escaped) {
      escaped = false;
      hasToken = true;
      alternativeHasContent = true;
      lastWasQuantifier = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '[' && !inClass) {
      inClass = true;
      hasToken = true;
      alternativeHasContent = true;
      lastWasQuantifier = false;
      continue;
    }
    if (c == ']' && inClass) {
      inClass = false;
      continue;
    }
    if (!inClass && (c == '*' || c == '+' || c == '?') && (!hasToken || lastWasQuantifier)) {
      if (error) *error = "量词前缺少匹配内容";
      return false;
    }
    if (!inClass && (c == '*' || c == '+' || c == '?')) {
      lastWasQuantifier = true;
      continue;
    }
    if (!inClass && c == '|') {
      if (!alternativeHasContent) {
        if (error) *error = "正则表达式不能包含空分支";
        return false;
      }
      hasToken = false;
      alternativeHasContent = false;
      lastWasQuantifier = false;
    } else if (!inClass && c == '^') {
      if (alternativeHasContent) {
        if (error) *error = "^ 只能出现在分支开头";
        return false;
      }
      lastWasQuantifier = false;
    } else if (!inClass && c == '$') {
      if (i + 1 < pattern.length() && pattern[i + 1] != '|') {
        if (error) *error = "$ 只能出现在分支末尾";
        return false;
      }
      lastWasQuantifier = false;
    } else if (!inClass) {
      hasToken = true;
      alternativeHasContent = true;
      lastWasQuantifier = false;
    }
  }
  if (escaped) {
    if (error) *error = "正则表达式末尾不能只有反斜杠";
    return false;
  }
  if (inClass) {
    if (error) *error = "字符组缺少右方括号";
    return false;
  }
  if (!alternativeHasContent) {
    if (error) *error = "正则表达式不能以空分支结尾";
    return false;
  }
  return true;
}

bool notificationRegexMatch(const String& pattern, const String& text) {
  if (!notificationRegexValid(pattern, nullptr)) return false;
  regexSteps = 0;
  String boundedText = text.length() > 512 ? text.substring(0, 512) : text;
  int start = 0;
  bool escaped = false;
  bool inClass = false;
  for (int i = 0; i <= (int)pattern.length(); i++) {
    char c = i < (int)pattern.length() ? pattern[i] : '|';
    if (escaped) {
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '[') inClass = true;
    else if (c == ']') inClass = false;
    else if (c == '|' && !inClass) {
      String alternative = pattern.substring(start, i);
      if (alternative.length() && matchAlternative(alternative, boundedText)) return true;
      start = i + 1;
    }
  }
  return false;
}

static String hexEncode(const String& value) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  out.reserve(value.length() * 2);
  for (unsigned int i = 0; i < value.length(); i++) {
    uint8_t c = (uint8_t)value[i];
    out += hex[c >> 4];
    out += hex[c & 0x0F];
  }
  return out;
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static bool hexDecode(const String& value, String& out) {
  if (value.length() % 2 != 0) return false;
  out = "";
  out.reserve(value.length() / 2);
  for (unsigned int i = 0; i + 1 < value.length(); i += 2) {
    int hi = hexNibble(value[i]);
    int lo = hexNibble(value[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out += (char)((hi << 4) | lo);
  }
  return true;
}

static String fieldAt(const String& data, int index) {
  int start = 0;
  for (int i = 0; i < index; i++) {
    start = data.indexOf(';', start);
    if (start < 0) return String();
    start++;
  }
  int end = data.indexOf(';', start);
  if (end < 0) end = data.length();
  return data.substring(start, end);
}

String serializeNotificationRoute(const NotificationRouteConfig& route) {
  String data = String(route.filterEnabled ? 1 : 0) + ";" + String(route.defaultEmailMask) + ";" + String(route.defaultPushMask);
  for (int i = 0; i < MAX_NOTIFICATION_RULES; i++) {
    const NotificationMatchRule& rule = route.rules[i];
    data += ";" + String(rule.enabled ? 1 : 0);
    data += ";" + String(rule.emailMask);
    data += ";" + String(rule.pushMask);
    data += ";" + hexEncode(rule.name);
    data += ";" + hexEncode(rule.pattern);
  }
  return data;
}

bool deserializeNotificationRoute(const String& data, NotificationRouteConfig& route) {
  if (data.length() == 0) return false;
  int separators = 0;
  for (unsigned int i = 0; i < data.length(); i++) if (data[i] == ';') separators++;
  if (separators != 2 + MAX_NOTIFICATION_RULES * 5) return false;
  NotificationRouteConfig parsed;
  parsed.filterEnabled = fieldAt(data, 0).toInt() != 0;
  parsed.defaultEmailMask = (uint8_t)fieldAt(data, 1).toInt() & 0x07;
  parsed.defaultPushMask = (uint8_t)fieldAt(data, 2).toInt() & 0x1F;
  int field = 3;
  for (int i = 0; i < MAX_NOTIFICATION_RULES; i++) {
    parsed.rules[i].enabled = fieldAt(data, field++).toInt() != 0;
    parsed.rules[i].emailMask = (uint8_t)fieldAt(data, field++).toInt() & 0x07;
    parsed.rules[i].pushMask = (uint8_t)fieldAt(data, field++).toInt() & 0x1F;
    if (!hexDecode(fieldAt(data, field++), parsed.rules[i].name)) return false;
    if (!hexDecode(fieldAt(data, field++), parsed.rules[i].pattern)) return false;
    if (parsed.rules[i].name.length() > 40 || parsed.rules[i].pattern.length() > 160) return false;
    if (parsed.rules[i].enabled && !notificationRegexValid(parsed.rules[i].pattern, nullptr)) return false;
  }
  route = parsed;
  return true;
}

const char* notificationRouteName(NotificationRouteType route) {
  static const char* names[NOTIFY_ROUTE_COUNT] = {
    "系统消息", "短信", "电话", "其他"
  };
  return route < NOTIFY_ROUTE_COUNT ? names[route] : "未知消息";
}

void initNotificationRouteDefaults() {
  static const char* patterns[NOTIFY_ROUTE_COUNT] = {
    "开机|启动|上线|wifi|恢复|配置|设置|重启|reset|状态快照|短信发送",
    "验证码|校验码|动态码|短信码|verification[ ]*code|otp",
    ".*",
    ".*"
  };
  for (int i = 0; i < NOTIFY_ROUTE_COUNT; i++) {
    NotificationRouteConfig& route = config.notificationRoutes[i];
    route.filterEnabled = false;
    route.defaultEmailMask = 0x07;
    route.defaultPushMask = 0x1F;
    for (int j = 0; j < MAX_NOTIFICATION_RULES; j++) {
      route.rules[j].enabled = false;
      route.rules[j].emailMask = 0x07;
      route.rules[j].pushMask = 0x1F;
      route.rules[j].name = j == 0 ? String(notificationRouteName((NotificationRouteType)i)) + "默认规则" : String();
      route.rules[j].pattern = j == 0 ? patterns[i] : String();
    }
  }
}

NotificationTargets resolveNotificationTargets(NotificationRouteType routeType, const String& matchText) {
  NotificationTargets targets = {0, 0};
  if (routeType >= NOTIFY_ROUTE_COUNT) return targets;
  const NotificationRouteConfig& route = config.notificationRoutes[routeType];
  if (!route.filterEnabled) {
    targets.emailMask = route.defaultEmailMask & 0x07;
    targets.pushMask = route.defaultPushMask & 0x1F;
    return targets;
  }
  for (int i = 0; i < MAX_NOTIFICATION_RULES; i++) {
    const NotificationMatchRule& rule = route.rules[i];
    if (rule.enabled && rule.pattern.length() && notificationRegexMatch(rule.pattern, matchText)) {
      targets.emailMask |= rule.emailMask & 0x07;
      targets.pushMask |= rule.pushMask & 0x1F;
    }
  }
  return targets;
}
