# FlapFC 飞控板测试固件 + 网页测试台

扑翼飞控板（**30 × 30 mm、4 层、STM32F446RET6** 版）回板后的硬件测试工具，分两部分：

- **固件**（`src/`、`include/`）：上电自动跑一遍自检，结果从 USB 虚拟串口和调试串口同时输出，之后进入命令行。
- **网页测试台**（`docs/`）：用浏览器通过 USB 直接连板子，实时显示每个传感器的数据和 IMU 姿态，控制电机和舵机，一键运行自检。可以挂在 GitHub Pages 上，打开网址就能用。

> 状态：固件已编译通过，网页已用内置的演示数据检查过；**两者都尚未在实物上运行过**。第一次上板请按“第一次上电”的顺序来。

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

## 网页测试台

**使用**：板子烧好固件、插上 USB 后，用电脑版 **Chrome 或 Edge** 打开页面，点“连接板子”，在弹出的列表里选板子的串口。Safari 和 Firefox 不支持 Web Serial，连不了板子。不接板子时可以点“演示数据”先看看界面。

页面内容：

| 区域 | 用途 |
|---|---|
| 模块状态 | 晶振、电源、I2C、IMU、气压计、ToF、无线、SD 卡、USB 各一个状态标记，综合自检结果和实时数据 |
| IMU 姿态 | 三维姿态示意，横滚 / 俯仰 / 航向角，加速度和角速度曲线 |
| 气压计 | 气压、温度、相对高度曲线 |
| 电源 | 电池电压、3V3、MCU 温度、USB 和充电状态 |
| 中断线 | 五根传感器中断线的实测脉冲频率，对照预期值 |
| 编码器 | 计数、速度、A/B 相电平 |
| 执行器 | 电机占空比滑块（需先解锁，带上限和停止键），三路舵机脉宽滑块 |
| 自检 | 运行全部或单项自检，结果表 |
| 无线 | 2.4 GHz 各信道能量扫描图 |
| 控制台 | 固件的原始文本输出，可手动输入命令 |

**电机安全**：电机只有在点了“解锁电机”之后才响应滑块；网页每 0.15 秒重发一次设定值，固件 0.6 秒收不到就自动停机并上锁，所以关掉页面、拔线、把页面切到后台都会让电机停下。运行任何自检也会先停电机。

**尚未实现**：ToF 测距。固件目前只检查 VL53L1X 是否在线，网页上没有距离读数。

### 挂到 GitHub Pages

网页是纯静态文件，没有构建步骤。仓库推到 GitHub 后：

1. 仓库页面 → Settings → Pages。
2. Source 选 “Deploy from a branch”，Branch 选 `master`，文件夹选 `/docs`，保存。
3. 一两分钟后页面出现在 `https://<用户名>.github.io/<仓库名>/`。

免费账号只能给公开仓库开 Pages。Web Serial 要求 HTTPS，GitHub Pages 自带。

本地预览：

```sh
python -m http.server 8000 --directory docs
```

然后打开 `http://localhost:8000`（localhost 也允许使用 Web Serial）。

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

供网页或脚本使用的命令（见下节）：`stream`、`echo`、`info`、`arm`、`disarm`、`M`、`S`。

## 通信协议（USB 虚拟串口）

网页和固件之间就是一行一条的文本，自己写脚本也可以直接用。命令以回车结尾。

| 命令 | 作用 |
|---|---|
| `echo off` / `echo on` | 关闭 / 打开字符回显和提示符 |
| `info` | 返回一行 JSON：固件版本、板名、芯片 ID |
| `stream <hz>` | 开始按 1–100 Hz 发送遥测，`stream 0` 停止。只走 USB，不走调试串口 |
| `arm` / `disarm` | 允许 / 禁止实时电机设定；`disarm` 同时停机 |
| `M <0..1000>` | 电机占空比（千分比），仅在解锁后有效；**0.6 秒内不重发就自动停机并上锁** |
| `S <1..3> <us>` | 舵机脉宽 500–2500 µs，`0` 为停止输出 |

除了上表这几条，其它任何命令都会先让电机停机并上锁。主机关闭串口或拔线时，固件自动停止遥测、恢复回显并停机。

遥测每行一个 JSON 对象：

```json
{"t":12345,"a":[0.012,-0.003,1.001],"g":[0.12,-0.05,0.02],"it":27.1,
 "p":101325.0,"bt":26.80,"tof":1,"vs":3.980,"vd":3.301,"mt":31.2,"vb":1,"ch":0,
 "en":-12,"ea":1,"eb":0,"iq":[0,2,5012,502,251],"m":0,"s":[0,0,0],"ar":0}
```

| 字段 | 含义 |
|---|---|
| `t` | 上电后的毫秒数 |
| `a` / `g` / `it` | 加速度（g）、角速度（°/s）、IMU 温度（°C），BMI088 芯片坐标系 |
| `p` / `bt` | 气压（Pa）、气压计温度（°C） |
| `tof` | 1 = VL53L1X 应答且型号 ID 正确 |
| `vs` / `vd` / `mt` | VSYS（V）、VDDA（V）、MCU 温度（°C） |
| `vb` / `ch` | VBUS_DET、CHG_DET 引脚电平（`vb`=1 时 `ch`=0 表示充电中） |
| `en` / `ea` / `eb` | 编码器计数（16 位有符号）、A/B 相电平 |
| `iq` | 中断脉冲累计数，顺序为 ToF、无线、陀螺仪、加速度计、气压计 |
| `m` / `s` / `ar` | 电机实际占空比（千分比）、三路舵机实际脉宽（µs，0 = 无脉冲）、是否已解锁 |

没有应答的传感器对应字段为 `null`。IMU 每帧更新，ADC、气压计、ToF 在线状态每秒更新 10 次。其它以 `{"k":…}` 开头的行是事件（`hello`、`rfscan`），不以 `{` 开头的行是给人看的文本。

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
  telemetry.c            遥测数据流、电机实时控制和超时保护
  console.c              USART3 + USB 双路控制台
  usbd_*.c               USB 虚拟串口
  stm32f4xx_it.c         中断入口
  syscalls.c             C 库所需的最小系统调用
docs/                    网页测试台（GitHub Pages 从这个文件夹发布，所以必须叫 docs）
  index.html, style.css
  js/app.js              页面逻辑：解析数据、更新界面、发送控制命令
  js/link.js             Web Serial 连接，以及不接板子时的演示数据
  js/ahrs.js             姿态解算（陀螺仪 + 加速度计互补滤波）
  js/board3d.js          三维姿态示意
  js/charts.js           曲线图和柱状图
platformio.ini           工程配置
link_flags.py            把硬件浮点选项传给链接器（PlatformIO 的限制）
STM32F446RE.ld           链接脚本（ST 原版，加了 .noinit 段）
```
