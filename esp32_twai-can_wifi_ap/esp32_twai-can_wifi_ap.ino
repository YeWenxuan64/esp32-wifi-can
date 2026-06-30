#include <Arduino.h>
#include "driver/twai.h"

#include <esp32_neopixel-led.h>
#include <esp32_wifi-ap.h>
//#include <esp32_twai-v1.h>


// ========== 配置参数 ==========
#define BAUD_RATE 115200 // 串口通信波特率
#define BUFFER_SIZE 128   // 串口接收缓冲区大小

#define TWAI_TX_PIN 4
#define TWAI_RX_PIN 5

#define AP_WIFI_SSID "ESP32-CAN-WIFI"
#define AP_WIFI_PASSWORD "12345678"
#define TCP_SERVER_PORT 8266

// #define BLE_NAME "ESP32-BLE-AP"
// #define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
// #define RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
// #define TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define NEOPIXEL_PIN 10




class ESP32CANV2
{
private:
    gpio_num_t rx_pin;
    gpio_num_t tx_pin;
    bool working = false;
    char recv_buffer[64];
    // twai_general_config_t twai_general_config;
    twai_handle_t twai_handle = nullptr;
    twai_timing_config_t twai_timing_config;
    twai_filter_config_t twai_filter_config;
    int timeout_ms = 0;

    // Helper: Convert single hex char to int (0-15), returns -1 on error
    static uint8_t hexchar_to_int(char c)
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        return 0;
    }

    // Helper: Parse 'len' hex chars from string into uint32_t
    static uint32_t hexstring_to_int(const char *str, int len)
    {
        uint32_t result = 0;
        for (int i = 0; i < len; i++)
        {
            uint8_t val = hexchar_to_int(str[i]);
            result = (result << 4) | val;
        }
        return result;
    }

    // Helper: Convert int (0-15) to hex char ('0'-'F')
    static char int_to_hexchar(uint8_t val)
    {
        val &= 0x0F; // 确保只取低 4 位
        if (val < 10)
        {
            return '0' + val;
        }
        else if (val < 16)
        {
            return 'A' + (val - 10);
        }
        else
        {
           return '0';
        }
    }

    // Helper: Convert int to hex string with specific length (padding with 0)
    static void int_to_hexstring(uint32_t val, char *str, int len)
    {
        for (int i = 0; i < len; i++)
        {
            // 从最低位开始提取，填入字符串末尾向前，实现高位补零
            str[len - 1 - i] = int_to_hexchar((val >> (i * 4)) & 0xF);
        }
        str[len] = '\0'; // 添加字符串结束符
    }

