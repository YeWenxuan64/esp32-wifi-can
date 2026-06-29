#ifndef ESP32BLESERIALSERVER_H
#define ESP32BLESERIALSERVER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


class ESP32BleSerialServer
{
private:
    const char* ble_name;
    const char* service_uuid;
    const char* rx_uuid;
    const char* tx_uuid;

    BLEServer* p_server = nullptr;
    BLEService* p_service = nullptr;
    BLECharacteristic* p_tx_characteristic = nullptr;
    BLECharacteristic* p_rx_characteristic = nullptr;
    BLEAdvertising* p_advertising = nullptr;

    bool device_connected = false;
    bool device_disconnected = false;

    static const size_t MAX_BUFFER_SIZE = 246;
    char tx_buffer[MAX_BUFFER_SIZE];
    char rx_buffer[MAX_BUFFER_SIZE];
    size_t tx_len = 0;
    size_t rx_len = 0;
    bool new_message_flag = false;

    const unsigned long batch_interval_ms = 8;
    unsigned long last_send_time = 0;

    // 连接回调
    class ServerCallbacks : public BLEServerCallbacks 
    {
        ESP32BleSerialServer* parent;

    public:
        ServerCallbacks(ESP32BleSerialServer* p) : parent(p) 
        {

        }
        
        void onConnect(BLEServer* pServer) override 
        {
            parent->device_connected = true;
            parent->device_disconnected = false;
            parent->rx_len = 0;
            //pServer->updateConnParams(pServer->getHandle(), 6, 8, 0, 100);
            //Serial.println("[BLE] Client connected");
        }
        
        void onDisconnect(BLEServer* pServer) override 
        {
            parent->device_connected = false;
            parent->device_disconnected = true;
            parent->rx_len = 0;

            yield();
            delay(200);  // 给BLE栈缓冲时间
            yield();

            pServer->startAdvertising(); // 断线重广播

            //Serial.println("[BLE] Client disconnected");
        }
    };

    // 数据接收回调
    class RxCallbacks : public BLECharacteristicCallbacks 
    {
        ESP32BleSerialServer* parent;

    public:
        RxCallbacks(ESP32BleSerialServer* p) : parent(p) 
        {

        }
        
        void onWrite(BLECharacteristic* pCharacteristic) override
        {
            String rx_value = pCharacteristic->getValue();
            bool end_flag = false;

            for (int i = 0; i < rx_value.length(); i++) 
            {
                char c = rx_value[i];

                if (parent->rx_len < parent->MAX_BUFFER_SIZE - 1) 
                {
                    parent->rx_buffer[parent->rx_len] = c;
                    parent->rx_len += 1;
                } 
                else 
                {
                    parent->rx_len = 0; // 缓冲区满，丢弃
                    break;
                }

                if (c == '\r' || c == '\n') 
                {
                    if (c == '\n' && parent->rx_len == 1) 
                    {
                        parent->rx_len = 0;
                        continue;
                    }

                    end_flag = true;
                    break; 
                }
            }

            if (parent->rx_len > 0 && end_flag) 
            {
                parent->rx_buffer[parent->rx_len] = '\0';
                parent->rx_len = 0;
                parent->new_message_flag = true;
            }

        }
    };

    void send_data_to_client(unsigned long current_time) 
    {
        if (this->tx_len > 0) 
        {
            if (current_time - this->last_send_time >= this->batch_interval_ms) 
            {
                this->p_tx_characteristic->setValue((uint8_t*)tx_buffer, tx_len);
                this->p_tx_characteristic->notify();
                this->tx_len = 0;
                this->last_send_time = current_time;
            }
        }
    }

public:
    ESP32BleSerialServer(const char* name, const char* service_uuid, const char* rx_uuid, const char* tx_uuid)
    {
        this->ble_name = name;
        this->service_uuid = service_uuid;
        this->rx_uuid = rx_uuid;
        this->tx_uuid = tx_uuid;
    }

    void begin() 
    {
        // 创建设备
        BLEDevice::init(this->ble_name);
        BLEDevice::setMTU(247);  // 协商最大MTU
        
        // 创建服务器并设置连接回调
        this->p_server = BLEDevice::createServer();
        this->p_server->setCallbacks(new ServerCallbacks(this));
        // 创建服务
        this->p_service = this->p_server->createService(service_uuid);
        
        // 创建发送特征并设置回调
        this->p_tx_characteristic = this->p_service->createCharacteristic(tx_uuid, BLECharacteristic::PROPERTY_NOTIFY);
        this->p_tx_characteristic->addDescriptor(new BLE2902());
        
        // 创建接收特征并设置回调
        this->p_rx_characteristic = this->p_service->createCharacteristic(rx_uuid, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
        this->p_rx_characteristic->setCallbacks(new RxCallbacks(this));
        
        // 启动服务
        this->p_service->start();
        
        // 配置广播
        this->p_advertising = BLEDevice::getAdvertising();
        this->p_advertising->addServiceUUID(service_uuid);
        this->p_advertising->setScanResponse(true);
        this->p_advertising->setMinPreferred(0x06);  // 6 * 1.25ms = 7.5ms
        this->p_advertising->setMaxPreferred(0x08);
        BLEDevice::startAdvertising();
    
        //Serial.printf("[BLE] Server started: %s\r\n", ble_name);
    }

    bool check_connect() 
    {
        return this->device_connected;
    }
    
    bool check_disconnect() 
    {
        bool ret = this->device_disconnected;
        this->device_disconnected = false;
        return ret;
    }

    bool send_message(const char* data) 
    {
        if (this->device_connected && this->p_tx_characteristic)
        {
            size_t data_len = strlen(data);

            if (this->tx_len + data_len >= this->MAX_BUFFER_SIZE - 1) // 防溢出检查（预留1字节给 '\0'）
            {
                return false; // 缓冲区已满，丢弃本次数据
            }
            
            memcpy(this->tx_buffer + this->tx_len, data, data_len);
            this->tx_len += data_len;
            this->tx_buffer[this->tx_len] = '\0'; // 保证 C 字符串结尾
            return true;

        }
        return false;
    }

    const char* recv_message() 
    {
        if (this->new_message_flag) 
        {
            this->new_message_flag = false;
            return this->rx_buffer;
        }
        return "";
    }

    void loop_process() 
    {
        unsigned long current_time = millis();
        
        
        // 批量发送数据
        if (this->device_connected && this->p_tx_characteristic) 
        {
            send_data_to_client(current_time);
        }
    }
};

#endif ESP32BLESERIALSERVER_H