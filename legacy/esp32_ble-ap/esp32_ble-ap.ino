#include <Arduino.h>

#include <esp32_neopixel-led.h>
#include <esp32_ble-ap.h>

#define BLE_NAME "ESP32-BLE-AP"
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write (Server接收)
#define TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify (Server发送)



ESP32BleSerialServer bleServer(BLE_NAME, SERVICE_UUID, RX_UUID, TX_UUID);
ESP32NeoPixel neonpixel(21, 20);



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
        bleServer.send_message(serialbuf);
        bufidx = 0; // 重置索引，准备接收下一条命令
        return true;
    }

    return false;
}



void setup() 
{
    Serial.begin(115200);
    neonpixel.begin();
    bleServer.begin();
}

void loop() 
{
    bleServer.loop_process();
    
    // 读取串口发送到蓝牙
    if (bleServer.check_connect())
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
    
    // 接收蓝牙数据打印到串口
    const char* rx = bleServer.recv_message();
    if (rx[0] != '\0') 
    {
        Serial.print("[BLE] ");
        Serial.println(rx);
        neonpixel.trigger(false, false, true);
    }

    neonpixel.update();
}