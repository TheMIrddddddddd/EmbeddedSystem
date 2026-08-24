# 工业数据采集终端 —— 开发流程(执行级展开)

> 文档版本:V1.0 | 日期:2026-08-09
> 定位:本文是《01_PROJECT_OVERVIEW.md》第十五章 M0~M7 里程碑的**执行级展开**——把每个里程碑翻译成「动作清单 → 产出文件 → 依赖 → 卡点 → 验收关卡」。
> 使用方式:开发时照表执行,验收时对照《01》第十四章(A-01~Q-02)与第十四-2(稳定性)逐条验证;技术原理见《02_TECH_STACK.md》。
> 当前工程状态:M0、M1 与 M2 已完成;当前进入 M3 准备阶段。M2 的公共组件均已在 PC 上完成验证,尚未移植 FreeRTOS,业务功能仍按 M3~M5 计划推进。

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
2. **先建 Common 最小子集**:`common_flash_layout.h`(内部 Flash Boot/App/Backup/Staging 地址宏 + manifest 宏);外部 GD25Q40E 元数据槽地址属于 M5 分区契约,冻结后再加入对应 Common 存储定义;
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
5. **完成 LED1~LED6 软件驱动与应用状态接口:**复用 `BSP/board_config.h` 的 `PD8~PD13`,提供 `board_led_set()` 和状态/累计进度显示接口;不再在 M0 扩展裸机灯效;
6. **完成 KEY1~KEY6 软件链路:**GPIO 输入初始化、低有效读取、定时扫描/消抖、ebtn 事件转换和 FreeRTOS 队列投递;M2 若保留 ebtn 仅做纯 C 核心测试;
7. **完成 USART 与板级接口适配:**USART0(`PA9/PA10`、H7、板载 CH340/COM4)、USART1(`PA2/PA3`、H6、RS232/RS485)、USART2(`PB10/PB11`、CN1);同时实现 PA1 `485_CS` 的 RS485 收发方向控制。CN2/CN3 只对应 H6 后端的 RS485/RS232 物理接口,不新增独立连接器驱动;
8. **完成 OLED 驱动链路:**I2C0(`PB8/PB9`,AF4) 原始传输、SSD1306 初始化/刷新和 DisplayTask 单一所有权;
9. **完成 SPI Flash/GD25Q40E 驱动链路:**SPI1(`PB13~PB15`,AF5)+普通 GPIO CS(`PB12`),实现复位、JEDEC `0x9F` 读 ID、状态寄存器、页写、读和扇区擦除;M3 只做原始驱动,Flash KV 业务放 M5;
10. **完成 TF 卡底层链路:**SDIO(`PC8~PC12/PD2`,AF12)+`PE2` 卡检测、DMA/NVIC 接入、块读写和 FatFs 适配;StorageTask 负责 mount/unmount,验证 TF 单一所有权模型;
11. DisplayTask 以按键扫描和显示刷新周期运行空业务模型;M3 只验证任务/驱动/所有权,采样业务、协议业务和告警业务分别进入 M4/M5。

**产出文件:** `Tasks/*`、`App/`(composition root + 配置模型)、`FreeRTOSConfig.h`、静态任务/队列/事件组/定时器内存定义、FreeRTOS 移植层、`BSP/` 的 LED/KEY/USART/RS485/I2C/OLED/SPI Flash/SDIO 驱动、`Middleware/FatFs` 适配层。

**验收关卡(照搬《01》M3):** 连续 **24h 无看门狗复位、无任务栈溢出**;六路 LED 和六路按键事件可被任务正确处理;USART0/1/2 路由与 PA1 方向控制可收发;OLED 可初始化刷新;GD25Q40E 可读出 JEDEC ID 并完成最小原始读写/擦除;TF 卡可检测、mount、读写和卸载。

**风险点:** 静态任务栈与内核对象 RAM 预算(十六-5)、IdleTask/Timer Service Task 的静态内存回调、喂狗豁免规则(十六-3.1)、DisplayTask 不得直接改业务状态。

---

## 七、M4:采集与通信

**目标:** 第一个业务里程碑——模拟量闭环 + 双串口双协议。

**动作清单:**

