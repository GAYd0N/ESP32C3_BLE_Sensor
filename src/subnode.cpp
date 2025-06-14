#include "common_defs.h"
#ifdef IS_CLIENT
// #include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <DHT.h>

#define DHTPIN 10
#define DHTTYPE DHT22
#define LED 8

DHT DHTSensor(DHTPIN, DHTTYPE);

String deviceName;

SensorData Data;
bool heaterOverride = false;
bool deviceFound = false;

BLEAddress pServerAddress(SERVER_MAC);
BLEClient *pClient = nullptr;
BLEScan* pBLEScan = nullptr;
BLERemoteService *pRemoteService = nullptr;
BLERemoteCharacteristic *pTxRemoteCharacteristic = nullptr;
BLERemoteCharacteristic *pRxRemoteCharacteristic = nullptr;

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pClient) {
    Serial.println("连接到服务器成功");
  }

  void onDisconnect(BLEClient* pClient) {
    Serial.println("与服务器断开连接");
  }
};

void receiveDataChunks(uint8_t *pData, size_t length) {
  static String jsonStr;
  if (pData == nullptr)
      return;

  jsonStr.concat(pData, length);

  // 检查是否收到完整数据
  if(jsonStr.endsWith("\n")) {
    Serial.println("完整数据接收完成:");
    Serial.println(jsonStr.c_str());
  }
  else {
    return;
  }

  JsonDocument jsonDoc;
  DeserializationError error = deserializeJson(jsonDoc, jsonStr);
  Serial.println(jsonStr.c_str());
  if (error) {
    Serial.printf("JSON 解析失败: %s\n", error.c_str());
  } else {
    Data.tempThreshold = jsonDoc["tempThreshold"];
    heaterOverride = jsonDoc["heaterOverride"];
    Data.heater = jsonDoc["heater"];
    Serial.printf("温度阈值: %.1f, 加热: %s, 自动设置: %s\n", Data.tempThreshold, Data.heater ? "ON" : "OFF", heaterOverride ? "OFF" : "ON");
  }
  jsonStr.clear();
}

void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify) {
  // Serial.printf("BLE Callback: pData=%s, length=%d, isNotify=%d\n", 
  //             pData, length, isNotify);
  receiveDataChunks(pData, length);
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

void sendLargeData(BLERemoteCharacteristic* pChar, const std::string& data) {
  const size_t chunkSize = DEFAULT_MTU;
  static bool isSending = false;
  //防止重复调用队列混乱
  if (isSending)
    return;
  isSending = true;
  size_t length = data.length();
  for(size_t i = 0; i < length; i += chunkSize) {
    size_t end = (i + chunkSize > length) ? length : i + chunkSize;
    std::string chunk = data.substr(i, end - i);
    pChar->writeValue(chunk, true);
    Serial.printf(chunk.c_str());
    delay(20); // 给接收方处理时间
  }
  pChar->writeValue("\n", true);
  Serial.println("");
  isSending = false;
}


void connectToServer() {
    // 扫描回调时直接连接不靠谱
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
        std::string data(deviceName.c_str());
        sendLargeData(pRxRemoteCharacteristic, data);
      }
    }
    else {
      Serial.println("Cant Find our Characteristic!");
    }
}


void SendDataToServer() {
  if (pClient->isConnected() && pRxRemoteCharacteristic != nullptr) {
    std::string jsonStr;
    JsonDocument jsonDoc;
    jsonDoc["name"] = deviceName.c_str();
    jsonDoc["temperature"] = Data.temperature;
    jsonDoc["humidity"] = Data.humidity;
    jsonDoc["tempThreshold"] = Data.tempThreshold;
    jsonDoc["heater"] = Data.heater;

    serializeJson(jsonDoc, jsonStr);
    sendLargeData(pRxRemoteCharacteristic, jsonStr);
    Serial.println(deviceName + ": 数据已发送");
  } else {
    Serial.println(deviceName + ": 无法发送数据 - 未连接或特征无效");
  }
}

void setHeaterStatus(bool status) {
  if (Data.heater != status) {
    Data.heater = status;
  }
  digitalWrite(LED, status ? LOW : HIGH);
  Serial.printf("已%s加热器\n", status ? "启动" : "关闭");
}

void setup() {
  Serial.begin(115200);
  Serial.println("子节点启动...");

  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);
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
  static uint32_t lastTime = 0;
  uint32_t msTime = millis();
  if (msTime - lastTime > 2000) {
    lastTime = msTime;
    // Serial.printf("%d, Test\n",msTime);
    // 每两秒检查连接状态
    if (!pClient->isConnected())
    {
      delay(5);
      // Serial.println("尝试重新连接服务器...");
      // pBLEScan->start(30,true);
      connectToServer();
      Serial.printf("%d, 未连接服务器...\n", msTime);
    }
  }
  // 约两秒读取一次传感器
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
  if (!heaterOverride && t > -1.0f) {
    setHeaterStatus((Data.tempThreshold > t));
  }
  else if (heaterOverride) {
    setHeaterStatus(Data.heater);
  }
  Serial.print(F("Humidity: "));
  Serial.print(h);
  Serial.print(F("%  Temperature: "));
  Serial.print(t);
  Serial.print(F("°C"));
  Serial.printf(" TempThreshold: %.1f", Data.tempThreshold);
  Serial.printf(" Heater: %s", Data.heater ? "ON" : "OFF");
  Serial.printf(" HeaterOverride: %s\n", heaterOverride ? "ON" : "OFF");

  SendDataToServer();

}
#endif