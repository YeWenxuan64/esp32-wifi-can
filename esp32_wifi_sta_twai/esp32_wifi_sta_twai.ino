#include <Arduino.h>

#include <esp32-twai-lawicel.h>
#include <esp32_neopixel-led.h>
#include <esp32_wifi-sta.h>



// ========== 配置参数 ==========
#define BAUD_RATE 115200 // 串口通信波特率
#define BUFFER_SIZE 128   // 串口接收缓冲区大小

#define TWAI_TX_PIN 4
#define TWAI_RX_PIN 5

#define AP_WIFI_SSID "ESP32-CAN-WIFI"
#define AP_WIFI_PASSWORD "12345678"
#define SERVER_PORT 8266

#define NEOPIXEL_PIN 10



// ========== 全局对象实例 ==========
ESP32CANV2 slcan(TWAI_TX_PIN, TWAI_RX_PIN);
ESP32NeoPixel neonpixel(NEOPIXEL_PIN, 20);
ESP32WiFiSerialClientUDP wifi_serial(AP_WIFI_SSID, AP_WIFI_PASSWORD, SERVER_PORT);



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
        ret = "\r";
        break;

    case 'T': // SEND EXT FRAME
        slcan.send(&cmd_buffer[1], true, false);
        ret = "\r";
        break;

    case 'r': // SEND STD RTR FRAME
        slcan.send(&cmd_buffer[1], false, true);
        ret = "\r";
        break;

    case 'R': // SEND EXT RTR FRAME
        slcan.send(&cmd_buffer[1], true, true);
        ret = "\r";
        break;

    case 'V':
        ret = "YWX-V0.7b\r";
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
