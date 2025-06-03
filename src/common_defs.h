// common_defs.h
#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

// BLE服务UUID
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

// 特征UUID
#define TX_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8" // 主发子收
#define RX_CHARACTERISTIC_UUID "a1b5483e-36e1-4688-b7f5-ea07361b26b9" // 子发主收

// 设备名称前缀
#define MASTER_DEVICE_NAME "ESP32_BLE_Master"
#define SLAVE_DEVICE_PREFIX "ESP32_Slave_"

// 最大子节点数量
#define MAX_SLAVES 3
#define IS_SLAVE 1
#endif
