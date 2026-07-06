#ifndef MODEM_H
#define MODEM_H

#include "globals.h"

String sendATCommand(const char* cmd, unsigned long timeout);
void modemPowerCycle();
void resetModule();
void modemInit();
void modemBackgroundTask();
bool sendATandWaitOK(const char* cmd, unsigned long timeout);
bool waitCEREG();
void blink_short(unsigned long gap_time = 500);
bool sendSMS(const char* phoneNumber, const char* message);
String getSimPhoneNumber();
bool writeSimPhoneNumber(const String& phoneNumber);
bool syncLocalPhoneFromSim(bool onlyIfEmpty);

#endif
