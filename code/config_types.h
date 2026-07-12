#ifndef CONFIG_TYPES_H
#define CONFIG_TYPES_H

#include <Arduino.h>

// 推送通道类型
enum PushType {
  PUSH_TYPE_NONE = 0,      // 未启用
  PUSH_TYPE_POST_JSON = 1, // POST JSON格式 {"sender":"xxx","message":"xxx","timestamp":"xxx"}
  PUSH_TYPE_BARK = 2,      // Bark格式 POST {"title":"xxx","body":"xxx"}
  PUSH_TYPE_GET = 3,       // GET请求，参数放URL中
  PUSH_TYPE_DINGTALK = 4,  // 钉钉机器人
  PUSH_TYPE_PUSHPLUS = 5,  // PushPlus
  PUSH_TYPE_SERVERCHAN = 6,// Server酱
  PUSH_TYPE_CUSTOM = 7,    // 自定义模板
  PUSH_TYPE_FEISHU = 8,    // 飞书机器人
  PUSH_TYPE_GOTIFY = 9,    // Gotify
  PUSH_TYPE_TELEGRAM = 10  // Telegram Bot
};

// 最大推送通道数
#define MAX_PUSH_CHANNELS 5
#define MAX_NOTIFICATION_RULES 5

enum NotificationRouteType {
  NOTIFY_ROUTE_SYSTEM = 0,
  NOTIFY_ROUTE_INCOMING_SMS = 1,
  NOTIFY_ROUTE_INCOMING_CALL = 2,
  NOTIFY_ROUTE_OTHER = 3,
  NOTIFY_ROUTE_COUNT = 4
};

struct NotificationMatchRule {
  bool enabled;
  String name;
  String pattern;
  uint8_t emailMask;
  uint8_t pushMask;
};

struct NotificationRouteConfig {
  bool filterEnabled;
  uint8_t defaultEmailMask;
  uint8_t defaultPushMask;
  NotificationMatchRule rules[MAX_NOTIFICATION_RULES];
};

// 定时短信周期
enum ScheduledSmsType {
  SCHEDULE_SMS_DAILY = 0,    // 每天
  SCHEDULE_SMS_WEEKLY = 1,   // 每周
  SCHEDULE_SMS_MONTHLY = 2   // 每月
};

// 定时短信配置
struct ScheduledSmsConfig {
  bool enabled;
  ScheduledSmsType type;
  String phone;
  String content;
  uint8_t hour;
  uint8_t minute;
  uint8_t weekday;      // 1=周一, 7=周日
  uint8_t monthDay;     // 1-31，超过当月天数时按月末处理
  uint32_t lastRunDayKey;
};

struct ScheduledNotifyConfig {
  bool enabled;
  ScheduledSmsType type;
  String content;
  uint8_t hour;
  uint8_t minute;
  uint8_t weekday;
  uint8_t monthDay;
  uint32_t lastRunDayKey;
};

enum ScheduledModemRestartMode {
  MODEM_RESTART_SOFT = 0,
  MODEM_RESTART_HARD = 1,
  MODEM_RESTART_WHOLE_DEVICE = 2
};

struct ScheduledModemRestartConfig {
  bool enabled;
  ScheduledSmsType type;
  ScheduledModemRestartMode mode;
  uint8_t hour;
  uint8_t minute;
  uint8_t weekday;
  uint8_t monthDay;
  uint32_t lastRunDayKey;
};

// 推送通道配置（通用设计，支持多种推送方式）
struct PushChannel {
  bool enabled;           // 是否启用
  PushType type;          // 推送类型
  String name;            // 通道名称（用于显示）
  String url;             // 推送URL（webhook地址）
  String key1;            // 额外参数1（如：钉钉secret、pushplus token等）
  String key2;            // 额外参数2（备用）
  String customBody;      // 自定义请求体模板（使用 {sender} {message} {timestamp} 占位符）
};

// 配置参数结构体
struct Config {
  String smtpServer;
  int smtpPort;
  String smtpUser;
  String smtpPass;
  String smtpSendTo;
  String smtpServer2;
  int smtpPort2;
  String smtpUser2;
  String smtpPass2;
  String smtpSendTo2;
  String smtpServer3;
  int smtpPort3;
  String smtpUser3;
  String smtpPass3;
  String smtpSendTo3;
  String adminPhone;
  String adminSmsWhitelist;
  String localPhone;
  PushChannel pushChannels[MAX_PUSH_CHANNELS];  // 多推送通道
  String webUser;      // Web管理账号
  String webPass;      // Web管理密码
  String numberBlackList;  // 号码黑名单（换行符分隔）
  String wifiSsid;     // 主 WiFi SSID
  String wifiPass;     // 主 WiFi 密码
  String wifiBackupSsid1; // 备用 WiFi A SSID
  String wifiBackupPass1; // 备用 WiFi A 密码
  String wifiBackupSsid2; // 备用 WiFi B SSID
  String wifiBackupPass2; // 备用 WiFi B 密码
  ScheduledSmsConfig scheduledSms;  // 定时短信
  ScheduledNotifyConfig scheduledNotify;
  ScheduledModemRestartConfig scheduledModemRestart;
  NotificationRouteConfig notificationRoutes[NOTIFY_ROUTE_COUNT];
};

// 默认Web管理账号密码
#define DEFAULT_WEB_USER "admin"
#define DEFAULT_WEB_PASS "admin123"

// 长短信合并相关定义
#define MAX_CONCAT_PARTS 10       // 最大支持的长短信分段数
#define CONCAT_TIMEOUT_MS 180000  // 长短信等待超时时间(毫秒)
#define MAX_CONCAT_MESSAGES 5     // 最多同时缓存的长短信组数

// 长短信分段结构
struct SmsPart {
  bool valid;           // 该分段是否有效
  bool pushed;          // 该分段是否已单独推送
  String text;          // 分段内容
};

// 长短信缓存结构
struct ConcatSms {
  bool inUse;                           // 是否正在使用
  int refNumber;                        // 参考号
  String sender;                        // 发送者
  String timestamp;                     // 时间戳（使用第一个收到的分段的时间戳）
  int totalParts;                       // 总分段数
  int receivedParts;                    // 已收到的分段数
  unsigned long firstPartTime;          // 收到第一个分段的时间
  SmsPart parts[MAX_CONCAT_PARTS];      // 各分段内容
};

#endif
