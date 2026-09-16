# GD32F470 工业数据采集终端

[English version](README_EN.md)

> 一个基于 GD32F470VET6 和 FreeRTOS 的分层工业数据采集终端，覆盖采集、处理、显示、存储、通信、告警、低功耗和固件升级。

## 项目概览

本项目面向工业现场数据采集与设备维护场景。系统不是把外设直接堆在 main 函数中，而是将硬件能力逐层封装为 BSP、公共契约、中间件、应用服务和实时任务，再由 Bootloader 和 App 两个运行映像分别承担启动升级与业务运行。

完整系统包含：

- ADC 电压采集、DMA 事件、三点均值滤波、变比换算和 DAC 回读；
- USART0 CLI、USART1 RS485 自定义协议和 Modbus RTU；
- OLED、LED、按键、RTC 和看门狗服务；
- TF/FatFs 采样、告警、审计和配置存储；
- GD25Q40E 外部 SPI NOR Flash 的业务 KV、告警记录和升级 Meta；
- Bootloader 在线 IAP、TF 离线升级、镜像校验、备份、试运行确认、回滚和升级进度显示；
- 由任务协调器组织的睡眠、唤醒、资源静默和系统恢复路径。

项目重点体现嵌入式系统中的分层抽象、接口封装、资源所有权、异步消息、故障恢复和可维护性。

## 总体软件架构

~~~mermaid
flowchart TB
    CHIP["GD32F470VET6<br/>MCU 外设与中断"]
    VENDOR["Driver / Libraries<br/>CMSIS + GD32F4xx 外设库"]
    BSP["BSP<br/>板级资源与设备适配"]
    CONTRACT["Common<br/>地址、数据格式、CRC、复位契约"]
    MIDDLEWARE["Middleware<br/>FreeRTOS / FatFs / Protocol / Modbus / FlashKV"]
    APP["App<br/>配置、协议业务、应用组合根"]
    TASKS["Tasks<br/>Protocol / Sample / Storage / Alarm / Control / Display / Health"]
    BOOT["Bootloader<br/>启动、在线/离线升级、安装、确认、回滚"]
    STORAGE["存储介质<br/>内部 Flash / GD25Q40E / TF"]
    IO["系统接口<br/>ADC / DAC / USART / RS485 / OLED / RTC"]
    HOST["PC 工具<br/>固件打包、串口交互、升级主机"]
    POWER["系统级电源管理<br/>睡眠静默、RTC 唤醒、恢复"]

    CHIP --> VENDOR --> BSP
    BSP --> IO
    BSP --> CONTRACT
    BSP --> MIDDLEWARE
    CONTRACT --> MIDDLEWARE
    MIDDLEWARE --> APP
    APP --> TASKS
    CONTRACT --> BOOT
    BSP --> BOOT
    BOOT --> STORAGE
    TASKS --> STORAGE
    TASKS --> POWER
    POWER --> BSP
    HOST <--> BOOT
    HOST <--> TASKS
~~~

### 分层关系

| 层级 | 目录 | 主要职责 | 依赖约束 |
|---|---|---|---|
| 芯片架构层 | Driver/CMSIS、Libraries | 启动文件、CMSIS、GD32F4xx 外设库、链接脚本 | 不包含项目业务 |
| 板级适配层 | BSP | 引脚、时钟、DMA、外设时序、设备读写和硬件状态 | 不决定业务状态，不访问任务队列 |
| 公共契约层 | Common | Flash 分区、manifest、CRC、升级 Meta、复位原因和序列化 | Boot/App 共用，不依赖业务任务 |
| 通用基础层 | Middleware | RTOS、FatFs、协议、环形缓冲、CLI、Flash KV、按键事件 | 提供可复用机制，不保存具体业务流程 |
| 应用业务层 | App | 应用入口、配置、协议业务、Modbus 业务、升级请求和确认 | 通过任务/服务接口使用基础能力 |
| 任务调度层 | Tasks | 采样、存储、告警、通信、控制、显示、健康监控 | 负责并发与资源所有权 |
| 启动升级层 | Bootloader | 启动分派、在线/离线升级、安装、确认、回滚 | 独立裸机映像，与 App 不并发 |
| 工程交付层 | MDK、tools、test、Docs | 构建、打包、主机交互、PC 验证和设计记录 | 不进入运行时业务链路 |

依赖方向是从硬件到抽象、从机制到业务：

~~~text
MCU / Vendor Library
        ↓
