#ifndef PUSH_H
#define PUSH_H

#include "globals.h"

void sendEmailNotification(const char* subject, const char* body);
void sendNotifyAll(const char* subject, const char* body);
String getSystemOverview();
String getEnabledPushNames();
String buildPushMessage(const char* sender, const char* message, const char* timestamp);
void sendSMSToServer(const char* sender, const char* message, const char* timestamp);
bool sendToChannel(const PushChannel& channel, const char* sender, const char* message, const char* timestamp);
String urlEncode(const String& str);
String jsonEscape(const String& str);
String dingtalkSign(const String& secret, int64_t timestamp);
int64_t getUtcMillis();

#endif
