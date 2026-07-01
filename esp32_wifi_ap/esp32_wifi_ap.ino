#include <Arduino.h>

#include <esp32_neopixel-led.h>
#include <esp32_wifi-ap.h>



// ========== 配置参数 ==========
#define BAUD_RATE 115200 // 串口通信波特率
#define BUFFER_SIZE 128   // 串口接收缓冲区大小



#define AP_WIFI_SSID "ESP32-CAN-WIFI"
#define AP_WIFI_PASSWORD "12345678"
#define TCP_SERVER_PORT 8266

#define NEOPIXEL_PIN 10



// ========== 全局对象实例 ==========
ESP32NeoPixel neonpixel(NEOPIXEL_PIN, 20);
ESP32WiFiSerialServerUDP wifi_serial(AP_WIFI_SSID, AP_WIFI_PASSWORD, TCP_SERVER_PORT);




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
        bufidx = 0;

        wifi_serial.send_message(serialbuf);
        return true;
    }

    return false;
}


void setup() {
    //setCpuFrequencyMhz(80);
    Serial.begin(115200);
    //Serial1.begin(115200, SERIAL_8N1, 9, 10);
    //Serial1.println("hello");
    neonpixel.begin();
    wifi_serial.begin();
}

void loop() 
{
    // 必须高频调用，确保 TCP 通信和网络状态机不被阻塞
    wifi_serial.loop_process();


    // 逻辑：将串口输入的数据通过 WiFi 发送
    if (wifi_serial.check_connect())
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

    // 逻辑：处理接收到的 WiFi 数据并打印到串口
    const char* msg = wifi_serial.recv_message();
    if (msg[0] != '\0') 
    {
        Serial.print(msg);
        neonpixel.trigger(false, false, true);
    }

    // 更新 LED 状态 (非阻塞)
    neonpixel.update();

    yield();
}