public:
    ESP32CANV2(int tx_pin, int rx_pin)
    {
        this->tx_pin = (gpio_num_t)tx_pin;
        this->rx_pin = (gpio_num_t)rx_pin;


        this->set_bitrate(7);
        this->twai_filter_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    }

    bool open()
    {
        this->close();
        
        twai_general_config_t twai_general_config = TWAI_GENERAL_CONFIG_DEFAULT_V2(0, this->tx_pin, this->rx_pin, TWAI_MODE_NORMAL);  // TWAI_MODE_NORMAL, TWAI_MODE_NO_ACK
        twai_general_config.tx_queue_len = 4;
        twai_general_config.rx_queue_len = 4;

        esp_err_t ret = twai_driver_install_v2(&twai_general_config, &this->twai_timing_config, &this->twai_filter_config, &this->twai_handle);

        if (ret == ESP_OK && this->twai_handle != nullptr)
        {
            ret = twai_start_v2(this->twai_handle);

            if (ret == ESP_OK)
            {
                this->working = true;
                return true;
            }
            else
            {
                this->close();
                return false;
            }
        }
        else
        {
            this->working = false;
            return false;
        }
    }

    bool close()
    {
        if (this->twai_handle != nullptr)
        {
            twai_stop_v2(this->twai_handle);
            twai_driver_uninstall_v2(this->twai_handle);
            this->twai_handle = nullptr;
            this->working = false;
            return true;
        }
        return false;
    }

    // 设置波特率
    bool set_bitrate(int preset)
    {
        // 更新内部配置，下次 open 时生效
        switch (preset)
        {
        case 0:
            this->twai_timing_config = TWAI_TIMING_CONFIG_10KBITS();
            return true;

        case 1:
            this->twai_timing_config = TWAI_TIMING_CONFIG_20KBITS();
            return true;

        case 2:
            this->twai_timing_config = TWAI_TIMING_CONFIG_50KBITS();
            return true;

        case 3:
            this->twai_timing_config = TWAI_TIMING_CONFIG_100KBITS();
            return true;

        case 4:
            this->twai_timing_config = TWAI_TIMING_CONFIG_125KBITS();
            return true;

        case 5:
            this->twai_timing_config = TWAI_TIMING_CONFIG_250KBITS();
            return true;

        case 6:
            this->twai_timing_config = TWAI_TIMING_CONFIG_500KBITS();
            return true;

        case 7:
            this->twai_timing_config = TWAI_TIMING_CONFIG_800KBITS();
            return true;

        case 8:
            this->twai_timing_config = TWAI_TIMING_CONFIG_1MBITS();
            return true;

        default:
            return false;
        }
    }

    bool send(const char *id_dlc_data, bool ext, bool rtr)
    {
        twai_message_t can_frame_send;
        int data_start_index = 3;

        if (ext)
        {
            can_frame_send.extd = 1;
            // Extended ID: 8 hex chars
            can_frame_send.identifier = hexstring_to_int(&id_dlc_data[0], 8);
            data_start_index = 9;

            if (rtr)
            {
                can_frame_send.rtr = 1;
            }
            else
            {
                can_frame_send.rtr = 0;
            }
        }
        else
        {
            can_frame_send.extd = 0;
            can_frame_send.identifier = hexstring_to_int(&id_dlc_data[0], 3);
            data_start_index = 4;

            if (rtr)
            {
                can_frame_send.rtr = 1;
            }
            else
            {
                can_frame_send.rtr = 0;
            }
        }

        int dlc = hexchar_to_int(id_dlc_data[data_start_index - 1]);
        can_frame_send.data_length_code = dlc;

        if (!rtr)
        {
            for (int i = 0; i < dlc; i++)
            {
                uint8_t data_byte = hexstring_to_int(&id_dlc_data[data_start_index + (i * 2)], 2);
                can_frame_send.data[i] = data_byte;
            }
        }

        if (!this->working || this->twai_handle == nullptr)
        {
            return false;
        }

        esp_err_t ret = twai_transmit_v2(this->twai_handle, &can_frame_send, pdMS_TO_TICKS(this->timeout_ms));
        if (ret == ESP_OK)
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    const char* recv()
    {
        if (!this->working || this->twai_handle == nullptr)
        {
            return "";
        }

        twai_message_t can_frame_recv;
        esp_err_t ret = twai_receive_v2(this->twai_handle, &can_frame_recv, pdMS_TO_TICKS(this->timeout_ms));
        memset(recv_buffer, 0, sizeof(recv_buffer));

        if (ret != ESP_OK)
        {
            return "";
        }

        uint32_t canid = can_frame_recv.identifier;
        // 拦截所有非目标 ID：既不是 0x504 也不是 0x222，直接返回空
        if (canid != 0x504 && canid != 0x503 && canid != 0x223 && canid != 0x222 && canid != 0x2e1 && canid != 0x426) 
        {
            return "";
        }


        int data_start_index = 5;
        int end_index = data_start_index;

        // 2. 赋值命令类型字符 (t/T/r/R)
        if (can_frame_recv.extd)
        {
            if (can_frame_recv.rtr)
            {
                recv_buffer[0] = 'R';
            }
            else
            {
                recv_buffer[0] = 'T';
            }
            data_start_index = 10; // T18DAF110 4 AABBCCDD\r

            return "";
        }
        else
        {
            if (can_frame_recv.rtr)
            {
                recv_buffer[0] = 'r';
            }
            else
            {
                recv_buffer[0] = 't';
            }

            int_to_hexstring(can_frame_recv.identifier, &recv_buffer[1], 3);
            data_start_index = 5;
            // t504 8 0102030405060708
            // 0123 4 56789012345678901
        }

        // DLC (1 位)
        recv_buffer[data_start_index - 1] = int_to_hexchar(can_frame_recv.data_length_code);
        end_index = data_start_index;

        // Data (如果不是 RTR)
        if (!can_frame_recv.rtr)
        {
            for (int i = 0; i < can_frame_recv.data_length_code; i++)
            {
                int_to_hexstring(can_frame_recv.data[i], &recv_buffer[data_start_index + (i * 2)], 2);
                end_index = data_start_index + ((i + 1) * 2);
            }
        }

        recv_buffer[end_index] = '\r';
        recv_buffer[end_index + 1] = '\0';

        if (recv_buffer[0] == 't')
        {
            for (int i = data_start_index; i < can_frame_recv.data_length_code * 2; i++) 
            {
                char c = recv_buffer[i];

                if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) 
                {
                    // Serial.print(c);
                    // Serial.print("in");
                    // Serial.print(i);
                    // Serial.println(recv_buffer);

                    //recv_buffer[data_start_index - 1] = int_to_hexchar(i / 2);;
                    //break;
                    
                    return "";
                }
            }
        }

        return recv_buffer;

    }
};




