// common_defs.h
#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

// 区分服务器客户端代码
#define IS_CLIENT 1
#define IS_SERVER 1

// 单次发送数据块大小，容易协商失败恢复默认
#define DEFAULT_MTU 20

#define SERVER_MAC "50:78:7D:45:D8:62"

// BLE服务UUID
#define SERVICE_UUID        "515940db-56c6-4252-8348-7e4a04e40d89"

// 特征UUID
#define TX_CHARACTERISTIC_UUID "7fce681c-4b6a-44a9-8bd3-0fc527137496" // 主发子收
#define RX_CHARACTERISTIC_UUID "8426c0e9-009f-4f00-a396-4e6211b3c0de" // 子发主收

// 设备名称前缀
#define SERVER_DEVICE_NAME "ESP32_BLE_Server"
#define CLIENT_DEVICE_PREFIX "ESP32_Client_"

// 最大子节点数量
#define MAX_CLIENTS 3

struct SensorData {
  float temperature;
  float humidity;
  float tempThreshold;
  bool heater;
};

struct CommandData {
  float tempThreshold =25;
  bool heaterOverride = false;
  bool heater = false;
};

#endif
