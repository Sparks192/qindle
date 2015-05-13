（本文为备忘录性质，用于记录研究中发现的技术特性。下文均以G10二代阅读器为研究对象）

# 基本参数 #
## 硬件 ##
  * RK2818 处理器，ARM926EJ-S核心
  * 128M 内存
  * EPSON S1D13521 e-ink驱动芯片
  * EINK V220 显示屏
  * WM8904 音频芯片
  * Marvell sd8686 无线网卡

## 软件 ##
  * Linux-2.6.25
  * glibc-2.9
  * Qt-4.7.4

# 系统设计 #
## 基本架构 ##
应用程序均运行在Qt Framework下。底层是名为boeyeserver的程序，一方面作为qwsserver向应用程序提供输入法、状态栏等服务，一方面通过dbus传输和执行各类消息。此外还有名为netserver的底层程序，作用类似于wpa\_cli，与wpa\_supplicant通信完成无线网络连接。

## 输入设备 ##
输入设备包括键盘和5向摇杆。5向摇杆的方向映射为方向键，确认映射为Enter键（与键盘上的Enter键完全相同）

Qt中特殊按键的映射如下：
| SYM | F6 |
|:----|:---|
| Scroll | F7 |
| Refresh | F5 |
| HOME | F8 |
| PrevPage | PageUp |
| NextPage | PageDown |
| MENU | F9 |
| BACK | Esc |

按键对应关系可通过/boot/keymaps下文件得到。

## 显示设备 ##
屏幕以framebuffer的形式驱动，大小为824x1200，不支持旋转，不支持双缓冲。支持8bpp, 16bpp, 32bpp显示模式，默认运行在16bpp。

注：8bit模式下line\_length需除以2.

## 屏幕刷新方式 ##
总所周知，eink屏幕需要手动执行指令刷新屏幕。此系统似乎并未重写Qt的显示驱动，而是通过另外的库libboeye.so和libboeyewidget.so，以重写控件的形式插入刷新指令。

底层的刷新操作是通过ioctl传递到内核。从迹象上看似乎是直接对应到驱动芯片指令。目前已知的指令包括

```
#define FBIO_UPD_DISPLAY_FULL   4621
#define FBIO_UPD_DISPLAY_AREA  4622
```

其中UPD\_DISPLAY\_FULL表示全屏刷新，也是通常使用的形式。UPD\_DISPLAY\_AREA表示只刷新部分区域，连续执行可并行调用多个刷新单元，达到类似动画的效果（不确定具体用法）。

结构体参数的格式如下：
```
    struct update_args_t {
        unsigned int    mode;
        unsigned int    x;
        unsigned int    y;
        unsigned int    w;
        unsigned int    h;
    }
```
其中mode的取值推测为
```
#define BS_UPD_MODE_INIT        0       //  INIT (clear screen)
#define BS_UPD_MODE_MU          1       //  MU (monochrome update)
#define BS_UPD_MODE_GU          2       //  GU (grayscale update)
#define BS_UPD_MODE_GC          3       //  GC (grayscale clear)
```
其中INIT执行清屏操作；MU是快速刷新，只能显示黑白；GU是局部刷新，可显示16灰度，但速度较慢；GC为全局刷新。