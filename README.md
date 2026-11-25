# FlapFC 飞控板测试固件

扑翼飞控板（**30 × 30 mm、4 层、STM32F446RET6** 版）回板后的硬件自检固件。上电自动跑一遍测试，结果从 USB 虚拟串口和调试串口同时输出，之后进入命令行，可手动测试舵机、电机、编码器等。

> 状态：已编译通过，**尚未在实物上运行过**。第一次上板请按“第一次上电”的顺序来。

## 编译和烧录

这是一个标准的 [PlatformIO](https://platformio.org/install) 工程，macOS / Linux / Windows 用法相同。仓库里只有应用代码，ST 的 HAL 库和编译器由 PlatformIO 在第一次编译时自动下载。

```sh
pio run                      # 编译
pio run -t upload            # 用 ST-Link 烧录（背面测试点 SWDIO / SWCLK / GND / 3V3）
pio run -e dfu -t upload     # 不用调试器，走 USB 烧录（见下）
pio device monitor           # 打开控制台（USB 虚拟串口）
```

**USB（DFU）烧录**：按住 BOOT（SW3），点一下 RESET（SW2），松开 BOOT，板子进入 ROM Bootloader，然后执行 `pio run -e dfu -t upload`。固件跑起来以后，在控制台输入 `dfu` 也能进入 Bootloader，不用再按键。

在 VS Code 里用 PlatformIO 扩展打开本目录，也可以直接点 Build / Upload / Monitor，环境选 `stlink` 或 `dfu`。

macOS 上烧录和串口都不需要装驱动，串口设备名形如 `/dev/cu.usbmodemXXXX`。Apple Silicon 机器上如果提示需要 Rosetta，执行 `softwareupdate --install-rosetta`。Windows 上用 DFU 烧录时，如果 dfu-util 找不到设备，需要用 Zadig 给 “STM32 BOOTLOADER” 装 WinUSB 驱动。

## 适用硬件

引脚对应嘉立创EDA 工程 Kero-Hardware 里的 `ControllerBoard_30x30_4L_backup`，固件用到的 41 个 MCU 信号已逐一对照该板的网表核对一致。**只适用于 30 × 30 这一版**，同一工程里的缩小版引脚和器件不同。

| 功能 | 引脚 |
|---|---|
| LED 绿 / 蓝 / 红（高电平亮） | PB1 / PB2 / PB10 |
| nRF24L01+（SPI1） | PA5 SCK，PA6 MISO，PA7 MOSI，PA4 CSN，PA3 CE，PC4 IRQ |
| BMI088（SPI3） | PB3 SCK，PB4 MISO，PB5 MOSI，PB6 ACC_CS，PA15 GYRO_CS，PB12 ACC_INT，PB7 GYRO_INT |
| I2C1：BMP388（0x76）+ VL53L1X（0x29） | PB8 SCL，PB9 SDA，PC15 BARO_INT，PC5 TOF_XSHUT，PB0 TOF_INT |
| SD 卡（SDIO 1-bit） | PC8 D0，PC12 CLK，PD2 CMD |
| 舵机 1/2/3（TIM2，50 Hz） | PA0 / PA1 / PA2 |
| 电机 PWM（TIM1_CH1，20 kHz） | PA8 |
| 编码器（TIM3） | PC6 A，PC7 B |
| 电池电压 / 充电 / USB 插入 | PC0（VSYS÷2），PC2，PA9 |
| USB FS | PA11 DM，PA12 DP |
| 调试串口 USART3，115200 8N1 | PC10 TX，PC11 RX（背面测试点） |

## 第一次上电

1. 先**不接电机和舵机**。固件不会自己驱动它们（只有输入 `servo` / `sweep` / `motor` 命令才会动），但第一次上电还是空载更稳妥。
2. 插 USB（或接电池并打开电源开关）。三颗 LED 依次亮一遍（绿→蓝→红），然后自检约 5 秒。
3. 结束后：**绿灯常亮 = 没有失败项，红灯常亮 = 有失败项**；蓝灯每秒闪一下表示程序在跑。红灯快闪 = 程序异常（HardFault）。
4. 打开 USB 虚拟串口（波特率随意），会自动重新打印一遍自检报告。也可以用 3.3 V 串口线接 PC10/PC11。

## 自检内容

| 项目 | 判定依据 |
|---|---|
| HSE 晶振 | 8 MHz 晶振起振并锁相到 168 MHz；不起振则退回内部 RC 并报 FAIL |
| 3V3、VSYS | 用内部基准反推 VDDA；VSYS 为 0 时提示电源开关关闭 / 仅 USB 供电 |
| I2C 总线 | 空闲电平、地址扫描 |
| BMP388 | 芯片 ID、气压温度是否合理、INT 脉冲计数 |
| VL53L1X | 型号 ID、XSHUT 拉低后是否掉线、INT 空闲电平 |
| BMI088 | 加速度计 / 陀螺仪 ID、寄存器回读、静止时 \|a\|≈1 g、两路 INT 脉冲计数 |
| nRF24L01+ | 寄存器读写、实发一包（验证 CE、IRQ、16 MHz 晶振）、2.4 GHz 能量扫描（天线通路） |
| SD 卡 | 初始化、卡信息、分别在 400 kHz / 8 MHz / 24 MHz 下读块 0 |
| USB | 是否被主机枚举、VBUS_DET 电平 |

没有自动覆盖、需要手动测的：舵机输出、电机驱动、编码器、ToF 实际测距、SD 写入。

## 命令

| 命令 | 作用 |
|---|---|
| `help` | 命令列表 |
| `t` / `t <名称>` | 重跑全部 / 单项（clock power i2c baro tof imu rf sd enc usb） |
| `last` | 重新打印上一次完整报告 |
| `imu` `baro` `bat` `enc` `irq` | 实时读数，按任意键停止 |
| `rfscan` | 2.4 GHz 各信道能量分布 |
| `sdw` | SD 写入验证（把块 1 原样写回再校验，不改变卡内数据） |
| `servo <1\|2\|3\|all> <500..2500\|off>` | 舵机脉宽（µs） |
| `sweep` | 三路舵机 1500→1000→2000→1500 后关闭 |
| `motor <1..100> [ms]` | 电机占空比（%），默认 1 秒，最长 10 秒，按任意键立即停 |
| `led` | 依次点亮三颗 LED |
| `dfu` / `reboot` | 进入 USB Bootloader / 重启 |

## 注意

- **电机直接接在电池上**（不经电源开关），`motor` 命令会让它真的转。先用小占空比，例如 `motor 10 500`。
- 舵机电源是 VSYS（电池电压），不是 5 V；只插 USB、电源开关关着时舵机没有电。
- SD 的 24 MHz 一档用的是轮询读取，失败只报 WARN，不代表硬件有问题；400 kHz 和 8 MHz 才是判定依据。
- 加速度计模长偏离 1 g、陀螺仪读数偏大只报 WARN（板子可能没放平或在动）。

## 代码结构

```
include/
  board.h                引脚定义和板级接口
  app.h                  各模块之间的函数声明
  stm32f4xx_hal_conf.h   HAL 配置（8 MHz 晶振、启用的外设）
  usbd_conf.h, usbd_desc.h
src/
  main.c                 启动流程、主循环、进入 Bootloader
  board.c                时钟、GPIO、SPI / I2C / 定时器 / ADC 初始化
  selftest.c             自检流程和结果统计，时钟 / 电源 / USB / 编码器测试
  sensors.c              I2C 总线、BMP388、VL53L1X、BMI088
  rf.c                   nRF24L01+
  sdtest.c               SD 卡
  cli.c                  命令行
  console.c              USART3 + USB 双路控制台
  usbd_*.c               USB 虚拟串口
  stm32f4xx_it.c         中断入口
  syscalls.c             C 库所需的最小系统调用
platformio.ini           工程配置
link_flags.py            把硬件浮点选项传给链接器（PlatformIO 的限制）
STM32F446RE.ld           链接脚本（ST 原版，加了 .noinit 段）
```
