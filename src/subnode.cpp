#include "common_defs.h"
#ifdef IS_SLAVE
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <DHT.h>

#define DHTPIN 10
#define DHTTYPE DHT22

BLEServer *pServer;
BLEService *pService;
BLECharacteristic *pTxCharacteristic; // 主发子收
BLECharacteristic *pRxCharacteristic; // 子发主收


DHT DHTSensor(DHTPIN, DHTTYPE);

bool deviceConnected = false;
bool oldDeviceConnected = false;



class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("主节点已连接");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("主节点断开连接");
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      if (value.length() > 0) {
        Serial.print("收到主节点消息: ");
        for (int i = 0; i < value.length(); i++)
          Serial.print(value[i]);
        Serial.println();
        
        // 模拟处理消息后回复
        if (deviceConnected) {
          String response = "Slave response @ " + String(millis()/1000);
          pRxCharacteristic->setValue(response.c_str());
          pRxCharacteristic->notify();
          Serial.println("已发送回复");
        }
      }
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("子节点启动...");
  
  // 设备名称带序号区分
  String deviceName = SLAVE_DEVICE_PREFIX + String(ESP.getEfuseMac() & 0xFFFF, HEX);
  BLEDevice::init(deviceName.c_str());
  
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  pService = pServer->createService(SERVICE_UUID);
  
  pTxCharacteristic = pService->createCharacteristic(
                      TX_CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_WRITE_NR
                    );
                      
  pRxCharacteristic = pService->createCharacteristic(
                      RX_CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  
  pTxCharacteristic->setCallbacks(new MyCallbacks());
  
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // 有助于iPhone连接
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("等待主节点连接...");


  DHTSensor.begin();

}

void loop() {
  // 处理断开重连
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // 给蓝牙堆栈一个处理时间
    pServer->startAdvertising();
    Serial.println("重新广播等待连接");
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  delay(1800);

  float t = DHTSensor.readTemperature();
  float h = DHTSensor.readHumidity();

  if (isnan(h) || isnan(t)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      return;
  }
  Serial.print(F("Humidity: "));
  Serial.print(h);
  Serial.print(F("%  Temperature: "));
  Serial.print(t);
  Serial.print(F("°C "));

  String message = "TEMP:" + String(t) + "\nHumidity:" + String(h);
  
  pRxCharacteristic->setValue(message.c_str()); 
  pRxCharacteristic->notify();
  
}
#endif