1. ADC/DMA 100ms 常驻采集 + 3 次均值滤波,写入共享区(采集引擎常驻,《01》十三-5);
2. DAC 输出 PA4 → 跳线 PC1 回读,打通输出-采集闭环(《02》3.4);
3. 基于 M3 已完成的 USART0 BSP 实现 CLI 全部指令(test / rtc config / rtc now / conf / ratio / limit / config save|read / protocol / id / baud / start / stop / hide / unhide / help),CLI 解析在 ControlTask 上下文,ISR 只收数据入队(《01》四-5);
4. 基于 M3 已完成的 USART1/PA1 RS485 链路实现自定义二进制帧协议(帧格式/CRC16/应答超时/序列号,《01》七章);
5. Modbus RTU 从站(03/04/06/10 功能码,寄存器映射《01》八章),`protocol_mode` 切换;
6. **Python 回归测试脚本同步进场**:串口发帧/收帧/断言,覆盖正常帧 + 错误帧 + 异常帧。

**验收关卡:** A/B/C 类验收项通过;协议测试脚本可重复执行并全绿。

**风险点:** USART IDLE 中断不保证帧边界,半帧/多帧必须由 RingBuffer 解析器消化(《02》3.1 注意事项)。

---

## 八、M5:TF 卡与告警

**目标:** 基于 M3 已完成的 TF/GD25Q40E 底层驱动,实现可靠落盘 + 配置原子导入 + 告警状态机。

**动作清单:**

1. 通过 M3 的 SDIO + FatFs + StorageTask 单一所有权实现三类文件存储:sample/alarm 每文件 10 条滚动、命名规则、audit 上电次数自增(boot_00000N.log),关键记录 `f_sync()`(《01》六章);
2. `config.ini` 原子导入:读全文件 → 临时结构解析校验 → 一次性替换 + Flash 原子写,任一行失败整组不生效(《01》三-1);
3. `config save/read` 在 M3 的 GD25Q40E 原始驱动之上实现 Flash KV(GD25Q40E sector 0/1 双扇区轮换,《02》4.5 候选分区方向);
4. 告警状态机:连续 3 次超限 → ACTIVE(只触发一次)→ 滞回 0.05V → RECOVERED;LED3 / CSV / Flash 最近 10 条 / RS485 上报按模式联动(《01》九章);
5. 拔卡降级/重挂载:检测拔卡停写不停采,重插恢复;写失败重试 3 次(100/300/900ms)后降级(《01》六-4)。

**GD25Q40E 角色边界:** M5 先实现参数/告警等业务存储;升级包和备份镜像属于后续 M6 升级流程的可选外部存储,具体地址、格式和掉电策略等做到对应阶段再确定。

**外部分区协调点(实测结论引入,强制):** M5 定稿 GD25Q40E 外部分区表时,**必须为 M6 元数据槽预留独立扇区**(建议 sector 2/3 双 4KB 槽轮换),不得把分区表全部划给业务 KV;M1 已实测内部 Meta 槽会被工具烧 App 整扇区擦除,元数据只能外部化,故该预留为硬约束,地址随 M5 分区表冻结写入 Common 宏。

**验收关卡:** P 类验收项(P-01~P-03、Q-01~Q-02)+ 十三-5 使能位联动规则。

**风险点:** APP 的 TF/GD25Q40E 参数/告警访问必须只在 StorageTask 上下文,ControlTask/AlarmTask 只发请求;M6 Bootloader 访问 GD25Q40E 仅允许发生在升级状态机的独占阶段(《01》三-4 存储分域强制项)。

---

## 九、M6:Bootloader(全程最硬核)

**目标:** 在内部 Flash Bootloader/App 链路之上实现在线 IAP + TF 离线升级 + GD25Q40E 升级包/备份镜像 + **元数据(升级状态)外部化固定槽** + 掉电恢复 + 启动确认回滚,全部按《01》十二章状态机实现。内部 Meta 槽(0x08010000/0x08011000)因 M1 实测(工具烧 App 整扇区擦除)已废弃,此区域退役为保留区,元数据一律存 GD25Q40E 固定元数据双槽(Boot 升级状态机阶段经 SPI1 独占访问,SPI 原始驱动 M3 提供)。

**动作清单(严格按文档顺序):**

1. 五阶段在线升级:0x0500 ENTER_BOOT(仅 APP)/ 0x0501 BEGIN / 0x0502 DATA(先写后 ACK)/ 0x0503 END / 0x0504 INSTALL,命令职责表与状态机命令限制照《01》十二-4;
2. TF 离线升级:统一暂存流程(staging_prepare → 剥头复制 → 校验 → 生成 manifest → STAGED_VALID → 共用 INSTALL)+ 失败包 `.failed` 隔离 + 成功包 `.applied` 幂等改名(《01》十二-6/9);
3. 双槽元数据(存 GD25Q40E 固定元数据槽 A/B:68B 固定序列化、双槽轮换、commit_marker 原子提交、无有效槽或外部 SPI 不可用时按 App/Backup manifest 恢复;《01》十二-10),槽地址随 M5 外部分区冻结;
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