BSP Hardware Adapter
        ↓
Common Contract + Middleware
        ↓
App Services / Tasks
        ↓
System Behaviors
~~~

上层不依赖底层实现细节。业务代码不直接操作寄存器，协议代码不直接擦写 Flash，ISR 不执行耗时业务，存储和显示通过明确的任务所有权访问。

## 文件代码架构与抽象解耦

### Driver 与 Libraries：芯片能力

- Driver/CMSIS 保存 Cortex-M4 内核头文件、GD32 系统初始化、启动文件和 GCC/Keil 链接脚本；
- Libraries/Include 和 Libraries/Source 保存 GD32F4xx 厂商外设库；
- 这一层只解决“芯片怎么启动、外设寄存器怎么使用”，不包含设备协议、配置语义或业务状态。

### BSP：板级差异的唯一入口

BSP 使用 board_ 前缀文件对具体硬件进行封装，例如：

- board_gpio、board_key：LED 和按键；
- board_adc、board_dac：ADC/DAC 初始化、采样和回读；
- board_usart：USART、DMA、IDLE/RBNE、RS485 方向控制；
- board_i2c、board_oled：I2C0 和 SSD1306；
- board_spi_flash：GD25Q40E 原始读写和扇区擦除；
- board_sdio：TF 卡块读写、DMA 事件和卡检测；
- board_rtc、board_timebase：RTC 和时间基准；
- board_internal_flash：内部 Flash 页操作。

上层调用 board_xxx 提供的功能接口，不需要重复了解 GPIO 复用、DMA 通道、RS485 收发方向或 OLED 命令时序。更换同系列板卡时，优先修改 BSP 和 board_config，而不是扩散修改业务任务。

### Common：Boot/App 之间的稳定契约

Common 将两个运行映像必须一致理解的内容集中管理：

- common_flash_layout.h：Boot、App、Backup、Staging 和 manifest 地址；
- upgrade_serialization.h：固件头、manifest、Meta、状态和安装子阶段；
- common_crc：CRC16/CRC32；
- common_reset_contract：软件复位、看门狗复位和异常复位原因；
- common_crash_marker：HardFault/IWDG 相关崩溃标记。

Common 不拥有业务任务，也不负责具体的 UART、TF 或 Flash 设备操作。它只定义跨模块、跨映像的数据和边界，避免 Bootloader 与 App 各自复制一套格式。

### Middleware：可替换的通用机制

- Middleware/FreeRTOS：调度、队列、事件组、互斥锁、软件定时器和静态内存机制；
- Middleware/Protocol：自定义帧、流式解析、CRC 错误重同步；
- Middleware/Protocol 中的 Modbus RTU：03/04/06/10 编解码和标准异常响应；
- Middleware/FatFs：文件系统核心；
- Middleware/FlashKV：固定记录、CRC、双扇区轮换和 GC；
- Middleware/Ringbuffer：串口接收和流式数据缓冲；
- Middleware/CLI：命令行解析；
- Middleware/Ebtn：按键事件机制。

这些模块提供机制，不决定“什么时候采样、什么条件触发告警、哪个文件保存什么业务含义”。业务语义由 App/Tasks 组合。

### App 与 Tasks：业务和并发分离

App 负责应用组合根、配置模型、协议业务、Modbus 业务以及升级请求/确认；Tasks 负责并发调度和资源所有权。这样可以把“业务规则”和“任务生命周期”分开：

- SampleTask 只负责采样、滤波、换算和发布快照；
- AlarmTask 消费采样结果并生成告警事件；
- StorageTask 执行实际文件/KV 持久化；
- ProtocolTask 负责收发帧、协议分派和响应；
- ControlTask 负责异步业务协调；
- DisplayTask 负责 OLED 和按键；
- HealthTask 负责健康状态和看门狗决策。

任务之间使用静态队列、事件组、互斥锁、任务通知和按值传递的请求对象，不通过跨任务函数回调或共享裸指针耦合业务。

### Bootloader：独立的状态机边界

Bootloader 复用 Common 的分区/序列化契约和 BSP 的硬件接口，但拥有自己的 main、升级协议、Meta、安装和回滚状态机。App 只负责提出升级请求和完成新固件健康确认，不能直接擦写自己的运行区。

这种设计把“正常业务运行”和“固件自更新”隔离为两个明确的运行上下文，避免 App 在自擦写过程中继续执行复杂任务。

## FreeRTOS 运行时模型

