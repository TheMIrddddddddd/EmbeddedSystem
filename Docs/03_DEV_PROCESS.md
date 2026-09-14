# 工业数据采集终端 —— 开发流程(执行级展开)

> 文档版本:V1.0 | 日期:2026-08-09
> 定位:本文是《01_PROJECT_OVERVIEW.md》第十五章 M0~M7 里程碑的**执行级展开**——把每个里程碑翻译成「动作清单 → 产出文件 → 依赖 → 卡点 → 验收关卡」。
> 使用方式:开发时照表执行,验收时对照《01》第十四章(A-01~Q-02)与第十四-2(稳定性)逐条验证;技术原理见《02_TECH_STACK.md》。
> 当前工程状态:M0~M3 已完成;M4 采集与通信阶段已完成 M4-0~M4-6——采样滤波换算、USART0 CLI 全量、RTC 日历、RS485 自定义协议全量(含自动上报与心跳)、Modbus RTU 从站及 Python 回归均已完成板测;当前进入 M5 TF 卡与告警。

---

## 〇、开发总原则

1. **每阶段结束必须"能烧、能跑、能验证"**,不允许悬空中间态;阶段之间永远有一个可交付的固件。
2. **先地基后功能**:目录结构 → 双工程 → 公共契约 → 组件 → RTOS 骨架 → 业务功能。架构返工比功能返工贵十倍,架构问题必须在前三个阶段解决。
3. **可测试组件在 PC 上单测**:RingBuffer / CRC / CLI / Flash KV / 协议编解码 / 升级序列化全部带 PC 测试,不占板机时。
4. **Common 契约单一来源**:Boot 与 App 必须编译同一份 `Common/` 定义(禁止复制粘贴);持久化一律逐字段编解码,禁止 `memcpy` 结构体(见《01》十二-2、十七-3)。
5. **关键路径显式初始化**:核心初始化集中在 composition root 显式调用;section 自动注册默认关闭(见《01》十七-6)。
6. **每个里程碑的验收直接引用《01》的验收编号**,不另造标准。

---

## 一、阶段总览与依赖

```
当前(单工程闪灯)
   │
   ▼
M0 硬件资源冻结 ──────────────┐
   │ board_config.h           │
   ▼ board_dma_map.h          │
M1 Boot/App 双工程地基 ◄──────┘ (链接脚本引用 common_flash_layout.h)
   │ Common 契约最小子集
   ▼
M2 公共基础组件(RingBuffer/CRC/CLI/KV/协议/ebtn,PC 单测)
   │
   ▼
M3 FreeRTOS APP 骨架(七任务 + 队列/事件组 + IWDG + FatFs 所有权)
   │
   ├────► M4 采集与通信(ADC/DAC、CLI、自定义协议、Modbus、Python 回归)
   ├────► M5 TF 卡与告警(三类存储、config 原子导入、Flash KV、告警状态机)
   │
   ▼
M6 Bootloader(在线 IAP + TF 离线 + 双槽元数据 + 启动确认/回滚)
   │
   ▼
M7 低功耗 + 总验收
```

**强制顺序(不可提前):**

| 前置 | 后置 | 原因 |
|---|---|---|
| M0 资源表 | M1 双工程及之后所有 BSP | 引脚/DMA/中断/Flash 边界未冻结,一切 BSP 无从写起 |
| M1 双工程 | M6 Bootloader 全部 | Boot/App 分区与跳转是升级机制存在的前提 |
| M3 RTOS 骨架 | M4/M5/M7 业务功能 | 所有业务都是七任务的负载,骨架先行 |
| M2 组件 | M4 协议、M5 KV、M6 序列化 | 编解码/校验/存储引擎被业务与 Bootloader 复用 |

---

## 二、阶段 0:基线动作(当前状态)

| 项 | 现状 |
|---|---|
| 工程 | 单工程,Keil(AC5)+ EIDE(GCC)双工具链,链接 0x08000000,无分区 |
| 代码 | `User/src/main.c` 38 行闪灯(前后台 + SysTick 1ms 时基),官方库完整 |
| 文档 | 《01》任务书、《02》技术栈、《03》本文,设计已冻结 |
| 工具 | OpenOCD + CMSIS-DAP 调试配置可用;编译产物已存在 |

**动作清单:**

1. **提交设计基线**:将 `Docs/` 全部入库(git commit),形成可追溯的设计快照;
2. **确认原理图来源**:板级引脚表(见 M0 卡点)必须来自原理图或厂家引脚说明,不接受"边写边猜";
3. 当前闪灯 demo 定位为"最小系统验证完成"状态,后续被 M1 吸收,不再演进。

**产出:** Docs 设计基线入库;引脚信息来源明确。

---

## 三、M0:硬件资源冻结

**目标:** 冻结全部硬件资源,产出板级配置表,消灭一切资源冲突。

**动作清单(五张表,按序):**

| # | 表 | 内容 | 依据 |
|---|---|---|---|
| 1 | 引脚分配表 | 每个外设 → 端口/引脚/AF/极性/默认电平/默认实例 | 原理图 |
| 2 | 时钟树表 | 25MHz HXTAL → PLL 240MHz;AHB/APB1/APB2 分频;USART/ADC/SPI/SDIO 时钟源;RTC = LSE 32.768k | 《02》2.1 |
| 3 | DMA 通道表 | ADC1 / USART0-RX / USART1-RX / SDIO 各占 DMA 控制器与通道,**编译期冲突检查** | 《02》3.1/3.3 |
| 4 | NVIC 优先级表 | USART IDLE、DMA、EXTI(按键)、RTC 闹钟、SDIO、SysTick 的抢占/子优先级 | 《01》十六 |
| 5 | Flash 边界表 | Boot 64KB / 旧内部 Meta 保留区 / App / Backup / Staging;App/Backup/Staging manifest 预留区按契约定义,升级元数据双槽位于外部 GD25Q40E | 《01》十二-1(宏已给出) |

**产出文件:**

```
BSP/board_config.h                         ← 当前板级引脚/时钟宏配置
BSP/Boards/gd32f470ve_v1/board_dma_map.h   ← 后续 DMA/定时器通道静态表 + 冲突检查
```

**写法参考(借用 PX4 h743mini,只借"怎么写",不借 OS 机制):**

| 参考文件 | 借鉴点 |
|---|---|
| `boards/gjl/h743mini/src/board_config.h` | 引脚宏打包:`(方向\|推挽\|速度\|初始电平\|端口\|引脚)` 一个宏定义全量信息 |
| `boards/gjl/h743mini/src/timer_config.cpp` | `constexpr` 通道静态表:每通道 = {定时器,通道,GPIO},编译期定死 |
| `boards/gjl/h743mini/src/spi.cpp` | 总线/设备/CS 静态表 + `validateSPIConfig()` 编译期校验 |

**资料状态:** 已取得 `CIMC-IHD-v04` 西门子原理图 PDF,并结合板级引脚确认完成本阶段的 GPIO 初始冻结。用户补充的本地芯片数据手册为 `D:\STM322222222222222222222\GD32_SieMens\GD32F470xxDatasheet_Rev2.1.pdf`;其中第 52~55 页用于核对 GPIO/AF、ADC/DAC 引脚,第 86 页给出 LXTAL 32.768kHz 电气参数。DMA 请求映射仍以 GD32F4xx User Manual Rev3.4 表 10-2/10-3 为准。用户已说明此前使用裸机程序确认过这块板的板载硬件,因此 M0 的硬件资源冻结与板级基础确认按本项目边界视为完成;DMA/NVIC 初始化、驱动代码和 FreeRTOS 集成统一放到 M3/M4 实现。外部 SPI Flash 已按用户指定冻结为 `GD25Q40E`:4Mbit/512KB、4KB 扇区;当前用途方向为参数/告警以及后续可选的升级包/备份镜像。Bootloader 与 App 的执行位置统一放在 GD32F470 内部 Flash;`0x9F` JEDEC ID 作为 M3 SPI Flash 驱动的最小软件验收项,外部 Flash 具体分区待后续实现阶段确定。

### M0 当前 GPIO/外设映射表

> 来源: `CIMC-IHD-v04` 西门子原理图第 1~4 页 + 用户于 2026-08-15/2026-08-17 提供的板级确认 + 本地 `GD32F470xxDatasheet_Rev2.1.pdf`。当前项目涉及的 AF 编号已经由数据手册核对。用户已确认此前用裸机程序验证过全部板载硬件;表格中的“归 M3 实现”表示软件驱动、DMA/NVIC、任务所有权和上层接口尚未写入当前工程,不表示 M0 硬件资源未闭合。

#### MCU GPIO 与板级外设