// ========== 全局对象实例 ==========
ESP32CANV2 slcan(TWAI_TX_PIN, TWAI_RX_PIN);
ESP32NeoPixel neonpixel(NEOPIXEL_PIN, 20);
ESP32WiFiSerialServerUDP wifi_serial(AP_WIFI_SSID, AP_WIFI_PASSWORD, TCP_SERVER_PORT);



void parse_serial_cmd(const char *cmd_buffer)
{ // LAWICEL PROTOCOL
    char command_type = cmd_buffer[0];
    String ret = "\0";

    switch (command_type)
    {
    case 'O': // OPEN CAN
        slcan.open();
        ret = "\r";
        break;

    case 'C':
        slcan.close();
        ret = "\r";
        break;

    case 'S':
    {
        char index_char = cmd_buffer[1];
        
        if (index_char >= '0' && index_char <= '8') // 判断是否为 '0'~'9' 的字符
        {
            int bitrate_preset = index_char - '0'; // 字符转整数：ASCII 码相减（'0'=48, '1'=49...）
            slcan.set_bitrate(bitrate_preset);
            ret = "\r";
        }
    }
    break;

    case 't': // SEND STD FRAME
        slcan.send(&cmd_buffer[1], false, false);
        break;

    case 'T': // SEND EXT FRAME
        slcan.send(&cmd_buffer[1], true, false);
        break;

    case 'r': // SEND STD RTR FRAME
        slcan.send(&cmd_buffer[1], false, true);
        break;

    case 'R': // SEND EXT RTR FRAME
        slcan.send(&cmd_buffer[1], true, true);
        break;

    case 'V':
        ret = "YWX-V0.6b\r";
        break;
    case 'N':
        ret = "NA0001\r";
        break;
    case 'F':
        ret = "F00\r";
        break;
    default:
        break;
    }

    if (ret != "\0")
    {
        if (wifi_serial.check_connect())
        {
            wifi_serial.send_message(ret.c_str());
        }
        else
        {
            Serial.print(ret.c_str());
        }
    }
}

bool read_usb_serial()
{
    // 静态变量：函数调用之间保持值不变
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
            // 已经溢出，丢弃后续字符，但不重置 bufidx

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
        bufidx = 0; // 重置索引，准备接收下一条命令

        parse_serial_cmd(serialbuf);
        return true;
    }

    return false;
}


bool read_wifi_serial()
{
    if (wifi_serial.check_connect())
    {
        const char* msg = wifi_serial.recv_message();

        if (msg[0] != '\0')
        {   
            parse_serial_cmd(msg);
            return true;
        }
    }
    else
    {
        neonpixel.trigger(true, false, false);
    }

    if (wifi_serial.check_connect_chang())
    {
        const char* msg ="C\r\0";
        //Serial.println("connect_changed");
        parse_serial_cmd(msg);
    }

    return false;
}



void setup()
{
    // put your setup code here, to run once:
    Serial.begin(115200);
    neonpixel.begin();
    wifi_serial.begin();
}

void loop()
{
    // put your main code here, to run repeatedly:
    wifi_serial.loop_process();

    // 1. 处理 USB 串口命令，如果有完整命令则闪绿灯
    if (read_usb_serial() || read_wifi_serial()) 
    {
        neonpixel.trigger(false, true, false);
    }

    // 2. 处理 CAN 接收，如果有报文则闪蓝灯
    const char* can_recv = slcan.recv();
    if (can_recv[0] != '\0') 
    {
        if (wifi_serial.check_connect())
        {
            wifi_serial.send_message(can_recv);
        }
        else
        {
            Serial.print(can_recv);
        }
        
        neonpixel.trigger(false, false, true);
    }

    // 3. 更新 LED 状态 (非阻塞)
    neonpixel.update();

    yield();
}