App 使用静态 FreeRTOS 资源，关闭动态内存分配，不链接 heap_4。任务的优先级和职责如下：

| 任务 | 主要职责 | 资源所有权 |
|---|---|---|
| ProtocolTask | 串口接收、帧解析、协议分派、响应发送 | USART1 收发和协议队列 |
| SampleTask | ADC/DMA 采样、滤波、变比换算 | ADC/DAC 和采样快照 |
| StorageTask | TF/FatFs、配置、采样、告警、审计、Flash KV | App 侧 TF/FatFs/SPI Flash |
| AlarmTask | 阈值、连续超限、滞回、ACTIVE/RECOVERED | 告警状态和告警请求 |
| ControlTask | CLI/按键/协议命令、异步请求和配置应用 | 系统协调和 pending 请求 |
| DisplayTask | OLED 刷新、按键扫描和显示服务 | App 侧 I2C/OLED |
| HealthTask | 心跳、busy/progress/deadline、看门狗 | 健康状态和 IWDG |

典型数据流：

~~~text
ADC/DMA
   ↓
SampleTask
   ├──→ DisplayTask：最新采样显示
   └──→ AlarmTask：阈值判断
                 └──→ StorageTask：告警持久化

USART / RS485
   ↓
ISR + RingBuffer
   ↓
ProtocolTask
   ↓ request/result queues
ControlTask
   ├──→ 配置与协议业务
   ├──→ SampleTask：采样控制
   └──→ StorageTask：持久化请求

HealthTask
   └──→ 看门狗、系统健康和低功耗协调
~~~

关键约束：

- ISR 只收数据、记录时间、设置事件或通知任务；
- ProtocolTask 不直接修改业务配置或写存储；
- ControlTask 和 AlarmTask 不直接调用 FatFs 或 Flash KV GC；
- StorageTask 是 App 侧 FatFs 和持久化 IO 的唯一执行上下文；
- DisplayTask 是 App 侧 OLED/I2C 的唯一所有者；
- 所有跨任务请求带有 request_id、来源、协议序列号和 deadline；
- 长 IO 使用 busy/progress/deadline 参与 HealthTask 健康判断。

## 硬件抽象与接口

| 硬件功能 | MCU 资源 | 软件接口路径 |
|---|---|---|
| 系统时钟 | 25 MHz HXTAL → 240 MHz | Driver/CMSIS → BSP 时钟初始化 |
| RTC | 32.768 kHz LSE | board_rtc → 时间服务 |
| ADC CH0/CH1 | PC0 / PC1 | board_adc → SampleTask |
| DAC | PA4 | board_dac，PA4 经跳线回读 PC1 |
| 调试 CLI | USART0 PA9/PA10 | board_usart → CLI → ControlTask |
| RS485/RS232 | USART1 PA2/PA3，PA1 方向控制 | board_usart → ProtocolTask |
| 外部串口 | USART2 PB10/PB11 | board_usart |
| OLED | I2C0 PB8/PB9 | board_i2c/board_oled → DisplayTask |
| SPI NOR | SPI1 PB13～PB15，CS=PB12 | board_spi_flash → FlashKV/Bootloader |
| TF 卡 | SDIO PC8～PC12、PD2，检测=PE2 | board_sdio → FatFs → StorageTask |
| LED/按键 | LED PD8～PD13；KEY PE15、PE13、PE11、PE9、PE7、PB0 | board_gpio/board_key |

## 存储架构与 Flash 分区

### 内部 Flash

GD32F470 内部 Flash 容量为 512 KiB，地址范围是 0x08000000～0x0807FFFF。

| 区域 | 起始地址 | 结束地址 | 大小 | 作用 |
|---|---:|---:|---:|---|
| Bootloader | 0x08000000 | 0x0800FFFF | 64 KiB | 启动与升级 |
| Legacy Meta 保留区 | 0x08010000 | 0x08011FFF | 8 KiB | 旧布局占位 |
| App | 0x08012000 | 0x08031FFF | 128 KiB | FreeRTOS 应用 |
| Backup | 0x08032000 | 0x08051FFF | 128 KiB | 旧 App 备份和回滚 |
| Staging | 0x08052000 | 0x08071FFF | 128 KiB | 新固件暂存 |
| 尾部保留区 | 0x08072000 | 0x0807FFFF | 56 KiB | 保留 |

