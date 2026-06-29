#include "scheduler.h"
#include "globals.h"
#include "config.h"
#include "modem.h"
#include "web_handlers.h"

#include <time.h>

#define SCHEDULE_TIMEZONE_OFFSET_SEC (8 * 3600)

static bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static uint8_t daysInMonth(int year, int month) {
  static const uint8_t days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (month == 2 && isLeapYear(year)) return 29;
  if (month < 1 || month > 12) return 31;
  return days[month - 1];
}

static uint32_t makeDayKey(const tm& t) {
  return (uint32_t)(t.tm_year + 1900) * 10000UL + (uint32_t)(t.tm_mon + 1) * 100UL + (uint32_t)t.tm_mday;
}

static bool shouldRunToday(const tm& t) {
  const ScheduledSmsConfig& scheduled = config.scheduledSms;

  if (scheduled.type == SCHEDULE_SMS_DAILY) return true;

  if (scheduled.type == SCHEDULE_SMS_WEEKLY) {
    uint8_t weekday = t.tm_wday == 0 ? 7 : t.tm_wday;
    return weekday == scheduled.weekday;
  }

  if (scheduled.type == SCHEDULE_SMS_MONTHLY) {
    uint8_t lastDay = daysInMonth(t.tm_year + 1900, t.tm_mon + 1);
    uint8_t targetDay = scheduled.monthDay > lastDay ? lastDay : scheduled.monthDay;
    return t.tm_mday == targetDay;
  }

  return false;
}

void checkScheduledSms() {
  ScheduledSmsConfig& scheduled = config.scheduledSms;
  if (!scheduled.enabled) return;
  if (scheduled.phone.length() == 0 || scheduled.content.length() == 0) return;

  time_t now = time(nullptr);
  if (now < 100000) return;
  if (!timeSynced) {
    timeSynced = true;
    logCaptureLn(String("NTP时间已可用，定时短信功能开始工作"));
  }

  time_t localNow = now + SCHEDULE_TIMEZONE_OFFSET_SEC;
  tm localTime;
  gmtime_r(&localNow, &localTime);

  if (localTime.tm_hour != scheduled.hour || localTime.tm_min != scheduled.minute) return;
  if (!shouldRunToday(localTime)) return;

  uint32_t dayKey = makeDayKey(localTime);
  if (scheduled.lastRunDayKey == dayKey) return;

  scheduled.lastRunDayKey = dayKey;
  saveConfig();

  logCaptureLn(String("触发定时短信发送"));
  logCaptureLn(String("目标号码: " + scheduled.phone));
  logCaptureLn(String("短信内容: " + scheduled.content));

  if (!modemReady) {
    logCaptureLn(String("定时短信跳过：模组未就绪"));
    return;
  }

  bool success = sendSMS(scheduled.phone.c_str(), scheduled.content.c_str());
  logCaptureLn(success ? String("定时短信发送成功") : String("定时短信发送失败"));
}