| 功能 | GD32F470 GPIO | 板级网络/器件 | 方向/电气语义 | 连接器或路由 | 来源/状态 |
|---|---|---|---|---|---|
| LED1~LED6 | `PD8~PD13` | `LED1~LED6` | GPIO 输出;当前软件定义为高电平点亮、低电平熄灭 | H2: 6→LED1、5→LED2、4→LED3、3→LED4、2→LED5、1→LED6 | 原理图 P2 + `BSP/board_config.h`;用户已有裸机逐路确认;LED 驱动与状态服务归 M3 |
| KEY1~KEY6 | `PE15、PE13、PE11、PE9、PE7、PB0` | `FUN_KEY1~FUN_KEY6` | GPIO 输入;外部 10k 上拉到 3V3,按下接 DGND,有效低 | H3: 6→KEY1、5→KEY2、4→KEY3、3→KEY4、2→KEY5、1→KEY6 | 原理图 P1/P2 + 用户确认;用户已有裸机确认;GPIO 扫描、消抖和 ebtn/FreeRTOS 事件归 M3 |
| ADC CH0 | `PC0` | `ADC1` 规划使用 | 模拟输入 | VR1 滑动端 → `ADC`; `ADC012_IN10` | 原理图 P1/P2 + 用户确认; ADC 通道 10 已确认; ADC1 实例/DMA/板测待完成 |
| DAC CH1 | `PA4` | `DAC0_OUT0` | 模拟输出 | 设计闭环为 `PA4` → 外部跳线 → `PC1` | 原理图 P1 + 项目规格; `DAC0_OUT0` 已确认; 板测待完成 |
| ADC CH1 | `PC1` | `ADC1` 规划使用 | 模拟输入 | 接收 DAC `PA4` 回读信号; `ADC012_IN11` | 原理图 P1 + 用户确认; ADC 通道 11 已确认; ADC1 实例/DMA/板测待完成 |
| SD DAT0 | `PC8` | `SD_DAT0` | SDIO 4-bit 双向数据 | TF 卡 DAT0 | 原理图 P1/P2 + 用户确认; `SDIO_D0`, `AF12`;硬件已由用户裸机确认;SDIO/DMA/FatFs 归 M3 |
| SD DAT1 | `PC9` | `SD_DAT1` | SDIO 4-bit 双向数据 | TF 卡 DAT1 | 原理图 P1/P2 + 用户确认; `SDIO_D1`, `AF12`;硬件已由用户裸机确认;SDIO/DMA/FatFs 归 M3 |
| SD DAT2 | `PC10` | `SD_DAT2` | SDIO 4-bit 双向数据 | TF 卡 DAT2 | 原理图 P1/P2 + 用户确认; `SDIO_D2`, `AF12`;硬件已由用户裸机确认;SDIO/DMA/FatFs 归 M3 |
| SD DAT3 | `PC11` | `SD_DAT3` | SDIO 4-bit 双向数据/片选复用网络 | TF 卡 DAT3/CS | 原理图 P1/P2 + 用户确认; `SDIO_D3`, `AF12`;硬件已由用户裸机确认;SDIO/DMA/FatFs 归 M3 |
| SD CLK | `PC12` | `SD_CLK` | SDIO 时钟输出 | TF 卡 CLK | 原理图 P1/P2 + 用户确认; `SDIO_CK`, `AF12`;硬件已由用户裸机确认;SDIO 初始化归 M3 |
| SD CMD | `PD2` | `SD_CMD` | SDIO 命令双向线 | TF 卡 CMD/DI | 原理图 P1/P2 + 用户确认; `SDIO_CMD`, `AF12`;硬件已由用户裸机确认;SDIO/DMA 归 M3 |
| SD card detect | `PE2` | `SD_CD` | GPIO 输入;原理图有 10k 上拉,插卡检测按低有效理解 | TF 卡 CD | 原理图 P1/P2 + 用户确认;硬件已由用户裸机确认;卡检测与热插拔策略归 M3/M5 |
| USART1 TX | `PA2` | `USART1_TX` | USART 发送 | H6 中间左侧;可路由到 MAX3485 或 SP3232 | 原理图 P1/P4 + 用户确认; `AF7`; H6 RS485 路由已现场确认;USART1 BSP/DMA 归 M3 |
| USART1 RX | `PA3` | `USART1_RX` | USART 接收 | H6 中间右侧;可路由到 MAX3485 或 SP3232 | 原理图 P1/P4 + 用户确认; `AF7`; H6 RS485 路由已现场确认;USART1 BSP/DMA 归 M3 |
| RS485 方向控制 | `PA1` | `485_CS` | GPIO 输出;图中同时连接 MAX3485 `RE#`/`DE`,低为接收、高为发送 | MAX3485 U12 | 原理图 P1/P4 + 用户确认;PA1/485_CS 连接已确认;方向控制驱动归 M3 |
| USART2 TX | `PB10` | `USART2_TX` | 3.3V TTL USART 发送 | CN1 pin 2 | 原理图 P1/P4 + 用户确认; `AF7`;USART2 BSP 归 M3 |
| USART2 RX | `PB11` | `USART2_RX` | 3.3V TTL USART 接收 | CN1 pin 3 | 原理图 P1/P4 + 用户确认; `AF7`;USART2 BSP 归 M3 |
| USART0 TX | `PA9` | `USART0_TX` | 3.3V TTL USART 发送 | H7 → 板载 CH340C | 原理图 P1/P4 + 用户确认; `AF7`; H7 与 COM4/CH340 现场确认;USART0 BSP/DMA 归 M3 |
| USART0 RX | `PA10` | `USART0_RX` | USART 接收 | H7 → 板载 CH340C | 原理图 P1/P4 + 用户确认; `AF7`; H7 与 COM4/CH340 现场确认;USART0 BSP/DMA 归 M3 |
| OLED DATA | `PB9` | `OLED_DAT` | OLED 数据线;项目技术栈按 I2C OLED 规划 | OLED1 SDA/DAT | 原理图 P1/P2 + 用户确认; `I2C0_SDA`, `AF4`;硬件已由用户裸机确认;I2C0/SSD1306/DisplayTask 归 M3 |
| OLED CLK | `PB8` | `OLED_CLK` | OLED 时钟线;项目技术栈按 I2C OLED 规划 | OLED1 SCL/CLK | 原理图 P1/P2 + 用户确认; `I2C0_SCL`, `AF4`;硬件已由用户裸机确认;I2C0/SSD1306/DisplayTask 归 M3 |
| SPI Flash MOSI | `PB15` | `SPI_MOSI` | SPI 主出从入 | GD25Q40E U4 SI/IO0 | 原理图 P1/P2 + 用户确认; `SPI1_MOSI`, `AF5`;硬件已由用户裸机确认;SPI1 原始驱动归 M3 |
| SPI Flash MISO | `PB14` | `SPI_MISO` | SPI 主入从出 | GD25Q40E U4 SO/IO1 | 原理图 P1/P2 + 用户确认; `SPI1_MISO`, `AF5`;硬件已由用户裸机确认;SPI1 原始驱动归 M3 |
| SPI Flash SCK | `PB13` | `SPI_SCK` | SPI 时钟输出 | GD25Q40E U4 CLK | 原理图 P1/P2 + 用户确认; `SPI1_SCK`, `AF5`;硬件已由用户裸机确认;SPI1 原始驱动归 M3 |
| SPI Flash CS | `PB12` | `FLASH_CS` | GPIO 输出,低有效片选 | GD25Q40E U4 CS | 原理图 P1/P2 + 用户确认;项目使用普通 GPIO CS,不使用 `SPI1_NSS`;硬件已由用户裸机确认;CS/读 ID/原始读写归 M3 |

#### M0 时钟树初步确认（2026-08-16）

**结论（源代码和实际构建配置级）:** 当前工程实际选择 `GD32F470` 的 `25MHz HXTAL → PLL 240MHz` 路径;启动文件在进入 `main()` 前调用 `SystemInit()`,因此 LED 闪烁程序使用的系统时钟路径就是这一套配置。下面的数值来自 `system_gd32f4xx.c` 的活动分支,不是根据 LED 闪烁现象反推。

| 时钟节点 | 配置/计算 | 结果 |
|---|---|---|
| HXTAL | 外部高速晶振 | `25MHz` |
| PLLP / SYSCLK | `25MHz / PSC25 × PLL_N480 / PLL_P2` | `240MHz` |
| AHB / HCLK | `SYSCLK / 1` | `240MHz` |
| APB2 / PCLK2 | `AHB / 2` | `120MHz` |
| APB1 / PCLK1 | `AHB / 4` | `60MHz` |

**当前项目外设所属总线:**

| 总线 | 项目外设 | 对应板级功能 |
|---|---|---|
| APB2 | `USART0`、`ADC1`、`SDIO` | CH340/COM4 调试串口、`PC0/PC1` ADC 规划、TF 卡 |
| APB1 | `USART1`、`USART2`、`SPI1`、`I2C0`、`DAC` | RS485/RS232、CN1 TTL 串口、SPI Flash、OLED、`PA4` DAC |

这里先冻结的是**总线归属和系统时钟**。USART 波特率、ADC 分频、SPI 分频、SDIO 输出频率还要等各外设初始化参数确定后再计算,不能直接把 `PCLK1/PCLK2` 当成最终通信速率。

**RTC 状态:** CMSIS 头文件默认定义 `LXTAL_VALUE=32768`;西门子原理图 P1 还明确画出了 `X1=32.768kHz` 并连接到 `OSC32K_IN/OSC32K_OUT`,因此 LSE 晶振在板级设计上存在。当前工程没有找到启用 `LXTAL` 并调用 `rcu_rtc_clock_config(RCU_RTCSRC_LXTAL)` 的活动初始化代码,所以 RTC 仍未进入当前运行配置;X1 实物装配、通断和运行波形尚未板测。

**HXTAL 板级证据:** 西门子原理图 P1 画出了 `X2=25MHz` 并连接到 `OSC25M_IN/OSC25M_OUT`,与当前 `25MHz HXTAL` 软件路径一致。

**证据与边界:** `system_gd32f4xx.c` 的 `GD32F470` 编译分支、PLL 参数、AHB/APB 分频已确认;`startup_gd32f450_470.s` 已确认 `SystemInit()` 先于 `main()` 执行。当前没有示波器/频率计对 HXTAL、LXTAL、SYSCLK 或外设时钟做板级测量,所以本条属于原理图 + 源代码/构建确认,不等于晶振波形和最终外设时钟的实测确认。

#### M0 DMA 请求映射（2026-08-16）

**结论（芯片手册映射级）:** GD32F4xx 官方用户手册 Rev3.4 的 DMA 请求表已经给出这些外设的可选位置。下面是结合本项目需求得到的**推荐无冲突分配**,只完成资源冻结建议,当前工程还没有 DMA 初始化代码。

