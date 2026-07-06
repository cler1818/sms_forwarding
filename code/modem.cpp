#include "modem.h"
#include "web_handlers.h"
#include "config.h"

static String normalizeSimPhone(String phone) {
  phone.trim();
  if (phone.startsWith("+86")) phone = phone.substring(3);
  return phone;
}

// 发送AT命令并获取响应
String sendATCommand(const char* cmd, unsigned long timeout) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < timeout) {
    if (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0) {
        // 读取剩余数据（最多 50ms）
        unsigned long t = millis();
        while (millis() - t < 50) {
          if (Serial1.available()) resp += (char)Serial1.read();
          server.handleClient();
        }
        return resp;
      }
    }
    server.handleClient();
  }
  return resp;
}

String getSimPhoneNumber() {
  String resp = sendATCommand("AT+CNUM", 3000);
  int first = resp.indexOf('"');
  while (first >= 0) {
    int second = resp.indexOf('"', first + 1);
    if (second < 0) break;
    int third = resp.indexOf('"', second + 1);
    int fourth = third >= 0 ? resp.indexOf('"', third + 1) : -1;
    if (third >= 0 && fourth > third) {
      String n = resp.substring(third + 1, fourth);
      n.trim();
      if (n.length() >= 5) return normalizeSimPhone(n);
      first = fourth + 1;
    } else {
      String n = resp.substring(first + 1, second);
      n.trim();
      if (n.length() >= 5 && n[0] >= '0' && n[0] <= '9') return normalizeSimPhone(n);
      first = second + 1;
    }
  }
  return "";
}

bool writeSimPhoneNumber(const String& phoneNumber) {
  String n = phoneNumber;
  n.trim();
  if (n.length() == 0) return false;
  int type = n.startsWith("+") ? 145 : 129;
  String resp = sendATCommand("AT+CPBS=\"ON\"", 2000);
  if (resp.indexOf("OK") < 0) {
    logCaptureLn(String("SIM本机号码写入不支持或选择ON电话本失败"));
    return false;
  }
  String cmd = "AT+CPBW=1,\"" + n + "\"," + String(type) + ",\"LOCAL\"";
  resp = sendATCommand(cmd.c_str(), 3000);
  bool ok = resp.indexOf("OK") >= 0;
  logCaptureLn(ok ? String("SIM本机号码写入完成") : String("SIM本机号码写入失败，模组或SIM可能不支持"));
  return ok;
}

bool syncLocalPhoneFromSim(bool onlyIfEmpty) {
  if (onlyIfEmpty && config.localPhone.length()) return false;
  String n = getSimPhoneNumber();
  if (n.length() == 0) {
    logCaptureLn(String("未从SIM卡读取到本机号码"));
    return false;
  }
  if (config.localPhone == n) return false;
  config.localPhone = n;
  saveConfig();
  logCaptureLn(String("已从SIM卡读取并保存本机号码: ") + n);
  return true;
}

// 新增"模组断电重启"函数
void modemPowerCycle() {
  pinMode(MODEM_EN_PIN, OUTPUT);

  logCaptureLn(String("EN 拉低：关闭模组"));
  digitalWrite(MODEM_EN_PIN, LOW);
  delay(1200);  // 关机时间给够

  logCaptureLn(String("EN 拉高：开启模组"));
  digitalWrite(MODEM_EN_PIN, HIGH);
  delay(6000);  // 等模组完全启动再发AT（关键）
}

// 重启模组（EN引脚断电重启 + 重新初始化）
void resetModule() {
  logCaptureLn(String("正在硬重启模组（EN 断电重启）..."));
  modemPowerCycle();
  modemInit();
}

