#ifndef ESP32NEOPIXEL_H
#define ESP32NEOPIXEL_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class ESP32NeoPixel // ESP32NeoPixel 非阻塞 LED 控制类
{
private:
    Adafruit_NeoPixel strip;

    // 状态标志位
    bool wait_active = false;
    bool read_active = false;
    bool write_active = false;

    // 时间戳
    unsigned long read_last_trigger = 0;
    unsigned long write_last_trigger = 0;
    unsigned long wait_last_trigger = 0;
    unsigned long last_toggle_time = 0;

    // 当前显示状态索引 (用于状态机)
    // 0: 显示颜色A, 1: 显示颜色B (或灭)
    uint8_t state_index = 0; 
    uint32_t current_color = 0;
    uint32_t last_color = 0;
    uint32_t color_idle;
    uint32_t color_read;
    uint32_t color_write;
    uint8_t brightness = 50;

    // 配置参数
    const unsigned long FLASH_INTERVAL = 100; // 闪烁间隔
    const unsigned long IDLE_TIMEOUT = 200;   // 信号超时时间

public:
    ESP32NeoPixel(uint8_t pin, uint8_t brightness) : strip(1, pin, NEO_RGB + NEO_KHZ800)
    {
        this->color_idle = strip.Color(255, 0, 0);
        this->color_read = strip.Color(0, 255, 0);
        this->color_write = strip.Color(0, 0, 255);
        this->brightness = brightness;
    }

    void begin() 
    {
        this->strip.begin();
        this->strip.clear();
        this->strip.setBrightness(brightness);
        this->strip.setPixelColor(0, 0, 0, 0);
        this->strip.show();
    }

    // 触发函数：根据颜色判断来源
    void trigger(bool wait=false, bool read=false, bool write=false) 
    {
        unsigned long current_time = millis();

        if (read) 
        { 
            this->read_active = true;
            this->read_last_trigger = current_time;
        } 
        else if (write) 
        { 
            this->write_active = true;
            this->write_last_trigger = current_time;
        }
        else if (wait)
        {
            this->wait_active = true;
            this->wait_last_trigger = current_time;
        }

    }

    // 核心更新逻辑
    void update() 
    {
        unsigned long current_time = millis();

        // 1. 检查超时，更新活跃状态
        if (this->read_active && (current_time - this->read_last_trigger > this->IDLE_TIMEOUT)) 
        {
            this->read_active = false;
        }
        if (this->write_active && (current_time - this->write_last_trigger > this->IDLE_TIMEOUT)) 
        {
            this->write_active = false;
        }
        if (this->wait_active && (current_time - this->wait_last_trigger > this->IDLE_TIMEOUT))
        {
            this->wait_active = false;
        }

        // 3. 闪烁逻辑 (基于时间间隔切换状态)
        if (current_time - this->last_toggle_time >= this->FLASH_INTERVAL) 
        {
            this->last_toggle_time = current_time;
            this->state_index = !this->state_index; // 切换状态索引 (0 <-> 1)

            this->current_color = this->color_idle;

            // state_inde:x 0 -> 1 -> 0 : on -> off -> on

            if (this->read_active && this->write_active) 
            {
                if (this->state_index == 0) 
                {
                    this->current_color = color_read; // 绿
                }
                else
                {   
                    this->current_color = color_write; // 蓝
                }
            } 
            else if (this->read_active && this->state_index == 0) 
            {
                this->current_color = color_read;
            } 
            else if (this->write_active && this->state_index == 0) 
            {
                this->current_color = this->color_write;
            }
            else if (this->wait_active && this->state_index == 0) 
            {
                this->current_color = 0;
            }

            if (this->current_color != this->last_color)
            {
                this->strip.setPixelColor(0, this->current_color);
                this->strip.show();
            }
            
            this->last_color = this->current_color;
        }
    }
};


#endif ESP32NEOPIXEL_H