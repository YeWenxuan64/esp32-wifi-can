#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <esp32_neopixel-led.h>

#define BLE_NAME "ESP32-BLE-AP"
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write (Client发送)
#define TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify (Client接收)

class ESP32BleSerialClient 
{
private:
    const char* server_ble_name;
    const char* service_uuid;
    const char* rx_uuid;   // 服务端RX = 客户端发送
    const char* tx_uuid;   // 服务端TX = 客户端接收

    BLEClient* p_client = nullptr;
    BLERemoteCharacteristic* p_tx_characteristic = nullptr;  // 接收通道 (Notify)
    BLERemoteCharacteristic* p_rx_characteristic = nullptr;  // 发送通道 (Write)
    BLEScan* p_ble_scan = nullptr;
    BLEAddress* p_server_address = nullptr;        // 保存目标设备地址

    inline static ESP32BleSerialClient* s_instance = nullptr; // 静态实例指针，用于回调中访问成员

    bool device_connected = false;
    bool do_connect = false;

    static const size_t MAX_BUFFER_SIZE = 246;
    char tx_buffer[MAX_BUFFER_SIZE];
    char rx_buffer[MAX_BUFFER_SIZE];
    size_t tx_len = 0;
    size_t rx_len = 0;
    bool new_message_flag = false;

    const unsigned long batch_interval_ms = 8;
    unsigned long last_send_time = 0;
    unsigned long last_reconnect_time = 0;
    const unsigned long reconnect_interval = 3000;

    // 通知回调函数 (Core 3.x 使用 std::function)
    static void notify_callback(BLERemoteCharacteristic* pCharacteristic, uint8_t* pData, size_t length, bool isNotify) 
    {
        // 通过全局指针或静态成员访问父类，此处使用静态指针方案
        if (ESP32BleSerialClient::s_instance == nullptr) return;
        ESP32BleSerialClient::s_instance->handle_notify(pData, length);
    }

    // 处理通知数据（实例方法）
    void handle_notify(uint8_t* pData, size_t length) 
    {
        if (pData == nullptr || length == 0) return;
        
        for (size_t i = 0; i < length; i++) 
        {
            char c = (char)pData[i];
            if (rx_len < MAX_BUFFER_SIZE - 1) 
            {
                rx_buffer[rx_len++] = c;
            } 
            else 
            {
                rx_len = 0;  // 缓冲区满，丢弃
                break;
            }
            if (c == '\r' || c == '\n') 
            {
                if (c == '\n' && rx_len == 1) 
                {
                    rx_len = 0;
                    continue;
                }
                rx_buffer[rx_len] = '\0';
                rx_len = 0;
                new_message_flag = true;
                break;
            }
        }
    }

    // 客户端连接回调
    class ClientCallbacks : public BLEClientCallbacks 
    {
        ESP32BleSerialClient* parent;
    public:
        ClientCallbacks(ESP32BleSerialClient* p) : parent(p) {}
        
        void onConnect(BLEClient* client) override 
        {
            //Serial.println("[BLE] Connected to server");
        }
        
        void onDisconnect(BLEClient* client) override 
        {
            if (parent) parent->device_connected = false;
            //Serial.println("[BLE] Disconnected from server");
        }
    };

    // 扫描回调
    class ScanCallbacks : public BLEAdvertisedDeviceCallbacks 
    {
        ESP32BleSerialClient* parent;
    public:
        ScanCallbacks(ESP32BleSerialClient* p) : parent(p) 
        {

        }
        
        void onResult(BLEAdvertisedDevice advertisedDevice) override 
        {
            if (advertisedDevice.haveName() && advertisedDevice.getName() == parent->server_ble_name) 
            {
                //Serial.printf("[BLE] Found: %s @ %s\r\n", 
                advertisedDevice.getScan()->stop();
                
                // 保存目标地址
                delete parent->p_server_address;
                parent->p_server_address = new BLEAddress(advertisedDevice.getAddress());

                parent->do_connect = true;
                
            }
        }
    };

    void scan_ble(unsigned long current_time) 
    {
        if (current_time - this->last_reconnect_time > this->reconnect_interval) 
        {
            this->last_reconnect_time = current_time;

            this->p_ble_scan->stop();
            //Serial.println("[BLE] Scanning...");
            
            this->p_ble_scan->clearResults();
            this->p_ble_scan->start(2, false);  // 扫描3秒
        }
    }