| 项目 DMA 请求 | DMA 控制器 | `PERIEN[2:0]` / 库枚举 | 推荐通道 | 对应中断 | 当前状态 |
|---|---|---|---|---|---|
| USART1-RX（RS485/RS232） | `DMA0` | `100` / `DMA_SUBPERI4` | `Channel5` | `DMA0_Channel5_IRQn` | 推荐冻结;尚未实现 |
| USART0-RX（CH340/COM4） | `DMA1` | `100` / `DMA_SUBPERI4` | `Channel5` | `DMA1_Channel5_IRQn` | 推荐冻结;尚未实现 |
| ADC1 routine DMA（PC0/PC1） | `DMA1` | `001` / `DMA_SUBPERI1` | `Channel2` | `DMA1_Channel2_IRQn` | 推荐冻结;尚未实现 |
| SDIO data DMA（TF 卡） | `DMA1` | `100` / `DMA_SUBPERI4` | `Channel6` | `DMA1_Channel6_IRQn` | 推荐冻结;尚未实现 |

**为什么这样分配:** 手册中 `ADC1` 可放在 `DMA1 Channel2/3`, `USART0_RX` 可放在 `DMA1 Channel2/5`,`SDIO` 可放在 `DMA1 Channel3/6`;选择 `ADC1→2`、`USART0-RX→5`、`SDIO→6` 后,同一 DMA 控制器的通道不重复。`USART1_RX` 在 `DMA0 Channel5`,不与 DMA1 的选择冲突。SDIO 手册示例也明确使用 `DMA1 Channel3 或 Channel6`,本项目选择 Channel6。

**后续可选请求:** 如果以后需要 USART 发送 DMA,手册给出 `USART1_TX→DMA0 Channel6`、`USART0_TX→DMA1 Channel7`;它们暂不纳入当前 M0 必选分配。DMA0/DMA1 时钟位分别是 `RCU_AHB1EN_DMA0EN`、`RCU_AHB1EN_DMA1EN`;所有 DMA 缓冲区仍必须放普通 SRAM,不能放 TCM。

**证据与边界:** 映射来源为官方《GD32F4xx User Manual Rev3.4》表 10-2/10-3（PDF 第 203 页）以及 SDIO DMA 示例（PDF 第 661 页）;本地库提供 `DMA_SUBPERI0~7` 和对应 DMA 中断向量。当前没有 DMA 代码、DMA 中断处理和板级数据收发,所以“通道可用”不等于“DMA 功能已完成”。

#### M0 供电输入初步确认（2026-08-16）

西门子原理图 P3 的电源链路标注 `TPS5450DDAR` 输入范围为 `10~31V`,外部输入端标注为 `24V`;因此按原理图设计,`12V` 处于允许输入范围内,可以作为这块板的外部供电候选。这个结论只覆盖稳压器输入范围,接线前仍必须确认电源端子极性、`DGND` 回路和实物丝印,并进行限流上电测试;不能把 `12V` 直接接到 `3V3` 或 `+5V` 排针。

#### 串口跳线和外部接口约定

| 接口/跳线 | 编号或引脚 | 连接关系 | 用途 |
|---|---|---|---|
| H6 | `1-3`、`2-4` | `485_RX↔USART1_TX(PA2)`、`485_TX↔USART1_RX(PA3)` | USART1 选择 RS485; 2026-08-15 已现场插接确认;不可与 RS232 跳线同时安装 |
| H6 | `3-5`、`4-6` | `USART1_TX(PA2)↔232_RX`、`USART1_RX(PA3)↔232_TX` | USART1 选择 RS232;不可与 RS485 跳线同时安装 |
| H7 | `1-3`、`2-4` | `USB_RX↔USART0_TX(PA9)`、`USB_TX↔USART0_RX(PA10)` | 板载 CH340C 连接 USART0 CLI; 2026-08-15 已现场确认,电脑识别为 COM4 |
| CN1 | 1/2/3 | `1=DGND、2=USART2_TX、3=USART2_RX` | 外部 3.3V TTL USART2 |
| CN2 | 1/2/3 | `1=DGND、2=485_A、3=485_B` | 外部 RS485 总线; 2026-08-15 已现场确认标号 |
| CN3 | 1/2/3 | `1=DGND、2=232_OUT_TX、3=232_OUT_RX` | 外部 RS232 总线,经 SP3232 |

#### 2026-08-15 用户现场确认记录

> 以下记录表示用户现场观察、插接或识别结果,不等同于 USART/RS485 软件收发已实现,也不替代断电万用表通断测试。

> **补充边界（2026-08-17）:** 用户说明这块板此前已经通过裸机程序确认过全部板载硬件,包括 LED、按键、TF 卡、OLED、SPI Flash 以及串口/跳线接口。因此下表中的“尚未证明”只表示本仓库当前没有重复执行该硬件实验,不再作为 M0 的阻塞项;软件驱动、DMA、中断、任务和接口回归统一放到 M3/M4。

| 项目 | 已确认结果 | 尚未证明 |
|---|---|---|
| 最小系统 | 现有 `PD8→LED1` 闪灯程序运行正常 | LED2~LED6 的逐路板测 |
| USB 调试串口 | 电脑识别板载 CH340 为 `COM4`; H7 使用 `1-3`、`2-4` | 当前 `main.c` 尚未实现 USART0 接收/CLI |
| USART1 到 RS485 | H6 使用 `1-3`、`2-4`; `485-T↔PA3`、`485-R↔PA2` | USART1 初始化、PA1 方向控制软件、实际收发和 DMA 尚未验证 |
| RS485 方向控制 | `PA1=485_CS` 连接关系已在原理图中确认 | PA1 高低电平的板级波形/软件实测 |
| RS485 外部接口 | CN2: `1=G/DGND`、`2=A/A+`、`3=B/B-` | 外部设备、总线终端和实际通信尚未接入测试 |

#### M0 本阶段已完成（2026-08-17）

- 已创建 `BSP/board_config.h`，统一记录 LED、按键、USART、RS485 方向控制、I2C、SPI、SDIO、ADC 和 DAC 的板级端口、引脚、复用功能或 ADC 通道宏。
- 已将 LED GPIO 时钟宏 `BOARD_LED_GPIO_CLK` 纳入板级配置；`User/src/main.c` 通过 `board_config.h` 获取 LED1 的端口、引脚和 GPIO 时钟。
- 用户已确认 LED1~LED6、KEY1~KEY6、TF 卡、OLED、SPI Flash/GD25Q40E 以及 H6/H7、CN1/CN2/CN3 的板级硬件此前均已用裸机程序确认；当前仓库只保留 LED1 的最小复现，不重复扩展 M0 硬件测试。
- `main.c` 已使用当前 GCC 参数完成单文件编译验证；所有外设驱动的整体构建、RTOS 任务接入和软件回归统一放到 M3/M4。
- 本阶段相关代码、板级配置和项目文档已提交到 GitHub；Keil 工程已加入 `BSP` 头文件包含路径。

#### APP 层 LED 文件整理（2026-08-17）

- 已删除 `User/src/led_app.c` 中的正弦查表、软件 PWM 和波浪呼吸实现，避免把持续刷新和波形逻辑带入当前工程。
- 当前 LED 应用文件已迁移到 `APP/led_app.c` 和 `APP/led_app.h`；`User/` 只保留主循环、SysTick 和中断入口等基础文件。
- `APP/led_app.c` 当前只作为应用层 LED 演示/接口保留；Bootloader 专属 LED 指示模块尚未创建，也没有接入升级状态机。
- 六级累计进度、校验慢闪和失败双闪接口暂作为后续状态显示接口保留，真正用于升级必须等 M1/M6 建立独立 Bootloader 后再接入。
- OLED 百分比显示、升级包总长度和已写入字节数尚未接入当前工程，属于后续 M6 升级状态机工作。

#### M0 已闭合；后续软件实现项（不再阻塞 M0）

- 当前项目涉及的 AF 编号以及 PC0/PC1/PA4 的 ADC/DAC 通道已依据本地 `GD32F470xxDatasheet_Rev2.1.pdf` 核对;ADC/DAC 初始化和闭环代码按 M4 实现。
- DMA 请求映射已依据 GD32F4xx 用户手册查明;推荐 `USART1-RX→DMA0/CH5`、`USART0-RX→DMA1/CH5`、`ADC1→DMA1/CH2`、`SDIO→DMA1/CH6`;初始化、缓冲区链接位置和中断验证按 M3/M4 实现。
- NVIC 资源表已完成冻结;USART IDLE、DMA、EXTI、RTC、SDIO 和 SysTick 的实际中断接入按对应 M3/M4 外设实现。
- H6/H7、CN1/CN2/CN3 是已确认的硬件路由/连接器,没有独立的“连接器驱动”;M3 实现对应 USART0/USART1/USART2、PA1 RS485 方向控制和收发适配。
- 外部 Flash 型号与容量已按用户指定冻结为 `GD25Q40E`（4Mbit/512KB）;M3 完成 SPI1、CS、读 ID、原始读写/擦除驱动,M5 再实现 Flash KV/参数告警存储,M6 再决定升级包/备份镜像用途和分区。

**验收关卡:** 资源表完成且**无冲突**(DMA 通道互斥、EXTI 线号互斥、NVIC 优先级分层、内部 Flash 可擦除边界与外部 GD25Q40E 分区职责明确)。

---

## 四、M1:工程地基(Boot/App 双工程)

**目标:** 双工程 + 内部 Flash 分区链接 + 向量跳转,打通"上电 Boot → 跳 App"最小链路;GD25Q40E 先保留为后续外部存储,不参与复位启动。

**动作清单(严格按序):**

1. **建目录骨架**(《01》十七-2 目录树):
   ```
   Common/  BSP/Boards/gd32f470ve_v1/  Middleware/  Services/  Tasks/  App/  Bootloader/
   Libraries/(SPL,不动)  Driver/(CMSIS,不动)  User/(逐步废弃)
   ```
