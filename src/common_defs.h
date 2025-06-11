// common_defs.h
#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

// BLE服务UUID
#define SERVICE_UUID        "515940db-56c6-4252-8348-7e4a04e40d89"

// 特征UUID
#define TX_CHARACTERISTIC_UUID "7fce681c-4b6a-44a9-8bd3-0fc527137496" // 主发子收
#define RX_CHARACTERISTIC_UUID "8426c0e9-009f-4f00-a396-4e6211b3c0de" // 子发主收

// 设备名称前缀
#define MASTER_DEVICE_NAME "ESP32_BLE_Master"
#define SLAVE_DEVICE_PREFIX "ESP32_Slave_"

// 最大子节点数量
#define MAX_SLAVES 3
// #define IS_SLAVE 1
#define IS_MASTER 1

struct SensorData {
  float temperature;
  float humidity;
  bool isHeating;
};
#endif
