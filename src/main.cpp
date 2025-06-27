#include "common_defs.h"
#ifdef IS_SERVER
// #include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLE2902.h>
#include <BLEAdvertisedDevice.h>

// 已连接的子节点结构体
struct ClientDevice {
  uint16_t connId;
  bool connected;
  SensorData Data;
  String jsonStr;
};

BLECharacteristic *pTxCharacteristic;
BLECharacteristic *pRxCharacteristic;
BLEAdvertising *pAdvertising;
BLEServer *pServer;
BLEService *pService;

ClientDevice Clients[3];
CommandData CData;
uint32_t connectedDevices = 0;

void sendLargeData(BLECharacteristic* pChar, const std::string& data) {
  const size_t chunkSize = DEFAULT_MTU;
  size_t length = data.length();
  
  for(size_t i = 0; i < length; i += chunkSize) {
    size_t end = (i + chunkSize > length) ? length : i + chunkSize;
    std::string chunk = data.substr(i, end - i);
    pChar->setValue(chunk);
    pChar->notify();
    delay(10); // 给接收方处理时间
  }
  pChar->setValue("\n");
  pChar->notify();
}

void notifyAllClients(JsonDocument& jsonDoc) {
  static bool isRunning = false;
  if (!connectedDevices || isRunning)
    return;
  isRunning = true;
  std::string jsonStr;
  serializeJson(jsonDoc, jsonStr);
  if (jsonStr.length() < DEFAULT_MTU)
  {
    pTxCharacteristic->setValue(jsonStr + "\n");
    pTxCharacteristic->notify();
    Serial.printf("Sent JSON: %s\n", jsonStr.c_str());
  }
  else 
  {
    sendLargeData(pTxCharacteristic, jsonStr);
  }
  isRunning = false;
}

void SendCommandJson() {
  JsonDocument jsonDoc;

  jsonDoc["tempThreshold"] = CData.tempThreshold;
  jsonDoc["heaterOverride"] = CData.heaterOverride;
  jsonDoc["heater"] = CData.heater;
  notifyAllClients(jsonDoc);
}

// 服务器回调类
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
        uint16_t connId = param->connect.conn_id;
        if (connectedDevices >= MAX_CLIENTS || connId >= MAX_CLIENTS) {
          pServer->disconnect(connId);
          return;
        }

        Clients[connId].connId = connId;
        Clients[connId].connected = true;
        SendCommandJson();

        connectedDevices++;
        Serial.printf("客户端已连接, connId: ");
        Serial.println(connId);
        Serial.print("当前连接客户端数量: ");
        Serial.println(connectedDevices); 
        BLEDevice::startAdvertising();
        Serial.println("重启广播");
    }

    void onDisconnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
        uint16_t connId = param->connect.conn_id;
        if (Clients[connId].connected && Clients[connId].connId == connId) {
          Clients[connId].connected = false;
          Clients[connId].connId = 0;
          Clients[connId].jsonStr.clear();
          Clients[connId].Data.heater = false;
          Clients[connId].Data.humidity = 0;
          Clients[connId].Data.temperature = 0;
          Clients[connId].Data.tempThreshold = 25.0;
        }
        
        connectedDevices--;
        Serial.print("客户端已断开: ");
        Serial.println(connId);
        Serial.print("剩余连接客户端数量: ");
        Serial.println(connectedDevices);

        BLEDevice::startAdvertising();
        Serial.println("重启广播");
    }
};

// 接收数据回调类
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic, esp_ble_gatts_cb_param_t* param) {
      uint16_t connId = param->connect.conn_id;
      uint8_t* pData = param->write.value;
      uint16_t length = param->write.len;
      if (!pData || !length || connId >= MAX_CLIENTS)
        return;

      // Serial.printf("DATA: %S\t", String(pData, length));
      // Serial.printf("CONN_ID: %d\t", connId);
      // Serial.printf("length: %d\n", length);

      if (Clients[connId].connected && Clients[connId].connId == connId) {
        Clients[connId].jsonStr.concat(pData, length);
        if(Clients[connId].jsonStr.endsWith("\n")) {
          // Serial.println("完整数据接收完成:");
          // Serial.println(client.jsonStr.c_str());
          if(!Clients[connId].jsonStr.startsWith("{") || !Clients[connId].jsonStr.endsWith("}\n")) {
            Serial.println("错误: 数据格式不符合JSON要求");
            Clients[connId].jsonStr.clear();
            return;
          }
        }
        else {
          return;
        }
      }

      JsonDocument jsonDoc;
      DeserializationError error = deserializeJson(jsonDoc, Clients[connId].jsonStr);
      if (error) {
          Serial.printf("JSON 解析失败: %s\n", error.c_str());
      } else {
          // Serial.printf("Received JSON from %d: %s\n", connId, it.receivedStr.c_str());
          String name = jsonDoc["name"];
          Clients[connId].Data.temperature = jsonDoc["temperature"];
          Clients[connId].Data.humidity = jsonDoc["humidity"];
          Clients[connId].Data.tempThreshold = jsonDoc["tempThreshold"];
          Clients[connId].Data.heater = jsonDoc["heater"];
          // Serial.printf("%s: T: %.2f, H: %.2f, TH: %.2f, HEATER: %d, connId: %d\n", 
          //   name.c_str(), Clients[connId].Data.temperature, Clients[connId].Data.humidity, Clients[connId].Data.tempThreshold, Clients[connId].Data.heater ? 1 : 0, connId);
          Clients[connId].jsonStr.clear();
      }
    }
};