2. **先建 Common 最小子集**:`common_flash_layout.h`(内部 Flash Boot/App/Backup/Staging 地址宏 + manifest 宏);外部 GD25Q40E 元数据槽地址由 M6 在元数据状态机设计阶段冻结,冻结后再加入对应 Common 存储定义;
3. **拆出 Boot 工程**(Keil 主力):0x08000000 裸机,最小 LED + 5s 等待 + 跳转 App(跳转前按《01》十二-8 的 10 步序列:先校验 MSP 与复位向量,通过后再关中断/关 SysTick/反初始化/NVIC 清中断,最后 VTOR/MSP/跳转);
4. **改造 App 工程**(Keil + EIDE):链接到 0x08012000,`SCB->VTOR = APP_BASE`,闪灯频率与 Boot 区分(如 2Hz vs 0.5Hz,肉眼可辨谁在跑);
5. **双工具链对齐**:AC5 scatter 与 GCC `gd32f470xE_flash.ld` 都按统一分区产出,两边都能编译烧录;
6. **旧单工程废弃**:闪灯代码拆入 `bsp_gpio.c` + `board_config.h`,`main.c` 变成空壳。

**产出文件:**

```
Common/common_flash_layout.h
Bootloader/(最小裸机跳转版)
App/ + BSP/bsp_gpio.c/h + BSP/Boards/gd32f470ve_v1/board_config.h
MDK 双工程(或 Boot/App 两个 .uvprojx 与 EIDE 工程)
```

**验收关卡(照搬《01》M1):** 上电 Boot 5s 后跳 App,OLED/LED 显示切换;两个工程均可编译烧录。

**工程身份(强制,构建/烧录前必须核对):**

| Keil 工程文件 | 身份 | 链接区 | 用途 |
|---|---|---|---|
| `MDK/IndustrialEmbedded.uvprojx` | ⚠️ **LEGACY 旧基线**(TargetName/OutputName 已加 `-Legacy` 后缀) | 0x08000000 / 512K 旧单工程 | 仅存档参考;**M1 验收前禁止用于烧录** |
| `MDK/IndustrialEmbedded-Boot.uvprojx` | **M1 Boot** | 0x08000000 / 64K | Boot 构建/烧录 |
| `MDK/IndustrialEmbedded-App.uvprojx` | **M1 App** | 0x08012000 / 128K | App 构建/烧录 |

- 所有构建/烧录命令**必须显式**指定 `IndustrialEmbedded-Boot.uvprojx` 或 `IndustrialEmbedded-App.uvprojx`;
- 打开 `IndustrialEmbedded.uvprojx`(旧工程)进行编译属于**错误操作**——产物名/地址均为旧基线,烧录会覆盖 Boot 区;旧工程退役删除前统一保留存档。

**M1 板级验证记录(实测):**