// 模组 AT 初始化流程（setup 中调用，resetModule 后也调用）
void modemInit() {
  // 清掉上电噪声/残留
  while (Serial1.available()) Serial1.read();

  int retry = 0;
  while (!sendATandWaitOK("AT", 1000) && retry++ < 5) {
    logCaptureLn(String("AT未响应，重试..."));
    blink_short();
  }
  if (retry > 5) {
    modemReady = false;
    logCaptureLn(String("模组未响应，后台稍后重试"));
    return;
  }
  logCaptureLn(String("模组AT响应正常"));

  //判断型号，做一些特定操作
  bool need_set_CGACT = true;
  String resp = sendATCommand("ATI", 2000);
  logCaptureLn(String("ATI响应: " + resp));
  if (resp.indexOf("OK") >= 0) {
    // 解析ATI响应
    String manufacturer = "未知";
    String model = "未知";
    String version = "未知";
    
    // 按行解析
    int lineStart = 0;
    int lineNum = 0;
    for (int i = 0; i < resp.length(); i++) {
      if (resp.charAt(i) == '\n' || i == resp.length() - 1) {
        String line = resp.substring(lineStart, i);
        line.trim();
        if (line.length() > 0 && line != "ATI" && line != "OK") {
          lineNum++;
          if (lineNum == 1) manufacturer = line;
          else if (lineNum == 2) model = line;
          else if (lineNum == 3) version = line;
        }
        lineStart = i + 1;
      }
    }
    //这个模组这条命令有bug
    if(model == "ML307Y") need_set_CGACT = false;
  }

  if(need_set_CGACT) {
    if (sendATandWaitOK("AT+CGACT=0,1", 3000)) logCaptureLn(String("已禁用数据连接(AT+CGACT=0,1)，防止流量消耗"));
    else logCaptureLn(String("设置CGACT失败，跳过"));
  } else {
    logCaptureLn(String("该型号无法配置(AT+CGACT=0,1)，跳过该命令，会不会消耗流量？自求多福"));
  }
  retry = 0;
  while (!sendATandWaitOK("AT+CNMI=2,2,0,0,0", 1000) && retry++ < 3) {
    logCaptureLn(String("设置CNMI失败，重试..."));
    blink_short();
  }
  logCaptureLn(String("CNMI参数设置完成"));
  retry = 0;
  while (!sendATandWaitOK("AT+CMGF=0", 1000) && retry++ < 3) {
    logCaptureLn(String("设置PDU模式失败，重试..."));
    blink_short();
  }
  logCaptureLn(String("PDU模式设置完成"));
  if (sendATandWaitOK("AT+CLIP=1", 1000)) {
    logCaptureLn(String("来电号码显示已开启"));
  } else {
    logCaptureLn(String("来电号码显示开启失败，跳过"));
  }
  if (waitCEREG()) {
    logCaptureLn(String("网络已注册"));
    modemReady = true;
    syncLocalPhoneFromSim(true);
  } else {
    logCaptureLn(String("模组未插SIM卡或未注册，后台继续检测"));
    modemReady = false;
  }
}

void modemBackgroundTask() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 15000) return;
  lastCheck = millis();
  if (modemReady) return;
  if (waitCEREG()) {
    modemReady = true;
    logCaptureLn(String("网络已注册"));
    syncLocalPhoneFromSim(true);
  } else {
    logCaptureLn(String("模组未插SIM卡或未注册"));
  }
}

void blink_short(unsigned long gap_time) {
  digitalWrite(LED_BUILTIN, LOW);
  delay(50);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(gap_time);
}

bool sendATandWaitOK(const char* cmd, unsigned long timeout) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < timeout) {
    if (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (resp.indexOf("OK") >= 0) return true;
      if (resp.indexOf("ERROR") >= 0) return false;
    }
    server.handleClient();
  }
  return false;
}

// 检测网络注册状态（LTE/4G）
// CEREG状态: 1=已注册本地, 5=已注册漫游
bool waitCEREG() {
  Serial1.println("AT+CEREG?");
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < 2000) {
    if (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (resp.indexOf("+CEREG:") >= 0) {
        if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) return true;
        if (resp.indexOf(",0") >= 0 || resp.indexOf(",2") >= 0 || 
            resp.indexOf(",3") >= 0 || resp.indexOf(",4") >= 0) return false;
      }
    }
    server.handleClient();
  }
  return false;
}

// 发送短信（PDU模式）
bool sendSMS(const char* phoneNumber, const char* message) {
  logCaptureLn(String("准备发送短信..."));
  logCapture(String("目标号码: ")); logCaptureLn(String(phoneNumber));
  logCapture(String("短信内容: ")); logCaptureLn(String(message));

  // 使用pdulib编码PDU
  pdu.setSCAnumber();  // 使用默认短信中心
  int pduLen = pdu.encodePDU(phoneNumber, message);
  
  if (pduLen < 0) {
    logCapture(String("PDU编码失败，错误码: "));
    logCaptureLn(String(pduLen));
    return false;
  }
  
  logCapture(String("PDU数据: ")); logCaptureLn(String(pdu.getSMS()));
  logCapture(String("PDU长度: ")); logCaptureLn(String(pduLen));
  
  // 发送AT+CMGS命令
  String cmgsCmd = "AT+CMGS=";
  cmgsCmd += pduLen;
  
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmgsCmd);
  
  // 等待 > 提示符
  unsigned long start = millis();
  bool gotPrompt = false;
  while (millis() - start < 5000) {
    if (Serial1.available()) {
      char c = Serial1.read();
      logCapture(String(c));
      if (c == '>') {
        gotPrompt = true;
        break;
      }
    }
    server.handleClient();
  }
  
  if (!gotPrompt) {
    logCaptureLn(String("未收到>提示符"));
    return false;
  }
  
  // 发送PDU数据
  Serial1.print(pdu.getSMS());
  Serial1.write(0x1A);  // Ctrl+Z 结束
  
  // 等待响应
  start = millis();
  String resp = "";
  while (millis() - start < 30000) {
    while (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      logCapture(String(c));
      if (resp.indexOf("OK") >= 0) {
        logCaptureLn(String("\n短信发送成功"));
        return true;
      }
      if (resp.indexOf("ERROR") >= 0) {
        logCaptureLn(String("\n短信发送失败"));
        return false;
      }
    }
    server.handleClient();
  }
  logCaptureLn(String("短信发送超时"));
  return false;
}