void ShowStatus() {
  for (auto &it : Clients) {
    if (!Clients->connected)
      continue;
    Serial.printf("当前状态 - 节点%d: 温度: %.1f°C, 湿度: %.1f, 阈值: %.1f, 加热: %s\n",
    it.connId, it.Data.temperature, it.Data.humidity, it.Data.tempThreshold, it.Data.heater ? "开启" : "关闭");
  }
  Serial.printf("温度阈值: %.1f, 加热器自动模式: %s\n", CData.tempThreshold, CData.heaterOverride ? "OFF" : "ON");
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command.startsWith("SET_THRESHOLD ")) {
      if (command.length() <= 14) { // 检查是否有参数
        Serial.println("错误: 缺少参数");
        return;
      }
      String numStr = command.substring(14);
      for (char c : numStr) {
          if (!isdigit(c) && c != '.') { // 检查是否仅含数字和小数点
              Serial.println("错误: 非法字符");
              return;
          }
      }

      float newThreshold = numStr.toFloat();

      if (newThreshold >= 0 && newThreshold <= 50) { // 合理温度范围检查
        CData.tempThreshold = newThreshold;
        SendCommandJson();
        Serial.printf("温度阈值已更新为: %.1f°C\n", CData.tempThreshold);
      }
      else {
        Serial.println("错误: 温度阈值必须在0-50°C之间");
      }
    }
    else if (command == "GET_THRESHOLD") {
      Serial.printf("当前温度阈值: %.1f°C\n", CData.tempThreshold);
    } 
    else if (command == "HEATER_START") {
      CData.heaterOverride = true;
      CData.heater = true;
      SendCommandJson();
      Serial.println("固定开启加热器");
    }
    else if (command == "HEATER_STOP") {
      CData.heaterOverride = true;
      CData.heater = false;
      SendCommandJson();
      Serial.println("固定关闭加热器");
    }
    else if (command == "HEATER_AUTO") {
      CData.heaterOverride = false;
      SendCommandJson();
      Serial.println("加热器自动模式");
    }
    else if (command == "STATUS") {
      ShowStatus();
    }
    else if (command == "HELP") {
      Serial.println("可用命令:");
      Serial.println("SET_THRESHOLD XX.X - 设置温度阈值(0-50°C)");
      Serial.println("GET_THRESHOLD - 获取当前温度阈值");
      Serial.println("STATUS - 获取当前系统状态");
      Serial.println("HEATER_START - 开启加热器");
      Serial.println("HEATER_STOP - 关闭加热器");
      Serial.println("HELP - 显示帮助信息");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(300);
  Serial.println("主节点启动...");

  BLEDevice::init(SERVER_DEVICE_NAME);
  // 创建BLE服务器
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  // 创建BLE服务
  pService = pServer->createService(SERVICE_UUID);
  // 创建用于发送的特征
  pTxCharacteristic = pService->createCharacteristic(
                        TX_CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_NOTIFY |
                        BLECharacteristic::PROPERTY_INDICATE
                      );
  pTxCharacteristic->addDescriptor(new BLE2902());
  // 创建用于接收的特征
  pRxCharacteristic = pService->createCharacteristic(
                        RX_CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_WRITE |
                        BLECharacteristic::PROPERTY_WRITE_NR
                      );
  pRxCharacteristic->setCallbacks(new MyCallbacks());
  // 启动服务
  pService->start();
  // 开始广播
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinInterval(0x20); // 建议设置合理的广播间隔
  pAdvertising->setMaxInterval(0x40);
  pAdvertising->setMinPreferred(0x20);
  
  BLEDevice::startAdvertising();
  Serial.println("等待客户端连接...");
}

void loop() {
  static uint32_t lastSendTime = 0;
  uint32_t msTime = millis();
  if (msTime - lastSendTime > 2000) {
    lastSendTime = msTime;
    // Serial.printf("%d, Test\n",msTime);
    ShowStatus();
  }
  handleSerialCommands();
  delay(100);
}

#endif