- 链路实测通过:上电 Boot 0.5Hz×5s → 跳 App 2Hz,确认 VTOR 重定位、中断向量化、跳转前 `__enable_irq()`(缺失则 SysTick 中断被 PRIMASK 屏蔽,`delay_1ms` 卡死)三个知识点全部成立;
- ⚠️ **元数据擦除实测(双工具复现)**:OpenOCD(stm32f2x 驱动,把 GD32F470 按 STM32F42x/43x 识别,扇区表 4×16KB+4×64KB+3×128KB 与手册一致)与 Keil(`GD32F4xx_512KB.FLM`)烧录 App 后,扇区 4 内的 Meta A/B 特征(0x08010000~0x08011FFF)均变为全 0xFF,与整扇区擦除现象一致;当前实验确认该下载流程不能保护 Meta 所在扇区,具体擦除命令以工具日志/下载算法为准;
- 证据:特征 0x5A 写入 Meta A/B → 分别用两工具烧 App → 读回 8KB 全 0xFF(实验产物在 `D:\backup\meta_test\`);
- **结论(进入设计约束):** 内部 Meta 槽与 App 同扇区 → 工具烧 App 必丢元数据 → **内部 Meta 方案废弃,元数据外部化至 GD25Q40E 固定元数据槽**(见 M5 外部分区协调点与 M6),内部 0x08010000~0x08011FFF 退役为保留区。

**卡点:** 若采用当前候选的 64KB Bootloader 区,发布映像 `Code + RO-data + RW-data load` 目标为 ≤ 60KB;最终容量以 M1 实际构建和跳转验证为准,若后期 SDIO/FatFs/OLED 撑爆,回到 M0 重新划区,不允许砍校验功能硬塞。

#### M1 已闭合;后续软件实现项(不再阻塞 M1)

- 六步动作清单全部完成(提交历史见 `m1/boot-app` 分支,a9f02fd ~ fd470bf);
- **阶段 6 记录:** `User/` 退役——共享 `systick.c/h` + `gd32f4xx_it.h` 迁移至 `Common/`(双工程同源,git 识别为 rename);Keil App/Boot 工程文件引用与 include path、EIDE(App+legacy target)、BootEIDE 工程引用及 incList 全部同步清理,全工程 `User/` 残留引用为 0;
- **阶段 5 记录:** GCC 分区链接脚本 `gd32f470xE_boot_flash.ld`(0x08000000/64K)与 `gd32f470xE_app_flash.ld`(0x08012000/128K)已入库;STM32CubeCLT arm-none-eabi-gcc 13.3.1 命令行 + EIDE GCC 两种方式实证,向量表/入口地址与 Keil AC5 产物三方一致;当前主力走 AC5(Keil/EIDE),GCC 配置保留备用;
- **审查修复记录:** #2 MSP 最小栈顶 +8B、#5 跳转后防返回、#7 App 中断启用时机后移、#8 文档十步序与实现对齐(先校验后清理)、#12 manifest 地址公式化、#13 对齐规则措辞、#14 App include path 修正、#15 旧工程 Legacy 标记与身份表,均以分支提交落库;
- 遗留(不阻塞,M2+ 处理):① 旧单工程保留为 `IndustrialEmbedded-Legacy` 存档,禁止用于 M1 烧录;② GCC syscall 桩(`_write/_read`)留待 M4 串口重定向实现;③ `LOAD segment RWX` 警告为 GD 模板 ld 固有,忽略;④ Keil FLM 页擦未单独实测(与 OpenOCD 同为"整扇区擦除"结论覆盖)。

---

## 五、M2:公共基础组件(纯 C,PC 单测)

**目标:** 把与硬件无关的机制全部在 PC 上做扎实,这是全程性价比最高的阶段。

**组件清单:**

| 组件 | 归属目录 | PC 单测内容 | 测试向量/依据 |
|---|---|---|---|
| RingBuffer | Middleware/RingBuffer | 满/空/环绕/单生产者单消费者 | 容量 ≥ 2048B(《01》七-3) |
| CRC16 | Common | `CRC16("123456789") = 0x4B37` | 《01》十二-2 测试向量 |
| CRC32 | Common | `CRC32("123456789") = 0xCBF43926` | 《01》十二-2 测试向量 |
| CLI Shell | Middleware | 命令表分发/参数解析/非法输入 | 《01》三~五章指令集 |
| Flash KV | Middleware/FlashKV | 追加写/提交标志/双扇区 GC 流程 | 《02》4.5,PC 上 mock raw Flash |
| 协议编解码 | Middleware/Protocol | 自定义帧逐字段编解码 + Modbus RTU | 《01》七/八章 |
| ebtn 纯 C 机制（可选） | Middleware/Ebtn | 去抖/单击/长按/KEEPALIVE 事件的无硬件核心测试 | 《01》十一-2；GPIO 扫描、事件投递和 FreeRTOS 接入统一在 M3 |
| 升级序列化 | Common | firmware_header/manifest/upgrade_meta 逐字段编解码 | 《01》十二-2/10 |

**产出文件:** 各组件 `.c/.h` + PC 测试工程(`test/` 目录,不占 MCU 工程)。

**验收关卡:** 组件级测试全过;**此阶段完全不依赖板子**,可与 M1 并行推进。

#### M2 已闭合（2026-08-24）

- 必做组件全部完成：CRC16、CRC32、RingBuffer、CLI Shell、自定义协议帧、Modbus RTU、Flash KV 与升级序列化。
- Flash KV 已完成 PC raw Flash Mock 的单区追加写、提交标志、CRC 校验、双扇区轮换与 GC；GC 在有效 Key 数量超过临时收集容量时返回 `FLASH_KV_STATUS_NO_SPACE`，不会静默丢失数据。
- 升级序列化已完成 `firmware_header`、`image_manifest`、`upgrade_meta` 的固定长度逐字段编解码、CRC 与 commit marker 校验。
- PC 单元测试共 **78 项，0 失败**；测试工程位于 `test/`，不依赖 MCU 硬件。
- `ebtn` 纯 C 机制为可选项，本阶段未实现，不阻塞 M2；GPIO 扫描、事件投递和 FreeRTOS 接入归入 M3。
- 当前 Flash KV 仍是 PC Mock 实现；GD25Q40E 真实 SPI 驱动和 sector 0/1 接入归入 M3/M5 的硬件与业务集成。

---

## 六、M3:FreeRTOS APP 骨架

**目标:** 架构完整、业务为空、能稳定跑的 App。

**动作清单:**

1. 移植 FreeRTOS:启用静态分配、关闭动态分配(`configSUPPORT_STATIC_ALLOCATION=1`,`configSUPPORT_DYNAMIC_ALLOCATION=0`),不编译/不链接 `heap_4.c`;SysTick 交还给 FreeRTOS 作 tick(《01》十六-5 内存预算);
2. 按《01》十六-1 使用 `xTaskCreateStatic()` 建**七任务空壳**(Protocol/Sample/Storage/Alarm/Control/Display/Health,优先级 5/4/3/3/3/2/5,栈 256/256/512/256/256/256/128 words),每个任务显式提供 `StaticTask_t` 和 `StackType_t[]`;
3. 按《01》十六-2 使用 `xQueueCreateStatic()`、`xEventGroupCreateStatic()`、`xSemaphoreCreateMutexStatic()` 和 `xTimerCreateStatic()` 建全部队列/事件组/互斥锁/软件定时器(容量照表:8×24B 请求队列等),明确 ISR 到任务的通知路径和共享资源保护;
4. IWDG(5s)+ HealthTask 喂狗链:全部任务心跳正常才喂狗(《01》十六-4);
5. **完成 LED1~LED6 软件驱动:**复用 `BSP/board_config.h` 的 `PD8~PD13`,提供 `board_led_set()`;应用状态/累计进度显示接口暂列后续项,待告警或升级状态真正使用时实现;
6. **完成 KEY1~KEY6 软件链路:**GPIO 输入初始化、低有效读取、定时扫描/消抖、ebtn 事件转换和 FreeRTOS 队列投递;M2 若保留 ebtn 仅做纯 C 核心测试;
7. **完成 USART 与板级接口适配:**USART0(`PA9/PA10`、H7、板载 CH340/COM4)、USART1(`PA2/PA3`、H6、RS232/RS485)、USART2(`PB10/PB11`、CN1);同时实现 PA1 `485_CS` 的 RS485 收发方向控制。CN2/CN3 只对应 H6 后端的 RS485/RS232 物理接口,不新增独立连接器驱动;
8. **完成 OLED 驱动链路:**I2C0(`PB8/PB9`,AF4) 原始传输、SSD1306 初始化/刷新和 DisplayTask 单一所有权;
9. **完成 SPI Flash/GD25Q40E 驱动链路:**SPI1(`PB13~PB15`,AF5)+普通 GPIO CS(`PB12`),实现复位、JEDEC `0x9F` 读 ID、状态寄存器、页写、读和扇区擦除;M3 只做原始驱动,Flash KV 业务放 M5;
10. **完成 TF 卡底层链路:**SDIO(`PC8~PC12/PD2`,AF12)+`PE2` 卡检测、DMA/NVIC 接入、块读写和 FatFs 适配;StorageTask 负责 mount/unmount,验证 TF 单一所有权模型;
11. DisplayTask 以按键扫描和显示刷新周期运行空业务模型;M3 只验证任务/驱动/所有权,采样业务、协议业务和告警业务分别进入 M4/M5。

**产出文件:** `Tasks/*`、`App/`(composition root + 配置模型)、`FreeRTOSConfig.h`、静态任务/队列/事件组/定时器内存定义、FreeRTOS 移植层、`BSP/` 的 LED/KEY/USART/RS485/I2C/OLED/SPI Flash/SDIO 驱动、`Middleware/FatFs` 适配层。

**验收关卡(照搬《01》M3):** 连续 **24h 无看门狗复位、无任务栈溢出**;六路 LED 和六路按键事件可被任务正确处理;USART0/1/2 路由与 PA1 方向控制可收发;OLED 可初始化刷新;GD25Q40E 可读出 JEDEC ID 并完成最小原始读写/擦除;TF 卡可检测、mount、读写和卸载。

**风险点:** 静态任务栈与内核对象 RAM 预算(十六-5)、IdleTask/Timer Service Task 的静态内存回调、喂狗豁免规则(十六-3.1)、DisplayTask 不得直接改业务状态。

### M3-2-5 当前 App RAM 基线（2026-08-26）

本基线来自当前 App 构建生成的 map 文件：

`MDK/build/IndustrialEmbedded_App/IndustrialEmbedded-App.map`

| 项目 | 实测值 |
|---|---:|
| RAM 执行区基址 | `0x20000000` |
| RAM 执行区上限 | `0x30000`（192 KB，196608 B） |
| `Total RW Size`（RW Data + ZI Data） | 9328 B（约 9.11 KB） |
| RAM 剩余（按执行区上限估算） | 187280 B（约 182.89 KB） |
| RAM 使用率（按执行区上限估算） | 约 4.75% |
| `Total RO Size` | 10588 B（约 10.34 KB） |
| `Total ROM Size` | 10776 B（约 10.52 KB） |

当前静态任务和内核对象拆分如下：

| 对象/模块 | map 中的占用 |
|---|---:|
| TestTask TCB | 104 B |
| TestTask 栈（256 words） | 1024 B |
| IdleTask TCB | 104 B |
| IdleTask 栈（128 words） | 512 B |
| Timer Service Task TCB | 104 B |
| Timer Service Task 栈（256 words） | 1024 B |
| `freertos_static.o` `.bss` 合计 | 1744 B |
| `queue.o` `.bss` | 128 B |
| `tasks.o` `.bss` | 240 B |
| `timers.o` `.bss` | 216 B |
| 启动栈 `STACK` | 1024 B |

说明：`Total RW Size` 包含初始化数据和零初始化数据，也包含链接器报告的启动栈；上述任务栈水位是运行时“历史最小剩余栈”，不能替代 map 中的静态分配统计。当前 App 未纳入 `heap_1.c`、`heap_2.c`、`heap_4.c` 或 `heap_5.c`，FreeRTOS 采用静态分配策略。

**M3-2-5 状态:** 已完成当前最小调度骨架的 RAM 基线记录。后续每加入任务、队列、事件组、互斥锁、软件定时器或 DMA/FatFs 缓冲区，必须重新构建并与本基线比较 `Total RW Size`、执行区剩余空间和各对象占用。

### M3-6 SDIO/StorageTask 迁移后 RAM 基线（2026-09-02）

本次加入 StorageTask 的卡识别上下文、5 个 512 字节对齐缓冲区、DMA 任务通知状态和只读诊断接口后，Keil AC5 全量重构建生成的 App map 为：

`MDK/ListingsApp/IndustrialEmbedded-App.map`

| 项目 | 实测值 |
|---|---:|
| `Total RW Size`（RW Data + ZI Data） | 21024 B（约 20.53 KB） |
| RAM 剩余（按 192 KB 执行区估算） | 175584 B（约 171.47 KB） |
| RAM 使用率（按 192 KB 执行区估算） | 约 10.69% |
| `Total RO Size` | 28560 B（约 27.89 KB） |
| `Total ROM Size` | 28636 B（约 27.96 KB） |

该基线只覆盖当前 SDIO/DMA 烟囱测试和 StorageTask 状态机，不包含后续 FatFs 工作区、文件缓存或 M5 持久化请求队列；后续每加入这些对象都要重新比较 map 和任务栈水位。

**M3-6 SDIO/StorageTask 状态（更新于 2026-09-05）:** SDIO 卡识别、普通块读写、DMA 接口、中断事件和 FreeRTOS 任务通知已接入；`app_main.c` 不直接访问 TF 卡，BSP 保持 RTOS 无关。StorageTask 已接入 FatFs 上电挂载、卸载、重插检测与重新挂载，以及静态文件请求/结果队列，支持 WRITE（覆盖写）、READ 和 APPEND；文件操作由 StorageTask 执行，当前 `diskio.c` 使用普通块读写接口，DMA 保留为已验证的底层能力。

本阶段验证与清理记录：

- 板级证据来自“M3阶段”会话：用户确认电脑端可见 `storage_test.txt` 数据，并提供 MCU 读回 `STORAGE_OK` 的调试截图；基础文件读写闭环通过。
- 2026-09-05 使用 Keil AC5 对 `IndustrialEmbedded_App` 重构建通过：0 Error(s)、0 Warning(s)。ControlTask 临时热插拔测试清理后的构建产物为 Code=37256 B、RO-data=60532 B、RW-data=648 B、ZI-data=19712 B，日志为 `MDK/build/verify_append_cleanup_final_20260905.log`。
- ControlTask 的临时文件写入、读取、比较状态机和缓冲区已清理；串口回显、OLED 测试图案等阶段测试逻辑已清理，任务保留基础运行与诊断接口。
- 各业务任务增加栈水位读取接口，HealthTask 纳入 StorageTask 心跳及栈水位观测。当前看门狗配置为 `FWDGT_PSC_DIV256`、重载值 `4095U`，这是当前调试配置，后续正式验收时再单独确定超时参数。
- 用户已完成 `apptest.txt` 的首次创建、末尾追加和复位后保留验证；并完成 `hotplug.txt` 的“写入 → 拔卡卸载 → 重插初始化/挂载 → 读回旧内容 → APPEND”验证，PC 端文件最终为 31 字节、两行内容。APPEND、卸载、重插挂载和热插拔读写闭环已完成。
- 业务记录滚动、持久化策略和 24h 稳定性验收暂缓；24h 暂缓不作为当前 M3 代码推进的阻塞项。文件请求中的 buffer 为指针，调用方必须保持缓冲区有效且在请求完成前不修改内容。

---

## 七、M4:采集与通信

**目标:** 第一个业务里程碑——模拟量闭环 + 双串口双协议。

**动作清单:**

1. ADC/DMA 100ms 常驻采集 + 3 次均值滤波,写入共享区(采集引擎常驻,《01》十三-5);
2. DAC 输出 PA4 → 跳线 PC1 回读,打通输出-采集闭环(《02》3.4);
3. 基于 M3 已完成的 USART0 BSP 实现 M4 运行时 CLI 指令(test / rtc config / rtc now / ratio / limit / protocol / id / baud / start / stop / hide / unhide / help),CLI 解析在 ControlTask 上下文,ISR 只收数据入队;`conf` 与 `config save|read` 的文件导入和持久化归 M5(《01》四-5);
4. 基于 M3 已完成的 USART1/PA1 RS485 链路实现自定义二进制帧协议(帧格式/CRC16/应答超时/序列号,《01》七章);
5. Modbus RTU 从站(03/04/06/10 功能码,寄存器映射《01》八章),`protocol_mode` 切换;
6. **Python 回归测试脚本同步进场**:串口发帧/收帧/断言,覆盖正常帧 + 错误帧 + 异常帧。

**验收关卡:** A/B/C 类验收项通过;协议测试脚本可重复执行并全绿。

**风险点:** USART IDLE 中断不保证帧边界,半帧/多帧必须由 RingBuffer 解析器消化(《02》3.1 注意事项)。

**M4 执行进度(更新于 2026-09-11):**

- **M4-1b/M4-2/M4-3a/M4-3b 已板测通过**,打包为本地提交 `fa844b4`(M4:完成采样滤波换算与CLI采样控制闭环,18 文件 +1204/-32,未推送)。SampleTask 3 点滑动均值滤波 + 码值→mV→×变比三级换算板上验证(CH1 DAC 回读 1650mV);`FF_CODE_PAGE` 932→437 释放 58.7KB RO-data(LFN 保留,《01》六章长文件名为硬约束);`board_dma_map.h` 收全 4 条 DMA 并带编译期冲突检查(实测有效);CLI 骨架 + start/stop/hide/unhide + 周期采样行 + 超限标注 + LED3 + KEY1 启停 + KEY2/KEY3/KEY4 周期设置,HEX 编码板上解码验证正确。
- **M4-3c/M4-3d 已板测通过**,打包为本地提交 `ce93854`(M4:完成CLI两段式配置与RTC日历闭环,12 文件 +1230/-145,未推送)。3c:ratio/limit/protocol/id/baud 五个两段式命令(pending 状态机收敛在 app_cli 内部,手写定点/HEX/十进制解析器,不用 strtod),D/E 类验收达成;3d:`BSP/board_rtc.c` LSE 32.768k 非致命初始化——**LSE 起振成功,最大硬件风险解除**,跨午夜日期翻转(C-01 达成),Unix 历法硬校验(2026-09-07 00:00:00 = 0x6A9DFE80)精确命中,`test` 四项自检全 PASS。范围裁定:protocol/id/baud 回复文本不带 `, saved [OK]`(持久化归 M5);`baud` 上电默认 115200(用户裁定,见《02》3.1)。
- **M4-4a/4b/4c 已板测通过**,打包为本地提交 `31ed947`(M4:完成RS485自定义协议链路与命令分发,15 文件 +1683/-6,未推送)。4a:`Middleware/Protocol/protocol_stream.c` 流式帧解析器(搜帧头/半帧/粘包/噪声前缀/CRC错重同步/坏长度丢弃,坏帧快照供错误应答),PC Unity 86 用例全绿;4b:ProtocolTask 集成——USART1 RX→解析器→类型/地址/重复帧三道过滤→request/result 双队列→ControlTask 经 `app_protocol.c` 业务分发(与 CLI 命令表分离)→编码回发,重复帧命中缓存原样重发,板测 9/9;4c:13 条命令(重启/版本/ID/波特率/DAC/阈值/变比/TF状态/自检)、广播写静默执行(允许表)、忙碌策略框架、K-01/K-02 坏帧错误应答、0x0101 应答后系统复位,板测 27/27。主机端测试工具 `test/rs485_host.py`(拼帧/CRC16-Modbus/自动判定)随步建成。
- **M4-4d 已板测通过**,打包为本地提交 `a3683cf`(M4:完成自动上报与心跳,RS485 协议链路收官,5 文件 +320/-18,未推送)。0x0302/0x0303/0x0304 自动上报启停与间隔设置;0x0382 事件帧 12B(RTC Unix 时间戳 + CH0/CH1 大端 float,取共享区最新快照),实测 2s 间隔下 2.6s 窗口收到 3 帧;0x8888 心跳上电一次后每 30s,载荷 2B 设备 ID(A-04 达成);忙碌策略通电——上报期间仅放行 0x0303/0x03AA,其余回 0x05(H-02 达成);LED1/2/5 联动补全。板测 35/35 + 心跳全 PASS。
- **已达成验收项**:A-01/A-02/A-04、B-01、C-01、D-01/D-02、E-01/E-02、G-01、H-01/H-02/H-03、K-01/K-02/K-03;L-01/M-01 的"生效"半程达成,持久化半程归 M5。ROM 54.6KB/42.6%(上限 128KB)。
- **M4-5/M4-6 已完成**:Modbus RTU 从站已完成寄存器映射、03/04/06/10 功能码、异常响应、`protocol_mode` 分派及 request/result 队列链路;`rs485_host.py` 已扩展 Modbus 8E1 回归和异常注入。已知文档留白补白:事件帧序列号=0;上报间隔范围 1~86400s;心跳载荷=2B 设备 ID。

---

### M4-5a Modbus RTU 规则冻结（2026-09-09）

状态：用户已确认拆步方案；本节冻结设计契约，不表示 M4-5b~f 已实现或板测通过。保留上方既有进度记录。任务书依据为《01》第八章和 O-01；O-01 列出 03/06/10，本阶段额外验收 04。

#### 寄存器和业务规则

- 保持寄存器：0x0000~0001 CH0 变比、0x0002~0003 CH1 变比、0x0004~0005 CH0 阈值、0x0006~0007 CH1 阈值，均为 float32；0x0010 为设备 ID；0x0011 为波特率枚举 0~4，对应 9600/19200/38400/57600/115200。0x0008~000F 为未映射空洞，不补零。
- 输入寄存器：0x0000~0001 CH0、0x0002~0003 CH1，提供滤波并乘变比后的 float32 采样值。一次读请求取同一份配置或采样快照。允许读单个 16 位寄存器。
- float32 高字在前，每个寄存器高字节在前（AB CD）；CRC16 低字节先发。复用 common_crc16_calc，不沿用自定义协议的 CRC 发送字节序。
- 06 仅允许写 0x0010/0011；float32 必须通过 10 完整写入寄存器对，禁止半字写。半字写或跨空洞等不支持的地址组合返回 0x02。
- 10 先验证全部字段，再原子发布整批配置；任何字段失败都不得部分生效。变比 0~100、阈值 0~500，拒绝 NaN/Inf。变比同时同步采样换算，不能只更新配置模型。
- 通用请求层支持标准读数量 1~125、10 写数量 1~123；业务层按映射拒绝不存在的地址。不得用当前队列容量静默截断合法请求。
- CRC 错、时序错、截断/结构不完整、非本机地址静默丢弃。完整且 CRC 正确的本机请求：未知功能码返回 0x01，非法地址返回 0x02，非法数量/数据值返回 0x03；队列满或处理超时补充标准 0x06（设备忙）。异常响应功能码为请求功能码 | 0x80；不复用自定义协议错误码。
- 广播地址 0：仅允许合法完整的变比/阈值写入，成功或失败均不应答；广播读、广播修改 ID/波特率静默忽略，混合广播写不得部分执行。

#### 模式切换和通信参数

- 自定义协议保持 8N1；Modbus 使用 8E1，PC 工具同步。USART 的有效数据位与校验位组合须按本地 GD32 库核对后配置，确保线上为 8 个数据位加偶校验位。
- protocol_mode 经 USART0 CLI 切换；切入 Modbus 前校验 ID 为 1~247，超范围拒绝且保留原模式。Modbus 模式下其他 ID 修改入口同样限制为 1~247。
- 已接受的请求及响应发送完成后切换；清理旧模式解析器、接收残帧和重复应答缓存，等待新模式帧起始条件。请求/结果必须携带协议标识及关联信息；校验 request_id、deadline 和模式代次，拒绝迟到结果及失效请求。
- 进入 Modbus 清除自定义自动上报使能及其忙碌状态，停止 0x0382/0x8888 事件发送；ADC 常驻采集和 CLI 本地采样保持原状态。切回自定义模式后不自动恢复上报，心跳从切换完成时重新计时，30s 后发送第一帧；真实冷启动的首次心跳规则保持原约定。
- Modbus 改 ID/波特率先以旧地址、旧波特率完成应答，等待 TC 后再使新参数生效；配置模型、USART 硬件和 RTU 计时一致更新。切换/改参操作在任务间协调，避免 CLI 在 RS485 发帧中途直接改硬件。
- 全部修改仅运行时生效，持久化严格归 M5，不输出 saved [OK]。FatFs、FF_CODE_PAGE=437 和 LFN 配置不变。

#### 收帧、所有权和验收

- USART1 IDLE 只是接收空闲提示，不等于 t3.5。RTU 层接收带边界/时间的信息；低于或等于 19200 按 11 bit 字符计算 t1.5/t3.5，高于 19200 使用 750us/1750us。基于微秒计时和接收活动确认边界，不能通过 ProtocolTask 的 10ms 轮询恢复已丢失的边界。
- ISR 仅维护接收数据、时间和事件；CRC、寄存器解析和业务放任务。定时器实例在 M4-5d 检查现有资源后分配。帧内超过 t1.5 又在 t3.5 前继续接收时整帧丢弃；溢出后等完整静默窗口恢复。
- ProtocolTask 负责收帧/协议分派/发帧；ControlTask 调用独立 app_modbus 业务层。复用现有 request/result 双队列并扩展载荷容量，保证最长已映射事务（10 写 8 寄存器的数据区 21B，03 读 8 寄存器响应数据区 17B）完整承载。跨任务不传指向可复用 RX 缓冲区的裸指针；静态缓冲优先，不把大 ADU 放任务栈。
- M4-5b：补请求解析及异常编码，保留 M2 原接口/测试；M4-5c：寄存器业务；M4-5d：RTU 收帧；M4-5e：队列与模式集成；M4-5f/M4-6：扩展 rs485_host.py 与板测。
- 每步对应 PC 测试及 App 构建，通过当次 MDK/build/*.map 记录 ROM/RAM 和增量，ROM 上限 128KB；提交前复核 IndustrialEmbedded-App.code-workspace 的 cortex-debug 设置。仅用户要求时提交，不推送。
- 板测端口：COM9 连接 USART0，仅用于 CLI 模式切换；COM14 连接 USART1/RS485，用于自定义协议和 Modbus RTU 收发。冷启动可用约 5.5s；影响采样结果的写入后等待 400ms。覆盖 03/04/06/10、异常、广播、参数原子性、ID/波特率及模式往返切换，并回归自定义协议 35/35 和心跳。亚毫秒时序须用板侧计时或逻辑分析仪验证，Windows Python sleep 不作为严格时序证据。

规范参考：https://www.modbus.org/docs/Modbus_over_serial_line_V1_02.pdf

### M4-5d 接收时序实现记录（2026-09-10）

- TIMER1 维持 1MHz 自由运行。`BSP/board_rtu_timing.h` 为可在 PC 执行的私有时序核心，`board_usart.c` 负责字节、RingBuffer、帧事件和硬件适配。
- 接收方案修正：仅用 DMA 批量提交/IDLE 时间差无法还原各字节间隙。RTU 模式改用 USART1 RBNE 逐字节中断采集完成时刻，关闭 USART 的 RX DMA 请求；自定义协议保留 DMA0 CH5 + IDLE。BSP 切换接口设置 8E1（9 位字长含偶校验）/8N1；该接口须由通信所有者在 TX 完成后调用。上层模式分派仍属 M4-5e。
- 时间判定采用相邻字节完成时刻之差，扣除本字符时间后比较 t1.5/t3.5。非法间隔置整帧错误并丢弃后续字节，直到完整静默窗口结束；新模式启用时同样等待静默。字节时间向上取整至微秒，阈值附近的量化及 ISR 延迟误差须在板上测量。
- 结束确认增加一个字符保护时间，防止在下一字节尚未收完时切断接收序列；115200 下 t1.5=750us、t3.5=1750us、字符时间=96us，比较定时等待=1846us。若下一字节先到，则根据其时间戳判断上一帧边界，不依赖任务轮询。
- 帧最大 256B。时序错误返回 `BOARD_USART1_RS485_RX_FRAME_GAP_ERROR`，超长/硬件错误返回 OVERFLOW，错误帧消费后 length=0。取帧过程以短临界区保护事件与字节的一致性；旧字节接口在 RTU 启用时不消费数据。事件队列满丢弃积压并记录计数。
- 新增 `test/Makefile.rtu_timing`，覆盖纯时序和实际 BSP 接收代码（仅寄存器/时钟用 PC 替身）：t1.5/t3.5 边界、异常后缀丢弃、初始静默、计时回绕、延迟中断、队列/长度溢出、首字节校验错后恢复、模式往返、9600/19200/115200 参数和定时回调中的待处理字节。已有 98 项协议/公共组件与 14 项业务测试回归通过。
- 证据边界：上述为 PC 测试和 AC5 构建验证；尚未完成串口压力下的 IRQ 延迟测量、COM9 RTU 收发与 O-01。ProtocolTask 默认仍运行自定义协议，RTU 接收默认关闭。
- 本步 AC5 map：ROM=58668B（57.29KiB，44.8%），RAM=24536B（23.96KiB）；相对上一步 ROM +460B、RAM +40B。未调用的 RTU 启用/取帧接口仍被链接器裁剪，M4-5e 接入后需再次核算。cortex-debug.variableUseNaturalFormat=true 保留。

### M4-5e-1 队列契约适配（2026-09-11）

- request 数据区扩为 252B，result 数据区扩为 32B；增加协议类型、模式代次、设备地址及应答后应用通信参数字段。队列仍按 sizeof 创建，深度各为 8。
- 自定义请求清零后填写身份字段，业务结果回填对应身份；`mode_epoch` 已由 ProtocolTask 生成并用于 Modbus 结果匹配。自定义载荷超过 12B 时，在缓存命中检查前拒绝，不再截断执行。
- ProtocolTask 和 ControlTask 的请求/结果对象移到任务独占静态区。AC5 构建通过，ROM=58760B（57.38KiB），RAM=27448B（26.80KiB）；相对 M4-5d 增加 ROM 92B、RAM 2912B。
- 既有 PC 公共组件/协议 98 项、业务 15 项及 RTU 时序/BSP 测试通过；cortex-debug 设置保留。上述测试不等于本次队列链路已在 COM9 回归。后续补充模式切换事务和 COM9 板测。

### M4-5e-2/3 ControlTask 分发与 ProtocolTask RTU 集成（2026-09-11）

- `ControlTask` 按 `protocol_kind` 分派：自定义请求进入 `app_protocol_execute()`；Modbus 请求在任务上下文重建 `modbus_request_t`，调用 `app_modbus_execute()`，再转换为公共 `protocol_result_t` 入 result 队列。10 功能码写入数据只使用队列内副本，不跨任务传递 RX 缓冲区指针。
- `ProtocolTask` 按 `app_config.protocol_mode` 同步协议模式：自定义模式使用 USART1 8N1 + DMA0 CH5 + IDLE；Modbus 模式使用 USART1 8E1 + RBNE 逐字节接收，从 RTU 帧接口取出完整 ADU，调用 `modbus_rtu_request_decode()` 后填充 request 队列。CRC/截断/非法地址等结构错误静默丢弃，完整未知功能码和数量异常交给业务层生成标准异常响应。
- Modbus 响应经 `modbus_rtu_encode()` 或 `modbus_rtu_exception_encode()` 生成，CRC 低字节先发；广播不响应。ID/波特率修改在旧响应发送完成后应用，模式切换清空自定义解析器和重复应答缓存，并关闭自动上报/心跳。
- request/result 队列已扩容并纳入模式代次、请求地址、功能字段和通信参数待应用字段；当前模式代次用于 Modbus 结果匹配，后续仍需补充更完整的模式切换事务协调和队列清理策略。
- `app_config` 增加 Modbus 地址约束：进入 Modbus 及 Modbus 模式下修改设备 ID 均限制为 1~247。AC5 构建通过：ROM=62420B（60.96KiB，47.6%），RAM=28136B（27.48KiB，14.3%）；公共/协议 98 项、业务 15 项、RTU 时序/BSP 接收测试通过。map 已确认 `control_execute_modbus_request`、`protocol_process_modbus_frames`、`protocol_process_modbus_request` 和 `app_modbus_execute` 均被链接。COM9 模式切换、COM14 实际 Modbus 收发及 O-01 板测结果见 M4-5f/M4-6 记录；workspace 的 `cortex-debug.variableUseNaturalFormat` 保持为 `true`。

### M4-5f/M4-6 Python 回归与板级验收（2026-09-11）

- `test/rs485_host.py` 新增 Modbus RTU 主机：COM14 使用 115200 8E1，CRC16-Modbus 低字节先发，支持 03/04/06/10 响应解析、异常响应校验和静默帧检查；新增 `test/test_rs485_host.py`，主机帧单元测试 5/5 通过。
- 端口职责已按实物连接固定：COM9 为 USART0/115200 8N1，用于发送 `protocol` 和模式值；COM14 为 USART1/RS485/115200 8E1，用于 Modbus RTU 和自定义 RS485 协议。测试由 COM9 将 `protocol` 从 0 切换为 1 后开始，Modbus 测试完成后再切回 0。
- Modbus 功能性板测 `python test/rs485_host.py COM14 modbus`：14/14 通过。覆盖设备 ID、变比和输入寄存器读取，06 原值回显，10 完整 float32 写入与回读，非法变比异常 03 及整批写入原子性，空洞地址异常 02，非法 ID 异常 03，未支持功能码异常 01，错误地址/错误 CRC 静默以及广播合法写静默。实测 ID=1、变比=1.000/1.000、CH0=2.282V、CH1=1.652V。
- 自定义 RS485 回归 `python test/rs485_host.py COM14 all`：39/39 通过；新增验证 0x0106 在 115200 与 57600 间切换，确认旧波特率应答完成后再切换，并恢复到 115200。原有自动上报、上报期间忙碌策略、停止上报、异常帧、DAC/阈值/变比业务、重启恢复均通过。重启后约 5.5s 恢复通信。
- 补缺闭环板测：设备 ID=248 时 CLI 输入 `protocol=1` 返回 `parameter invalid, protocol unchanged` 且仍保持 custom；KEY2/KEY3/KEY4 分别输出并设置 5s/10s/15s；切换 custom↔Modbus 后 Modbus 回归通过。双队列 reset 的 PC 测试 1/1 通过，自定义结果已按 request_id/mode_epoch/protocol_kind 匹配。
- 随代码变更重新 AC5 构建：ROM=62768B（61.30KiB，47.9%），RAM=28136B（27.48KiB，14.3%）；`cortex-debug.variableUseNaturalFormat=true` 保持。测试结束时设备恢复为 ID=1、115200、自定义协议，采样周期最后设置为 15s；参数仍为运行时生效，持久化继续归 M5。
- 本次证据覆盖功能性串口收发、模式往返和 O-01 所需 03/06/10（另含 04）板测；未替代逻辑分析仪对亚毫秒 IRQ 延迟、t1.5/t3.5 临界边界的严格线级测量。全部配置仍为运行时生效，持久化继续归 M5。

## 八、M5:TF 卡与告警

**目标:** 基于 M3 已完成的 TF/GD25Q40E 底层驱动,实现可靠落盘 + 配置原子导入 + 告警状态机。

**动作清单:**

1. 通过 M3 的 SDIO + FatFs + StorageTask 单一所有权实现三类文件存储:sample/alarm 每文件 10 条滚动、命名规则、audit 上电次数自增(boot_00000N.log),关键记录 `f_sync()`(《01》六章);
2. `config.ini` 原子导入:读全文件 → 临时结构解析校验 → 一次性替换 + Flash 原子写,任一行失败整组不生效(《01》三-1);
3. `config save/read` 在 M3 的 GD25Q40E 原始驱动之上实现 Flash KV(GD25Q40E sector 0/1 双扇区轮换,《02》4.5 候选分区方向);
4. 告警状态机:连续 3 次超限 → ACTIVE(只触发一次)→ 滞回 0.05V → RECOVERED;LED3 / CSV / Flash 最近 10 条 / RS485 上报按模式联动(《01》九章);
5. 拔卡降级/重挂载:检测拔卡停写不停采,重插恢复;写失败重试 3 次(100/300/900ms)后降级(《01》六-4)。

**GD25Q40E 角色边界:** M5 先实现参数/告警等业务存储;升级包和备份镜像属于后续 M6 升级流程的可选外部存储,具体地址、格式和掉电策略等做到对应阶段再确定。

**M6 外部分区前置(决策更新 2026-09-13):** M5 只冻结 GD25Q40E 业务 KV 使用 sector 0/1,不在本阶段确定升级元数据地址或修改 Common 布局宏;元数据双槽建议使用 sector 2/3,由 M6 在实现双槽状态机前统一冻结地址、序列化契约和独占访问边界。M1 已实测内部 Meta 槽会被工具烧 App 整扇区擦除,因此元数据必须外部化。

**验收关卡:** P 类验收项(P-01~P-03、Q-01~Q-02)+ 十三-5 使能位联动规则。

**风险点:** APP 的 TF/GD25Q40E 参数/告警访问必须只在 StorageTask 上下文,ControlTask/AlarmTask 只发请求;M6 Bootloader 访问 GD25Q40E 仅允许发生在升级状态机的独占阶段(《01》三-4 存储分域强制项)。

**板测结论补充(2026-09-12,SDIO DMA 轮询接入):**

- 审计日志行尾修复:`storage_task_format_audit_event()` 与 `boot #N` 行的写入长度由 `position - 1` 改为 `position`。修复前每条记录只写 `CR`、缺 `LF`(`boot_000085.log`:322 字节,8 CR / 0 LF);修复后 `boot_000087.log` 实测 11 CR / 11 LF,内容与串口一致。
- 热拔在途写入:拔卡若发生在文件创建或记录写入过程中,可能留下 0 字节文件(`sample_20260101_053402.csv`);系统不死机、采样不停(拔卡后仍每 10s 输出采样行),重插后由挂载策略自动重试恢复。被打断写之后的首次挂载曾出现一次瞬态 `FatFsErr=1 / DMAerr=2 (FEE)`,自动重试后 `FatFs: Mounted [PASS]`。
- 同一轮干净卡验证:`config.ini` 导入 [OK]、采样文件 `sample_20260101_053750.csv` 6 行 186 字节(6 CR / 6 LF)、告警文件 `alarm_20260101_052714.csv` 1 行 35 字节、重插后 Q-02 重挂载 [PASS];异常未再复现。
- M5-5 最终板测闭环(2026-09-13):电脑侧确认 TF 卷为 MBR + exFAT,初始状态为 `Dirty / Full Repair Needed`,且 `audit` 目录不可读;执行 `chkdsk G: /f` 后卷恢复为 `Healthy / OK`,dirty 标志清除,`audit` 目录恢复可读,坏扇区为 0 KB。
- DMA 写启动时序修正: `board_sdio_write_block_dma_polling()` 按 `CMD24 → 数据状态机 → 配置并使能 DMA 通道 → 打开 SDIO DMA 请求` 启动,避免 SDIO 请求早于 DMA 通道就绪而触发 `FEE`。Keil AC5 重构建 `0 error / 0 warning`。
- 修正后板测:上电自动挂载 [PASS];实际告警 `f_open/f_write/f_sync` 写入后 `0x0701=01` 保持;TF 卡拔出并重新插入后 COM9 自检、FatFs 重挂载和 `0x0701=01` 均 [PASS];重插后再次告警写入通过,Flash 告警记录由 6 条增至 7 条,采样查询持续正常。
- 生产配置与 Q-01 收尾(2026-09-13):CH0/CH1 已按生产配置设为 `2.50 / 12.50`,并通过 `config save`、`config read` 及软件重启后的全字段回读;用户确认此前已完成真实断电→上电参数保持验收,本轮不重复断电。当前 CH0 采样约 4.56V,高于 2.50V 会产生真实超限告警,不再视为测试告警。
- FatFs 后端策略调整(2026-09-13):按现场稳定性要求,`Middleware/FatFs/src/diskio.c` 改回 `board_sdio_read_block()` / `board_sdio_write_block()` 的 CPU FIFO 轮询路径,不再调用 SDIO DMA 接口;DMA1/Channel6 的 BSP 底层能力保留,但不属于当前 FatFs 文件访问链路,后续 M5 文件验收以轮询路径为准。
- TF 卡满处理实现(2026-09-13):StorageTask 挂载后及业务写入前调用 FatFs `f_getfree()`,按空闲簇数低于总簇数 5% 置 `TF full`;FatFs 保持挂载但关闭 sample/alarm/audit 及通用 TF 写入,告警仍先写 GD25Q40E,ControlTask 通过 COM9 一次性提示 `TF card full`,重新挂载并恢复空间后清除满卡状态。5% 边界策略用例已加入 `test_storage_mount_policy.c`,AC5 重构建 `0 error / 0 warning`;真实满卡硬件注入待后续验收。

---

## 九、M6:Bootloader(全程最硬核)

**目标:** 在内部 Flash Bootloader/App 链路之上实现在线 IAP + TF 离线升级 + GD25Q40E 升级包/备份镜像 + **元数据(升级状态)外部化固定槽** + 掉电恢复 + 启动确认回滚,全部按《01》十二章状态机实现。内部 Meta 槽(0x08010000/0x08011000)因 M1 实测(工具烧 App 整扇区擦除)已废弃,此区域退役为保留区,元数据一律存 GD25Q40E 固定元数据双槽(Boot 升级状态机阶段经 SPI1 独占访问,SPI 原始驱动 M3 提供)。

**动作清单(严格按文档顺序):**

1. 五阶段在线升级:0x0500 ENTER_BOOT(仅 APP)/ 0x0501 BEGIN / 0x0502 DATA(先写后 ACK)/ 0x0503 END / 0x0504 INSTALL,命令职责表与状态机命令限制照《01》十二-4;
2. TF 离线升级:统一暂存流程(staging_prepare → 剥头复制 → 校验 → 生成 manifest → STAGED_VALID → 共用 INSTALL)+ 失败包 `.failed` 隔离 + 成功包 `.applied` 幂等改名(《01》十二-6/9);
3. 双槽元数据(存 GD25Q40E 固定元数据槽 A/B:72B 固定序列化、双槽轮换、commit_marker 原子提交、无有效槽或外部 SPI 不可用时按 App/Backup manifest 恢复;《01》十二-10),槽地址由 M6 在本阶段先冻结;
4. 启动确认:TRIAL_PENDING → APP 满足五条件写 CONFIRMED;IWDG/HardFault 失败计数 ≥3 回滚;crash_marker 统一消费(《01》十二-5/9);
5. OLED 升级进度(0~90% 接收 / 90~100% 校验搬运)+ LED 状态/进度指示(裸机 1ms 时基,无软件 PWM;《01》十一-4/5/6);
6. 跳转 App 10 步序列与 FWDGT 接管(《01》十二-8、十六-4)。

**产出文件:** `Bootloader/` 完整状态机、Common 的升级元数据/固件格式编解码、Python 打包工具(固件头 + 映像 + manifest 出厂四件套)。

**验收关卡:** N-01~N-10 逐条通过,重点是 N-05(备份/擦写随机断电)与 N-08(回滚/改名断电)的**随机断电测试**。

**风险点(投入最大处):**
- 安装子阶段掉电恢复矩阵(BACKUP_START→STAGED_VALID、BACKUP_VALID/APP_ERASING/APP_PROGRAMMING→回滚、APP_VALID→TRIAL_PENDING,《01》十二-4);
- 外部 GD25Q40E 元数据槽必须使用其 4KB sector 擦除/编程/读回流程;内部 Flash 若保留其他 4KB 数据页,才适用 `fmc_page_erase()`(《01》十二-11);
- 大块擦写循环中喂狗;Bootloader 长等待不能复位(《01》十六-4)。

---

## 十、M7:低功耗与总验收

**目标:** 睡眠/唤醒闭环 + 全量回归,不新增功能。

**动作清单:**

1. 睡眠静默序列 11 步:停存储请求 → 等 flush → 关文件 → 停 ADC/DMA → 等 RS485 发完释放总线 → 保存使能位 → RTC 闹钟 10s + EXTI → IWDG 窗口改 ≥15s → `vTaskSuspendAll` → 熄屏灭灯 → WFI(《01》十-1);
2. 唤醒恢复:重建时钟/外设 → 复用不重建软定时器(Stop→ChangePeriod→Start)→ `vTaskDelayUntil` 任务重置 `xLastWakeTime` → 发 0x0381 事件帧(《01》十-2);
3. 全量回归:协议异常注入(100 错误帧不死机)、TF 热插拔、升级随机断电、24h 连续运行。

**验收关卡(照搬《01》十四):** 全部功能验收项 A~Q + 稳定性三项(24h 无复位、写入中随机断电 20 次数据最多丢一条、异常输入 100 帧不死机)。

---

## 十一、执行节奏与风险

| 里程碑 | 工作量占比(估) | 最大风险 | 缓解 |
|---|---|---|---|
| M0 | 小 | 原理图缺失(唯一阻塞) | 阻塞即停,不猜引脚 |
| M1 | 小 | Boot 容量 60KB 门槛、双工具链不一致 | 每步双链编译 |
| M2 | 中 | 组件质量 | PC 单测全覆盖,可与 M1 并行 |
| M3 | 中 | 栈溢出/喂狗误判 | 24h 长测 + 栈水位检测 |
| M4/M5 | 大 | 功能密集、协议边界 | Python 回归同步写,边写边测 |
| M6 | 大 | 掉电恢复矩阵 | 随机断电按验收项逐条测 |
| M7 | 中 | 集成回归遗漏 | 只回归,不新增功能 |

---

## 附:里程碑 ↔ 文档章节 ↔ 验收编号对照

| 里程碑 | 规格章节(《01》) | 技术原理(《02》) | 验收编号(《01》十四) |
|---|---|---|---|
| M0 | 十七(架构/板级配置) | 2.x(硬件) | 资源表无冲突 |
| M1 | 十二-1/8、十七-2/6 | 5.1 | M1 验收 |
| M2 | 七(数据结构)、十二-2(CRC) | 4.3~4.7 | 组件级测试 |
| M3 | 十六(任务/队列/喂狗/内存) | 4.1、5.2 | M3 验收 |
| M4 | 二~五、七、八 | 3.1~3.4、6.1 | A/B/C |
| M5 | 三、六、九、十三-5 | 3.5~3.7、4.2/4.5 | P、Q |
| M6 | 十二全部 | 3.8、3.10、5.1、5.3 | N-01~N-10 |
| M7 | 十、十四-2 | 3.9、4.1 | 全部 + 稳定性三项 |

---

*本文档为《01_PROJECT_OVERVIEW.md》第十五章里程碑的执行展开,与《01》《02》配套使用;冲突时以《01》为准。*
