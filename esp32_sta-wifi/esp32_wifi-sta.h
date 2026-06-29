#ifndef ESP32_UDP_WIFISERIAL_CLIENT_H
#define ESP32_UDP_WIFISERIAL_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <lwip/sockets.h>
#include <WiFiUdp.h>




class ESP32WiFiSerialClientUDP
{
private:
    const char* wifi_ssid;
    const char* wifi_password;
    const char* server_ip;
    uint16_t udp_port;

    WiFiUDP wifi_udp;
    volatile bool device_connected = false; // UDP无连接，此处映射为 WiFi 连接状态

    static const size_t MAX_BUFFER_SIZE = 768;
    char tx_buffer[MAX_BUFFER_SIZE];
    size_t tx_len = 0;

    // 微型消息队列（防单包多行丢失）
    static const uint8_t RX_QUEUE_SIZE = 24;
    static const uint8_t MAX_MSG_LEN   = 42;
    char rx_queue[RX_QUEUE_SIZE][MAX_MSG_LEN]; // 消息池
    uint8_t rx_q_head = 0;
    uint8_t rx_q_tail = 0;
    uint8_t rx_q_count = 0;

    const unsigned long batch_interval_ms = 4;
    unsigned long last_send_time = 0;

    unsigned long last_reconnect_time = 0;
    const unsigned long reconnect_interval = 3000;
    unsigned long last_ping_time = 0;
    unsigned long last_recv_time = 0;

    // void reconnect_wifi(unsigned long current_time)
    // {
    //     if (current_time - this->last_reconnect_time > this->reconnect_interval)
    //     {
    //         this->last_reconnect_time = current_time;
    //         //WiFi.disconnect();
    //         //Serial.println("try reconnect bec not connect");
    //         //WiFi.reconnect();
    //     }
    // }

    void ping_udp(unsigned long current_time)
    {
        if (current_time - this->last_send_time >= 1000)
        {
            this->send_message("\r");
        }
    }

    void read_udp_data(unsigned long current_time)
    {
        int packet_size = wifi_udp.parsePacket();
        if (packet_size <= 0) 
        {
            return;
        }

        bool is_dropping_line = false;
        size_t rx_len = 0;
        this->last_recv_time = current_time;

        while (this->wifi_udp.available()) 
        {
            char c = this->wifi_udp.read();

            if (c == '\r' || c == '\n')
            {
                // 1. 过滤连续换行（完美兼容 \r\n、\n\r、\r\r、\n\n）
                if (rx_len == 0) continue;

                // 2. 仅在非丢弃模式下处理封口与入队
                if (!is_dropping_line) 
                {
                    // 只要 \r 不要 \n：如果是 \r 且空间足够，则追加到字符串末尾
                    if (rx_len < this->MAX_MSG_LEN - 1) 
                    {
                        this->rx_queue[this->rx_q_tail][rx_len] = '\r';
                        rx_len +=1;
                    }
                    // \n 不存入，直接作为分隔符消耗掉

                    // 严格保证 C 字符串结尾
                    this->rx_queue[this->rx_q_tail][rx_len] = '\0';

                    // 队列未满才入队（防覆盖未读数据）
                    if (this->rx_q_count < this->RX_QUEUE_SIZE) 
                    {
                        this->rx_q_tail = (this->rx_q_tail + 1) % this->RX_QUEUE_SIZE;
                        this->rx_q_count++;
                    }
                }

                // 3. 无论是否入队，遇到换行符都重置行状态
                rx_len = 0;
                is_dropping_line = false;
            } 
            else 
            {
                // 丢弃模式：只消耗字符，不写入队列
                if (is_dropping_line) continue;

                // 新行开始时检查队列是否已满
                if (rx_len == 0 && this->rx_q_count >= this->RX_QUEUE_SIZE) 
                {
                    is_dropping_line = true;
                    continue;
                }

                // 正常写入数据字符
                if (rx_len < this->MAX_MSG_LEN - 1) 
                {
                    this->rx_queue[this->rx_q_tail][rx_len] = c;
                    rx_len +=1;
                } 
                else 
                {
                    // 单行超长（不含换行符），标记丢弃直到遇到 \r/\n
                    is_dropping_line = true;
                }
            }
        }

    }

    void send_udp_data(unsigned long current_time)
    {
        if (current_time - this->last_send_time >= this->batch_interval_ms)
        {
            if (this->device_connected && this->tx_len > 0)
            {
                this->wifi_udp.beginPacket(this->server_ip, this->udp_port);
                this->wifi_udp.write((const uint8_t*)tx_buffer, tx_len);
                this->wifi_udp.endPacket();

                this->tx_len = 0;
                this->last_send_time = current_time;
            }
        }
    }

public:
    ESP32WiFiSerialClientUDP(const char* ssid, const char* password, const char* ip, uint16_t port)
    {
        this->wifi_ssid = ssid;
        this->wifi_password = password;
        this->server_ip = ip;
        this->udp_port = port;
    }

    void begin() 
    {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.setTxPower(WIFI_POWER_15dBm);
        WiFi.setAutoReconnect(true);

        // 注册 WiFi 事件回调 (使用 C++11 Lambda 表达式捕获 this 指针)
        WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) 
        {
            switch (event) 
            {
                case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                    this->device_connected = true;
                    this->last_recv_time = millis();
                    break;

                case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                    this->device_connected = false;
                    break;

                default:
                    break;
            }
        });


        WiFi.begin(this->wifi_ssid, this->wifi_password);
        this->last_reconnect_time = millis();

        this->wifi_udp.begin(udp_port); // 客户端绑定本地端口监听回包
    }

    bool check_connect() 
    { 
        return this->device_connected; 
    }

    bool send_message(const char* data) 
    {
        size_t data_len = strlen(data);

        if (this->device_connected)
        { 
            if (this->tx_len + data_len < this->MAX_BUFFER_SIZE - 1)
            {
                memcpy(this->tx_buffer + this->tx_len, data, data_len);
                this->tx_len += data_len;
                this->tx_buffer[this->tx_len] = '\0';
                return true;
            }
            // else
            // {
            //     this->last_send_time = 0;
            // }
        }

        return false;
    }

    const char* recv_message() 
    {
        if (this->rx_q_count > 0) 
        {
            const char* msg = this->rx_queue[this->rx_q_head];
            this->rx_q_head = (this->rx_q_head + 1) % this->RX_QUEUE_SIZE;
            this->rx_q_count -= 1;
            return msg;
        }
        return "";
    }

    void loop_process() 
    {
        unsigned long current_time = millis();

        if (this->device_connected) 
        {
            read_udp_data(current_time);
            ping_udp(current_time);
            send_udp_data(current_time);

            // if (current_time - this->last_recv_time > this->reconnect_interval)
            // {   
            //     this->device_connected = false;
            //     //WiFi.disconnect();
            //     //Serial.println("no ping");
            // }
        }
        // else
        // {
        //     reconnect_wifi(current_time);
        // }
    }
};


#endif ESP32_UDP_WIFISERIAL_CLIENT_H