App、Backup、Staging 各自在分区末尾保留 64 字节 manifest 区域，App manifest 地址为 0x08031FC0。升级状态 Meta 放在外部 Flash，避免 App 烧录时擦除内部 Meta 所在扇区。

### GD25Q40E

GD25Q40E 容量为 4 Mbit，即 512 KiB，扇区大小为 4 KiB。

| 区域 | Offset | 所有者 | 作用 |
|---|---:|---|---|
| Business KV slot A/B | sector 0/1 | StorageTask | 配置、参数和业务持久化 |
| Upgrade Meta slot A/B | sector 2/3 | Bootloader 升级状态机 | active/pending/backup、状态、版本、CRC、来源 |
| Diagnostic sector | 0x7F000 | 诊断工具 | 诊断数据 |

Meta 采用 72 字节固定序列化格式，通过 CRC、generation 和最后写入的 commit marker 实现双槽轮换与原子选择。

## 通信架构

### USART0 CLI

USART0 是本地配置和诊断入口。ISR 只接收并入队，CLI 在 ControlTask 中解析和执行。功能包括设备 ID、波特率、采样周期、变比、阈值、协议模式、RTC、采样启停、显示状态、配置读写和 TF 状态。

### 自定义 RS485 协议

USART1 使用 PA1 控制 RS485 收发方向。协议层封装了帧头搜索、半帧、粘包、噪声前缀、CRC 错误重同步、非法长度丢弃、重复帧缓存和异步请求/结果匹配。

### Modbus RTU

Modbus 使用 USART1 的 8E1，支持 03、04、06、10 功能码、标准异常响应、CRC16-Modbus、广播写静默以及自定义协议/Modbus 模式切换。请求由 ProtocolTask 接收、由 ControlTask 执行业务，存储仍由 StorageTask 所有。

## Bootloader 升级架构

### 在线升级

~~~text
App
  → 0x0500 ENTER_BOOT
  → 持久化升级请求并应答 READY
  → 复位进入 Bootloader
  → 0x0501 BEGIN
  → 0x0502 DATA：写入 Staging、读回校验、ACK
  → 0x0503 END：校验长度、CRC、manifest、向量表
  → 0x0504 INSTALL：Backup → 擦除 App → 编程 App → 整体校验
  → TRIAL_PENDING
  → App 健康确认 CONFIRMED
  → active=pending 原子提交
~~~

0x0505 ABORT 用于清理在线升级上下文。破坏性操作前先持久化状态和安装子阶段，长等待及分块擦写过程中持续喂 FWDGT。

### TF 离线升级

~~~text
firmware/app.bin
  → 暂存区准备
  → 剥头复制与读回
  → 长度/CRC/向量表校验
  → 生成 manifest
  → STAGED_VALID
  → 共用 INSTALL
  → TRIAL_PENDING / CONFIRMED
  → app.bin → .applied
~~~

失败包使用 .failed 隔离。离线清理、Meta 状态和镜像状态共同决定下一次启动是否允许继续升级。

### OLED 升级指示

Bootloader 侧独立封装 SSD1306 指示模块：

- 暂存和 BEGIN：0%；
- DATA 写入并读回：0%～90%；
- END 校验和 Meta 提交：约 90%～95%；
- INSTALL 搬运、擦除和校验：96%～100%；
- 跳转 App 前清屏并关闭 OLED；
- OLED 初始化/刷新异常时降级到 LED，不阻断升级状态机。

## 低功耗与系统级闭环

低功耗属于完整系统架构的一部分，核心不是增加一个孤立的 sleep 函数，而是让所有任务在关闭硬件前完成各自的事务。

~~~mermaid
flowchart TD
    CMD["RS485 0x03AA"]
    P["ProtocolTask<br/>收帧与协议解析"]
    C["ControlTask<br/>生成睡眠请求"]
    PM["PowerManager<br/>系统协调器"]
    S["StorageTask<br/>停止新请求、flush、关闭文件"]
    A["SampleTask<br/>停止 ADC/DMA"]
    R["ProtocolTask / RS485<br/>等待发送完成"]
    H["HealthTask<br/>健康检查、睡眠就绪、IWDG 窗口"]
    HW["BSP RTC / EXTI / PMU<br/>配置唤醒源"]
    WFI["WFI 深度睡眠"]
    RESTORE["恢复时钟、外设、任务、定时器"]
    TICK["重置 vTaskDelayUntil 时间基准"]
    EVENT["0x0381 唤醒事件"]

    CMD --> P --> C --> PM
    PM --> S
    PM --> A
    PM --> R
    PM --> H
    PM --> HW --> WFI
    WFI --> RESTORE --> TICK --> EVENT
    EVENT --> P