    bool connect_to_server() 
    {
        if (!this->p_server_address)
        {
            return false;
        } 

        //Serial.println("[BLE] connecting...");
        
        // 创建客户端
        delete this->p_client;
        this->p_client = BLEDevice::createClient();
        
        p_client->updateConnParams(6, 8, 0, 100);  // min, max, latency, timeout
        p_client->setMTU(247);

        this->p_client->setClientCallbacks(new ClientCallbacks(this));
        
        // 连接设备
        if (!this->p_client->connect(*this->p_server_address)) 
        {
            //Serial.println("[BLE] Connection failed");
            return false;
        }
        
        // 获取服务
        BLERemoteService* p_service = p_client->getService(this->service_uuid);
        if (!p_service) 
        {
            //Serial.println("[BLE] Service not found");
            return false;
        }
        
        // 获取特征
        this->p_tx_characteristic = p_service->getCharacteristic(tx_uuid);  // 接收通道 (Notify)
        this->p_rx_characteristic = p_service->getCharacteristic(rx_uuid);  // 发送通道 (Write)
        
        if (!this->p_tx_characteristic || !this->p_rx_characteristic) 
        {
            //Serial.println("[BLE] Characteristic not found");
            return false;
        }
        
        // 注册通知回调 (Core 3.x: 传入函数指针)
        this->p_tx_characteristic->registerForNotify(notify_callback, true);

        return true;
    }

    void send_data_to_server(unsigned long current_time) 
    {
        if (tx_len > 0 && device_connected && p_rx_characteristic) 
        {
            if (current_time - last_send_time >= batch_interval_ms) 
            {
                p_rx_characteristic->writeValue((uint8_t*)tx_buffer, tx_len, false);
                tx_len = 0;
                last_send_time = current_time;
            }
        }
    }

public:
    ESP32BleSerialClient(const char* name,  const char* service_uuid, const char* rx_uuid, const char* tx_uuid)
    {
        this->server_ble_name = name;
        this->service_uuid = service_uuid;
        this->rx_uuid = rx_uuid;
        this->tx_uuid = tx_uuid;
        s_instance = this;  // 注册实例指针
    }

    void begin() 
    {
        // 创建设备
        BLEDevice::init("");
        BLEDevice::setMTU(247); // 协商最大MTU
        
        // 配置扫描 (Core 3.x 接口)
        this->p_ble_scan = BLEDevice::getScan();
        this->p_ble_scan->setActiveScan(true);
        this->p_ble_scan->setInterval(100);
        this->p_ble_scan->setWindow(99);
        
        // 注册扫描回调 (Core 3.x: 第二个参数为是否调用 onResult 时释放)
        this->p_ble_scan->setAdvertisedDeviceCallbacks(new ScanCallbacks(this), true);
        this->p_ble_scan->start(3, false);

        this->last_reconnect_time = millis();
        //Serial.printf("[BLE] Scanning for: %s\r\n", server_ble_name);
    }

    bool check_connect() 
    {
        return this->device_connected;
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
        
        if (device_connected && (!p_client || !p_client->isConnected())) 
        {
            device_connected = false; // 意外断开，触发重连
        }

        // 1. 扫描阶段
        if (!device_connected && !do_connect) 
        {
            scan_ble(current_time);
        }
        
        // 2. 连接阶段
        if (do_connect) 
        {
            device_connected = connect_to_server();
            do_connect = false;
        }
        
        // 3. 通信阶段
        if (device_connected && p_client && p_client->isConnected()) 
        {
            send_data_to_server(current_time);
        } 
    }
};


//ESP32BleSerialClient* ESP32BleSerialClient::s_instance = nullptr;
ESP32BleSerialClient bleClient(BLE_NAME, SERVICE_UUID, RX_UUID, TX_UUID);
ESP32NeoPixel neonpixel(10, 20);



bool read_usb_serial()
{
    // 静态变量：函数调用之间保持值不变
    static const size_t BUFFER_SIZE = 48;
    static char serialbuf[BUFFER_SIZE]; // 字符缓冲区，存储当前接收的命令
    static size_t bufidx = 0;           // 当前缓冲区写入位置索引
    bool end_flag = false;

    while (Serial.available() > 0)
    {
        char c = Serial.read(); // 读取1个字节

        // 缓冲区未满：正常存入字符
        if (bufidx < BUFFER_SIZE - 1) // -1 是为 '\0' 预留空间
        {
            serialbuf[bufidx] = c;
            bufidx += 1;
        }
        else
        {
            bufidx = 0; // 清空索引 = 丢弃整条不完整命令
            break;
        }

        if (c == '\r' || c == '\n')
        {
            if (c == '\n' && bufidx == 1)
            {
                bufidx = 0;
                continue;
            }

            end_flag = true;
            break; 
        }
    }

    if (bufidx > 0 && end_flag)
    {
        serialbuf[bufidx] = '\0';  // 添加字符串结束符，确保是合法 C 字符串
        bleClient.send_message(serialbuf);
        bufidx = 0; // 重置索引，准备接收下一条命令
        return true;
    }

    return false;
}



void setup() 
{
    Serial.begin(115200);
    neonpixel.begin();
    bleClient.begin();
}

void loop() 
{
    bleClient.loop_process();
    
    // 连接成功后发送数据
    if (bleClient.check_connect())
    {
        if (read_usb_serial())
        {
            neonpixel.trigger(false, true, false);
        }
    }
    else
    {
        neonpixel.trigger(true, false, false);
    }

    // 接收数据
    const char* rx = bleClient.recv_message();
    if (rx[0] != '\0') 
    {
        Serial.print(rx);
        neonpixel.trigger(false, false, true);
    }

    neonpixel.update();
    yield();
}