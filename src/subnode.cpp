#include "common_defs.h"
#ifdef IS_SLAVE
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <DHT.h>

#define DHTPIN 10
#define DHTTYPE DHT22
#define LED 0

DHT DHTSensor(DHTPIN, DHTTYPE);

String deviceName;
String message;

SensorData Data;
CommandData CData;
// 标志变量
bool deviceFound = false;
BLEAddress pServerAddress(SERVER_MAC);
BLEClient *pClient = nullptr;
BLEScan* pBLEScan = nullptr;
BLERemoteService *pRemoteService = nullptr;
BLERemoteCharacteristic *pTxRemoteCharacteristic = nullptr;
BLERemoteCharacteristic *pRxRemoteCharacteristic = nullptr;
BLEUUID serviceUUID(SERVICE_UUID);
BLEUUID charUUID(RX_CHARACTERISTIC_UUID);

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pClient) {
    Serial.println("连接到服务器成功");
  }

  void onDisconnect(BLEClient* pClient) {
    Serial.println("与服务器断开连接");
  }
};

void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify) {
  Serial.printf("BLE Callback: pData=%p, length=%d, isNotify=%d\n", 
              pData, length, isNotify); 
  if (pData == nullptr || length != sizeof(CommandData)) {
      return;
  }
  Serial.println("continue");
  CommandData cmdData;
  memcpy(&cmdData, pData, sizeof(CommandData));
  Serial.printf("tempThreshold: %.2f, heater: %s, I: %d\n", cmdData.tempThreshold, cmdData.heater);
  CData.heater = cmdData.heater;
  CData.tempThreshold = cmdData.tempThreshold;

  Serial.print("Notify callback for characteristic ");
  Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
  Serial.print(" of data length ");
  Serial.println(length);
  Serial.print("data: ");
  Serial.write(pData, length);
  Serial.println();
}
// 扫描回调类
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.print("地址: ");
      Serial.print(advertisedDevice.getAddress().toString().c_str());
      Serial.printf(" %s\n", advertisedDevice.haveName() ? advertisedDevice.getName().c_str() : "Unknown");
      if (advertisedDevice.haveName() && advertisedDevice.getName() == SERVER_DEVICE_NAME) {
        Serial.println("已找到服务器");
        advertisedDevice.getScan()->stop();
        deviceFound = true;
      }
    }
};



void connectToServer() {
    pClient->connect(pServerAddress);
    Serial.printf("%d, 连接设备中, %s\n", millis(), pServerAddress.toString().c_str());
    delay(500);

    pRemoteService = pClient->getService(SERVICE_UUID);
    delay(100);

    if (pRemoteService != nullptr) {
      Serial.println("Found our service!");
    }
    else {
      Serial.println("Cant Find our service!");
    }

    pTxRemoteCharacteristic = pRemoteService->getCharacteristic(TX_CHARACTERISTIC_UUID);
    pRxRemoteCharacteristic = pRemoteService->getCharacteristic(RX_CHARACTERISTIC_UUID);

    delay(100);

    if (pTxRemoteCharacteristic != nullptr && pRxRemoteCharacteristic != nullptr) {
      Serial.println("Found our characteristic!");

      // 读取特征值
      if (pTxRemoteCharacteristic->canRead()) {
        std::string value = pTxRemoteCharacteristic->readValue();
        Serial.print("Received: ");
        Serial.println(value.c_str());
      }
      if (pTxRemoteCharacteristic->canNotify()) {
        pTxRemoteCharacteristic->registerForNotify(notifyCallback);
      }

      // 如果可以写入，也可以发送数据
      if (pRxRemoteCharacteristic->canWrite()) {
        String data = "Hello from " + deviceName;
        pRxRemoteCharacteristic->writeValue((uint8_t*)data.c_str(), data.length());
      }
    }
    else {
      Serial.println("Cant Find our char!");
    }
}

void setup() {
  Serial.begin(115200);
  Serial.println("子节点启动...");

  pinMode(0, OUTPUT);

  // 设备名称带序号区分
  deviceName = CLIENT_DEVICE_PREFIX + String(ESP.getEfuseMac() & 0xFFFF, HEX);

  BLEDevice::init(deviceName.c_str());
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());

  // 开始扫描
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->start(30);
  if (deviceFound) {
    connectToServer();
  }
  DHTSensor.begin();
}

void loop() {
  static uint32_t lastSendTime = 0;
  uint32_t msTime = millis();
  if (msTime - lastSendTime > 2000) {
    lastSendTime = msTime;
    // Serial.printf("%d, Test\n",msTime);
    if (!pClient->isConnected())
    {
      // Serial.println("尝试重新连接服务器...");
      // pBLEScan->start(30,true);
      connectToServer();
      Serial.printf("%d, 未连接服务器...\n", msTime);
    }
  }
  // if (!deviceConnected && !oldDeviceConnected) {
  //     delay(500); // 给BLE栈时间完成清理
  //     // BLEDevice::getScan()->start(30);
  //     oldDeviceConnected = deviceConnected;
  // }
  if (Data.isHeating)
    digitalWrite(LED, HIGH);
  else
    digitalWrite(LED, LOW);


  delay(1800);

  float t = DHTSensor.readTemperature();
  float h = DHTSensor.readHumidity();

  if (isnan(h) || isnan(t)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      h = -1;
      t = -1;
  }
  Data.humidity = h;
  Data.temperature = t;
  
  Serial.print(F("Humidity: "));
  Serial.print(h);
  Serial.print(F("%  Temperature: "));
  Serial.print(t);
  Serial.println(F("°C"));
  if (pClient->isConnected() && pRxRemoteCharacteristic != nullptr) {
    pRxRemoteCharacteristic->writeValue((uint8_t*)(&Data), sizeof(Data));
    Serial.println(deviceName + ": 数据发送成功");
  } else {
    Serial.println(deviceName + ": 无法发送数据 - 未连接或特征无效");
  }

}
#endif