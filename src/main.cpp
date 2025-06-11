#include "common_defs.h"
#ifdef IS_MASTER
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLE2902.h>
#include <BLEAdvertisedDevice.h>

// 已连接的子节点结构体
struct ClientDevice {
  uint16_t connId = 0;
  bool connected = false;
  SensorData Data;
};
BLECharacteristic *pTxCharacteristic;
BLECharacteristic *pRxCharacteristic;
BLEAdvertising *pAdvertising;
BLEServer *pServer;
BLEService *pService;
ClientDevice* clients[3];

float tempThreshold = 25.0;
bool isAdvertising = false;
int connectedDevices = 0;
// 服务器回调类
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
        if (param == nullptr || connectedDevices >= MAX_SLAVES) return;
        uint16_t connId = param->connect.conn_id;
        for (auto client : clients) {
          if (client == nullptr)
            client = new ClientDevice;
          if (client->connected) {
            continue;
          }
          client->connId = connId;
          client->connected = true;
        }
        connectedDevices++;
        Serial.printf("客户端已连接");
        Serial.println(connId);
        Serial.print("当前连接客户端数量: ");
        Serial.println(connectedDevices);
    };

    void onDisconnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
        // 从列表中移除断开连接的客户端
        for (auto client : clients) {
          if (client == nullptr)
            continue;
          if (client->connected && client->connId == pServer->getConnId()) {
            delete client;
            break;
          }
        }
        connectedDevices--;
        Serial.print("客户端已断开: ");
        Serial.println(param->connect.conn_id);
        Serial.print("剩余连接客户端数量: ");
        Serial.println(connectedDevices);
    }
};

// 接收数据回调类
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic, esp_ble_gatts_cb_param_t* param) {
      uint16_t connId = param->connect.conn_id;
      uint8_t* data = param->write.value;
      uint16_t length = param->write.len;
      SensorData* SData = reinterpret_cast<SensorData*>(data);

      for (auto client : clients) {
        if (client->connected && client->connId == connId) {
          if (SData == nullptr || sizeof(SData) < sizeof(SensorData)) {
              // 处理数据不足或空指针情况
              continue;
          }
          Serial.printf("T: %.2f, H: %.2f, I: %d, connId: \n", SData->temperature, SData->humidity, SData->isHeating, connId);
          client->Data.temperature = SData->temperature;
          client->Data.humidity = SData->humidity;
          client->Data.isHeating = SData->isHeating;
        }
      }
    }
};

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
        tempThreshold = newThreshold;
        Serial.printf("温度阈值已更新为: %.1f°C\n", tempThreshold);
      } 
      else {
        Serial.println("错误: 温度阈值必须在0-50°C之间");
      }
    }
    else if (command == "GET_THRESHOLD") {
      Serial.printf("当前温度阈值: %.1f°C\n", tempThreshold);
    } 
    else if (command == "GET_STATUS") {
      float temp = 0;
      for (int i = 0; i < connectedDevices; i++) {
        if (clients[i] == nullptr) {
          continue;
        }
        float t = clients[i]->Data.temperature;
        float h = clients[i]->Data.humidity;
        bool s = clients[i]->Data.isHeating;
        temp += t;
        Serial.printf("当前状态 - 节点%d: %.1f°C, 湿度: %.1f%, 加热: %s\n",
        i, clients[i]->Data.temperature, clients[i]->Data.humidity, clients[i]->Data.isHeating ? "开启" : "关闭");
      }
      if (connectedDevices) {
        temp = temp / connectedDevices;
      }
      Serial.printf("温度平均值: %.1f, 阈值: %.1f", temp, tempThreshold);

    } 
    else if (command == "HELP") {
      Serial.println("可用命令:");
      Serial.println("SET_THRESHOLD XX.X - 设置温度阈值(0-50°C)");
      Serial.println("GET_THRESHOLD - 获取当前温度阈值");
      Serial.println("GET_STATUS - 获取当前系统状态");
      Serial.println("HELP - 显示帮助信息");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(300);
  Serial.println("主节点启动...");

  BLEDevice::init(MASTER_DEVICE_NAME);
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
                        BLECharacteristic::PROPERTY_WRITE
                      );
  pRxCharacteristic->setCallbacks(new MyCallbacks());
  // 启动服务
  pService->start();
  // 开始广播
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("等待客户端连接...");
}

void loop() {
  static uint32_t lastSendTime = 0;

  handleSerialCommands();

  if (millis() - lastSendTime > 2000) {
    lastSendTime = millis();
    Serial.printf("%d, Test\n",millis());
  }
  
  // 处理其他任务
  delay(100);
}

#endif