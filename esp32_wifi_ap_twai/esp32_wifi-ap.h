#include "HardwareSerial.h"
#ifndef ESP32_UDP_WIFISERIAL_SERVER_H
#define ESP32_UDP_WIFISERIAL_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include <WiFiUdp.h>




class ESP32WiFiSerialServerUDP
{
private:
    const char* wifi_ssid;
    const char* wifi_password;
    IPAddress gateway_ip = IPAddress(192, 168, 4, 1);
    uint16_t udp_port = 8266;
    
    WiFiUDP udp;
    IPAddress remote_ip;
    uint16_t remote_port = 0;
    volatile bool device_connected = false;
    volatile bool connect_changed = false;

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
    int packet_size = 0;

    const unsigned long batch_interval_ms = 4;
    const unsigned long ping_interval_ms = 800;
    const unsigned long peer_timeout_ms = 4000; // UDP无连接，超时未通信视为断开
    unsigned long last_send_time = 0;
    unsigned long last_recv_time = 0;


    // 更新对端状态 & 模拟连接/断开
    void handle_udp_peer(unsigned long current_time)
    {
        this->packet_size = udp.parsePacket();
        if (this->packet_size > 0)
        {
            IPAddress current_ip = udp.remoteIP();

            if (current_ip != this->gateway_ip)
            {
                uint16_t current_port = udp.remotePort();

                if (!this->device_connected || current_ip != this->remote_ip || current_port != this->remote_port)
                {
                    // 新设备接入或设备切换
                    this->remote_ip = current_ip;
                    this->remote_port = current_port;
                    this->device_connected = true;
                    this->connect_changed = true;
                }

                this->last_recv_time = current_time;
            }
        }
        else if (this->device_connected)
        {
            if (current_time - this->last_recv_time > this->peer_timeout_ms)
            {
                this->device_connected = false;
                this->connect_changed = true;
            }
        }
    }

    void ping_udp(unsigned long current_time)
    {
        if (current_time - this->last_send_time > this->ping_interval_ms)
        {
            this->send_message("\r");
        }
    }

    void read_udp_data()
    {
        // parsePacket() 已在 handle_udp_peer 调用，此处直接读取当前包数据
        if (this->packet_size <= 0) 
        {
            return;
        }

        bool is_dropping_line = false;
        size_t rx_len = 0;

        while (udp.available()) 
        {
            char c = this->udp.read();

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
        if (tx_len > 0 && device_connected)
        {
            if (current_time - last_send_time >= batch_interval_ms)
            {
                udp.beginPacket(remote_ip, remote_port);
                udp.write((const uint8_t*)tx_buffer, tx_len);
                udp.endPacket(); // UDP 发送立即返回，不阻塞

                tx_len = 0;
                last_send_time = current_time;
            }
        }
    }

public:
    ESP32WiFiSerialServerUDP(const char* ssid, const char* password, uint16_t port)
    {
        this->wifi_ssid = ssid;
        this->wifi_password = password;
        this->udp_port = port;
    }

    void begin() 
    {
        WiFi.mode(WIFI_AP);
        WiFi.setSleep(false);
        esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
        WiFi.setTxPower(WIFI_POWER_15dBm); // 20, 19, 18, 17, 15, 13, 11

        // 注册 WiFi 事件回调 (使用 C++11 Lambda 表达式捕获 this 指针)
        WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) 
        {
            switch (event) 
            {
                case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
                    this->device_connected = true;
                    // this->connect_changed = true;
                    this->last_recv_time = millis();
                    // Serial.println("connect call");
                    break;

                // case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
                //     this->device_connected = false;
                //     // this->connect_changed = true;
                //     // Serial.println("disconnect call");
                //     break;

                default:
                    break;
            }
        });


        
        WiFi.softAPConfig(this->gateway_ip, this->gateway_ip, IPAddress(255, 255, 255, 0));
        WiFi.softAP(wifi_ssid, wifi_password, 11, 0, 4); // 信道11, 不隐藏, 最大4连接

        udp.begin(udp_port);
    }

    bool check_connect() 
    { 
        return device_connected; 
    }

    bool check_connect_chang()
    {
        bool ret = this->connect_changed;
        this->connect_changed = false;
        return ret;
    }

    const char* recv_message() 
    {
        if (rx_q_count > 0) 
        {
            const char* msg = this->rx_queue[this->rx_q_head];
            this->rx_q_head = (this->rx_q_head + 1) % this->RX_QUEUE_SIZE;
            this->rx_q_count -= 1;
            return msg;
        }
        return "";
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

                // if (this->tx_len > 400)
                // {
                //     Serial.print("send");
                //     Serial.println(this->tx_len);
                // }

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

    void loop_process() 
    {
        unsigned long current_time = millis();
        handle_udp_peer(current_time);

        if (device_connected) 
        {
            read_udp_data();
            ping_udp(current_time);
            send_udp_data(current_time);
        }
    }
};

#endif ESP32_UDP_WIFISERIAL_SERVER_H