~~~

低功耗和其他模块的结合关系是：

- ProtocolTask 识别 0x03AA，ControlTask 负责异步请求生命周期；
- PowerManager 负责协调各任务，而不是让 ControlTask 直接关闭外设；
- StorageTask 先停止新的持久化请求，完成当前写入和 FatFs flush，再关闭文件；
- SampleTask 停止 ADC/DMA，避免睡眠期间外设继续产生事件；
- ProtocolTask 等待 RS485 发送完成，避免总线半帧；
- HealthTask 检查任务健康状态，设置覆盖睡眠和唤醒初始化的 IWDG 窗口；
- BSP 的 RTC/EXTI/PMU 接口负责定时唤醒、按键唤醒和 WFI；
- 唤醒后恢复时钟、外设、调度器、软件定时器和任务周期基准；
- 系统恢复后由 ProtocolTask 发送 0x0381，通知主机设备已经唤醒。

## 构建、打包与烧录

正式构建链使用 Keil AC5。EIDE 配置保留用于工程开发，但 Boot 的正式发布构建以 Keil 工程为准。

PowerShell 示例：

~~~powershell
$UV4 = "D:\keil5\UV4\UV4.exe"
& $UV4 -b "MDK\IndustrialEmbedded-Boot.uvprojx"
& $UV4 -b "MDK\IndustrialEmbedded-App.uvprojx"
~~~

主要产物：

| 产物 | 路径 | 烧录地址 |
|---|---|---:|
| Bootloader BIN | MDK/ObjectsBoot/IndustrialEmbedded-Boot.bin | 0x08000000 |
| App BIN | MDK/ObjectsApp/IndustrialEmbedded-App.bin | 0x08012000 |
| App manifest HEX | MDK/ObjectsApp/IndustrialEmbedded-App-with-manifest.hex | 使用 HEX 地址记录 |
| TF 离线包 | MDK/ObjectsApp/IndustrialEmbedded-App-vXX-offline.bin | TF firmware 目录 |

交付工具：

- tools/pack_app_manifest.py：为 App 映像生成 manifest；
- tools/pack_offline_firmware.py：生成 TF 离线升级包；
- tools/m6_upgrade_host.py：通过串口执行在线升级。

## 工程结构摘要

~~~text
Driver/CMSIS/       Cortex-M4、GD32 系统启动与链接脚本
Libraries/          GD32F4xx 厂商外设库
BSP/                板级硬件适配与设备驱动
Common/             跨 Boot/App 的地址、CRC、序列化和复位契约
Middleware/         FreeRTOS、FatFs、协议、Modbus、FlashKV、CLI、RingBuffer
Tasks/              七个 FreeRTOS 应用任务
App/                应用入口、配置、协议业务、升级请求/确认
Bootloader/         启动分派、在线/离线升级、Meta、安装、试运行、回滚
MDK/                Keil 工程、链接配置和构建输出
test/               C/Unity/Python 主机和组件测试
tools/              固件打包和在线升级工具
Docs/               项目总览、分区、流程和验收记录
~~~

## 设计特点

- **分层抽象**：硬件、板级适配、公共契约、中间件、业务和任务各自有明确边界；
- **接口封装**：上层通过 board_xxx、服务接口和队列使用硬件，不扩散寄存器细节；
- **资源所有权**：StorageTask 独占 FatFs/持久化，DisplayTask 独占 OLED，ProtocolTask 独占通信收发；
- **异步解耦**：请求带 request_id、来源、序列号和 deadline，结果按关联信息匹配；
- **故障安全**：CRC、Meta 双槽、manifest、Backup、Staging、试运行确认、回滚和看门狗共同组成恢复链；
- **运行映像隔离**：Bootloader 和 App 使用不同链接区域及不同运行模型；
- **可维护交付**：构建、打包、主机升级、设计契约和验收记录分别位于独立目录。

## 相关文档

- [项目总览](Docs/01_PROJECT_OVERVIEW.md)
- [开发流程与设计记录](Docs/03_DEV_PROCESS.md)
- [M6 分区冻结契约](Docs/M6_0A_PARTITION_FREEZE.md)
- [M5 配置文件契约草案](Docs/M5_CONFIG_INI_CONTRACT.md)
- [English README](README_EN.md)
