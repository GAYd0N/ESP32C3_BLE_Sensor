#include "common_defs.h"
#ifdef IS_MASTER
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "common_defs.h"

// 已连接的子节点结构体
struct SlaveDevice {
  BLEAdvertisedDevice* device;
  BLEClient* client;
  BLERemoteCharacteristic* txCharacteristic; // 主发子收
  BLERemoteCharacteristic* rxCharacteristic; // 子发主收
  bool connected;
};

SlaveDevice slaves[MAX_SLAVES];
int connectedSlaves = 0;

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // 检查是否为子节点设备
    if (advertisedDevice.getName().find(SLAVE_DEVICE_PREFIX) != std::string::npos) {
      Serial.printf("发现子节点: %s \n", advertisedDevice.toString().c_str());
      
      // 检查是否已连接最大数量的子节点
      if (connectedSlaves < MAX_SLAVES) {
        bool alreadyAdded = false;
        
        // 检查是否已添加该设备
        for (int i = 0; i < connectedSlaves; i++) {
          if (slaves[i].device->getAddress().equals(advertisedDevice.getAddress())) {
            alreadyAdded = true;
            break;
          }
        }
        
        if (!alreadyAdded) {
          slaves[connectedSlaves].device = new BLEAdvertisedDevice(advertisedDevice);
          connectedSlaves++;
          Serial.println("已添加到连接队列");
        }
      } else {
        Serial.println("已达到最大子节点连接数");
      }
    }
  }
};

void connectToSlave(SlaveDevice &slave) {
  Serial.printf("尝试连接子节点: %s\n", slave.device->getAddress().toString().c_str());
  
  // 创建客户端
  slave.client = BLEDevice::createClient();
  slave.client->connect(slave.device);
  
  // 获取服务
  BLERemoteService* pRemoteService = slave.client->getService(SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.println("未找到服务");
    return;
  }
  
  // 获取特征
  slave.txCharacteristic = pRemoteService->getCharacteristic(TX_CHARACTERISTIC_UUID);
  slave.rxCharacteristic = pRemoteService->getCharacteristic(RX_CHARACTERISTIC_UUID);
  
  if (slave.txCharacteristic == nullptr || slave.rxCharacteristic == nullptr) {
    Serial.println("未找到特征");
    return;
  }
  
  // 注册接收回调
  slave.rxCharacteristic->registerForNotify([](BLERemoteCharacteristic* pBLERemoteCharacteristic, 
                                             uint8_t* pData, size_t length, bool isNotify) {
    String data((char*)pData, length);
    String address = pBLERemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress().toString().c_str();
    Serial.printf("从 %s 收到数据: %s\n", address.c_str(), data.c_str());
  });
  
  slave.connected = true;
  Serial.println("连接成功");
}

void sendToSlave(SlaveDevice &slave, String message) {
  if (slave.connected && slave.txCharacteristic != nullptr) {
    slave.txCharacteristic->writeValue(message.c_str(), message.length());
    Serial.printf("已发送到 %s: %s\n", 
                 slave.device->getAddress().toString().c_str(), 
                 message.c_str());
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("主节点启动...");
  
  BLEDevice::init(MASTER_DEVICE_NAME);
  
  // 开始扫描
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->start(10, false); // 扫描10秒
  
  // 连接发现的子节点
  for (int i = 0; i < connectedSlaves; i++) {
    connectToSlave(slaves[i]);
  }
}

void loop() {
  static uint32_t lastSendTime = 0;
  
  // 每5秒向所有子节点发送消息
  if (millis() - lastSendTime > 5000) {
    lastSendTime = millis();
    
    for (int i = 0; i < connectedSlaves; i++) {
      if (slaves[i].connected) {
        String message = "Master->Slave " + String(i+1) + " @ " + String(millis()/1000);
        sendToSlave(slaves[i], message);
      }
    }
  }
  
  // 处理其他任务
  delay(100);
}
#endif