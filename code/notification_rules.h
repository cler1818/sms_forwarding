#ifndef NOTIFICATION_RULES_H
#define NOTIFICATION_RULES_H

#include "config_types.h"

struct NotificationTargets {
  uint8_t emailMask;
  uint8_t pushMask;
};

void initNotificationRouteDefaults();
NotificationTargets resolveNotificationTargets(NotificationRouteType route, const String& matchText);
bool notificationRegexValid(const String& pattern, String* error = nullptr);
bool notificationRegexMatch(const String& pattern, const String& text);
String serializeNotificationRoute(const NotificationRouteConfig& route);
bool deserializeNotificationRoute(const String& data, NotificationRouteConfig& route);
const char* notificationRouteName(NotificationRouteType route);

#endif
