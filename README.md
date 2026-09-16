# GD32F470 工业数据采集终端

[English version](README_EN.md)

> 基于 GD32F470VET6、FreeRTOS、RS485、Modbus RTU、TF/FatFs 和可恢复 Bootloader 升级链路构建的工业数据采集终端。

## 项目概览

本项目把一块工业控制板组织成一个具有清晰边界的嵌入式系统，而不是把多个外设 Demo 直接堆在 main 函数中。系统从底层硬件开始，经过 BSP、公共协议/数据契约、中间件、应用服务和 FreeRTOS 任务，最终形成采集、计算、显示、存储、通信、告警和固件升级闭环。

系统支持：

- ADC 电压采集、三点均值滤波、变比换算和 DAC 回读；
- USART0 CLI、USART1 RS485 自定义协议和 Modbus RTU；
- OLED、LED、按键和 RTC 服务；
- TF 卡/FatFs 采样、告警、审计和配置存储；
- GD25Q40E 外部 SPI NOR Flash 参数、告警和升级 Meta 存储；
- Bootloader 在线 IAP、TF 离线升级、镜像校验、启动确认、回滚和 OLED 升级进度；
- 面向后续低功耗的任务协作和资源所有权设计。

项目的重点是分层抽象、模块封装、任务间异步协作、存储可靠性和升级恢复能力。

## 当前项目边界

当前仓库已经完成 M0～M6 选定范围的主要功能，并已进入最终交付阶段。由于实习求职安排，后续新增开发和测试暂停，M7 低功耗睡眠/唤醒及总验收保留为未来扩展。

当前版本可以诚实地描述为：

> 已完成工业数据采集终端的运行时业务、通信、存储和 Bootloader 升级闭环；低功耗管理、长期稳定性和部分随机掉电矩阵不属于当前版本的完成证据。

M6 已完成的选定范围包括在线/离线升级、GD25Q40E 双槽 Meta、App 试运行确认、失败回滚、Boot→App 跳转、FWDGT 安全等待、错误返回和 OLED 升级指示。N-05、N-07、N-08 随机掉电/改名掉电项按项目决定未执行，不能写成全部 N 类验收通过。

## 系统分层架构

~~~mermaid
flowchart TB
    HW["GD32F470VET6<br/>GPIO / ADC / DAC / USART / SPI / SDIO / I2C / RTC"]
    VENDOR["Driver + Libraries<br/>CMSIS + GD32F4xx 外设库"]
    BSP["BSP<br/>板级引脚、时钟、DMA、外设适配"]
    COMMON["Common<br/>Flash 布局、CRC、升级序列化、复位契约"]
    MW["Middleware<br/>FreeRTOS / FatFs / Protocol / Modbus / FlashKV / RingBuffer / CLI"]
    SERVICES["App services<br/>配置、采样、告警、显示、持久化、协议业务"]
    TASKS["FreeRTOS tasks<br/>Protocol / Sample / Storage / Alarm / Control / Display / Health"]
    BOOT["Bootloader<br/>启动分派、在线/离线升级、Meta、安装、试运行、回滚"]
    HOST["PC host tools<br/>打包、RS485/Modbus 回归、在线升级主机"]

    HW --> VENDOR --> BSP
    BSP --> COMMON
    BSP --> MW
    COMMON --> MW
    MW --> SERVICES --> TASKS
    COMMON --> BOOT
    BSP --> BOOT
    BOOT <--> HOST
    BOOT --> TASKS
~~~

依赖方向遵循由下到上的单向抽象：

1. **硬件与厂商库层**只提供 MCU 外设寄存器、启动文件和芯片通用能力。
2. **BSP 层**把具体引脚、时钟、DMA、收发方向和设备时序封装成板级接口。
3. **Common 层**定义 Boot/App 共同理解的数据格式、地址边界、CRC、Meta 和复位契约。
4. **Middleware 层**提供与具体业务无关的 RTOS、文件系统、协议解析、环形缓冲和 Flash KV 能力。
5. **App service 与 Tasks 层**负责业务语义，通过任务、队列和服务接口使用底层能力。
6. **Bootloader 层**是独立的裸机运行映像，复用 Common/BSP 的契约和驱动，但不与 App 并发运行。
7. **tools/test/Docs**负责构建、打包、主机交互、PC 侧验证和设计记录。

## 文件与目录架构

| 目录 | 层级定位 | 封装内容 |
|---|---|---|
| Driver/CMSIS | 芯片启动与架构层 | Cortex-M4、GD32 系统初始化、启动文件、链接脚本 |
| Libraries | 厂商外设库 | GD32F4xx 的 GPIO、DMA、ADC、DAC、USART、SPI、SDIO、I2C、RTC、FMC 等 |
| BSP | 板级适配层 | board_config、GPIO、按键、LED、ADC、DAC、USART/RS485、RTC、OLED、SPI Flash、SDIO、内部 Flash |
| Common | 跨映像公共契约 | common_flash_layout、CRC、升级序列化、复位原因、崩溃标记 |
| Middleware/FreeRTOS | RTOS 内核 | 静态任务、队列、事件组、互斥锁、软件定时器和调度机制 |
| Middleware/FatFs | 文件系统层 | FatFs 核心和 TF 块设备适配 |
| Middleware/Protocol | 协议基础层 | 自定义帧、流式解析、Modbus RTU 编解码 |
| Middleware/FlashKV | 持久化基础层 | 外部 Flash KV 记录、CRC、双扇区轮换和 GC |
| Middleware/Ringbuffer、CLI、Ebtn | 通用组件层 | 环形缓冲、命令解析和按键事件机制 |
| Tasks | 应用任务层 | 采样、存储、告警、协议、控制、显示和健康任务 |
| App | 应用组合根与业务层 | FreeRTOS 配置、应用入口、CLI 业务、协议业务、配置和升级确认 |
| Bootloader | 启动与升级层 | 请求、暂存、DATA/END/INSTALL、离线升级、Meta、试运行、回滚、跳转和升级指示 |
| MDK | 工程与构建层 | Keil AC5 工程、链接配置、工作区和构建输出 |
| test | 主机和组件验证层 | Unity/C 测试、Python 主机测试、协议/存储/升级序列化测试 |
| tools | 交付工具层 | App manifest、TF 离线包和在线升级主机工具 |
| Docs | 设计和过程层 | 总览、分区契约、开发流程、配置契约和验收记录 |
| .cursor/rules | AI 协作层 | 完整项目阅读、架构解释和证据边界约定 |

目录之间的职责边界是项目设计的一部分。业务任务不直接操作寄存器，ISR 不执行协议解析或持久化，ControlTask 不绕过 StorageTask 直接写 TF/Flash，Bootloader 也不与 App 同时访问升级资源。

## 运行时任务与封装边界

App 采用静态 FreeRTOS 资源，不启用动态内存分配，不链接 heap_4。任务之间使用静态队列、事件组、互斥锁、软件定时器和任务通知传递请求及完成结果。

| 任务 | 输入 | 输出/调用 | 责任边界 |
|---|---|---|---|
| ProtocolTask | USART1 接收事件、RingBuffer 数据 | 请求队列、响应帧、事件帧 | 负责收帧、协议分派、编码和发送，不执行持久化业务 |
| SampleTask | ADC/DMA 完成事件、采样配置 | 最新采样快照、采样结果队列 | 负责采集、滤波和工程量换算 |
| StorageTask | 配置/采样/告警/审计请求 | 文件和 KV 完成结果 | App 侧唯一执行 FatFs、TF 和外部 Flash 持久化的任务 |
| AlarmTask | 采样结果 | 告警事件、持久化请求 | 负责阈值、连续次数、滞回和 ACTIVE/RECOVERED 状态机 |
| ControlTask | CLI、按键、协议请求和完成消息 | 业务调用、配置应用、协议结果 | 系统协调器，维护异步请求生命周期 |
| DisplayTask | 显示状态、按键扫描周期 | OLED 刷新、按键事件 | App 侧唯一 OLED/I2C 显示所有者 |
| HealthTask | 各任务心跳、busy/progress/deadline | 看门狗决策、健康状态、睡眠就绪 | 负责健康契约和 IWDG 喂狗条件 |

### 典型数据流

~~~text
ADC/DMA ──→ SampleTask ──→ 最新值快照 ──→ DisplayTask
                         └──→ AlarmTask ──→ 告警持久化请求

USART/RS485 ──→ ISR/RingBuffer ──→ ProtocolTask
                                      └──→ 请求队列 ──→ ControlTask
                                                         ├──→ SampleTask
                                                         ├──→ StorageTask
                                                         └──→ 协议结果队列 ──→ ProtocolTask

ControlTask / AlarmTask ──持久化请求──→ StorageTask
                                       ├──→ FatFs / TF
                                       └──→ FlashKV / GD25Q40E

HealthTask ──健康状态、busy、deadline──→ 看门狗和系统协调
~~~

### 关键所有权规则

- ISR 只接收字节、记录时间、设置事件或通知任务，不解析协议、不打印、不擦写 Flash；
- ProtocolTask 只负责通信帧和协议编码，ControlTask 执行具体业务；
- StorageTask 独占 App 侧 FatFs 和 GD25Q40E 业务持久化；
- AlarmTask 只生成告警事件和持久化请求，不直接写存储；
- DisplayTask 独占 App 侧 OLED/I2C；
- 跨任务消息携带 request_id、来源、协议序列号、deadline 和按值复制的 payload，不传递可复用接收缓冲区的裸指针；
- Bootloader 在启动/升级阶段独占升级所需资源，完成后关闭自身显示并跳转 App。

## 硬件抽象与接口映射

| 硬件功能 | MCU 资源 | 软件抽象 |
|---|---|---|
| 系统时钟 | 25 MHz HXTAL → 240 MHz | CMSIS/GD32 system init |
| RTC | 32.768 kHz LSE | BSP RTC 原始读写和时间服务 |
| ADC CH0/CH1 | PC0 / PC1 | board_adc → SampleTask |
| DAC | PA4 | board_dac，PA4 经跳线回读 PC1 |
| 调试 CLI | USART0 PA9/PA10 | board_usart → CLI → ControlTask |
| RS485/RS232 | USART1 PA2/PA3，PA1 方向控制 | board_usart → ProtocolTask |
| 外部串口 | USART2 PB10/PB11 | board_usart |
| OLED | I2C0 PB8/PB9 | board_i2c/board_oled → DisplayTask |
| SPI NOR | SPI1 PB13～PB15，CS=PB12 | board_spi_flash → FlashKV/Bootloader Meta |
| TF 卡 | SDIO PC8～PC12、PD2，检测=PE2 | board_sdio → FatFs → StorageTask |
| LED/按键 | LED PD8～PD13；KEY PE15、PE13、PE11、PE9、PE7、PB0 | board_gpio/board_key → DisplayTask/业务状态 |

BSP 隔离了板级细节，因此上层只依赖功能接口，不需要知道 GPIO 初始化寄存器、DMA 通道、RS485 收发方向或 SSD1306 命令时序。

## 存储与 Flash 分区

### 内部 Flash

GD32F470 内部 Flash 为 512 KiB，地址范围是 0x08000000～0x0807FFFF。

| 区域 | 起始地址 | 结束地址 | 大小 | 作用 |
|---|---:|---:|---:|---|
| Bootloader | 0x08000000 | 0x0800FFFF | 64 KiB | 启动与升级 |
| Legacy Meta 保留区 | 0x08010000 | 0x08011FFF | 8 KiB | 已退役，仅保留布局占位 |
| App | 0x08012000 | 0x08031FFF | 128 KiB | FreeRTOS 应用 |
| Backup | 0x08032000 | 0x08051FFF | 128 KiB | 旧 App 备份与回滚 |
| Staging | 0x08052000 | 0x08071FFF | 128 KiB | 新固件暂存 |
| 尾部保留区 | 0x08072000 | 0x0807FFFF | 56 KiB | 当前不占用 |

App、Backup、Staging 各自保留最后 64 字节存放 manifest。App manifest 地址为 0x08031FC0。旧内部 Meta 方案被废弃，是因为烧录 App 时可能擦除其所在扇区。

### GD25Q40E 外部 Flash

GD25Q40E 容量为 4 Mbit，即 512 KiB，扇区大小为 4 KiB。

| 区域 | Offset | 使用者 | 作用 |
|---|---:|---|---|
| Business KV slot A/B | sector 0/1 | StorageTask | 配置和业务参数 |
| Upgrade Meta slot A/B | sector 2/3 | Bootloader 升级状态机 | active/pending/backup、状态、版本、CRC、来源 |
| Diagnostic sector | 0x7F000 | 诊断工具 | 诊断数据 |

升级 Meta 采用 72 字节固定序列化格式，包含 CRC、generation 和最后写入的 commit marker。双槽轮换保证单槽写入失败时仍保留另一份有效记录。

## 通信架构

### USART0 CLI

USART0 是本地配置和诊断入口。ISR 只接收并入队，CLI 解析和执行在 ControlTask 中完成。CLI 覆盖设备 ID、波特率、采样周期、变比、阈值、协议模式、RTC、采样启停、显示状态、配置读写和 TF 状态。

### 自定义 RS485 协议

USART1 通过 PA1 控制 RS485 收发方向。协议层采用流式解析器，处理帧头搜索、半帧、粘包、噪声前缀、CRC 错误重同步、坏长度丢弃、重复帧缓存和异步请求/结果匹配。

### Modbus RTU

Modbus 使用 USART1 的 8E1，支持 03、04、06、10 功能码、标准异常响应、CRC16-Modbus、广播写静默以及自定义协议/Modbus 模式切换。Modbus 请求经过 ProtocolTask、请求队列和 ControlTask，不能绕过应用任务边界。

## Bootloader 升级架构

Bootloader 是独立裸机映像，使用 Common 中的地址和序列化契约，使用 BSP 中的内部 Flash、SPI NOR、SDIO、USART 和 OLED 接口。

### 在线升级

~~~text
App 运行
  → 0x0500 ENTER_BOOT
  → App 持久化请求并应答 READY
  → 软件复位进入 Bootloader
  → 0x0501 BEGIN
  → 0x0502 DATA：写入 Staging、读回校验、再 ACK
  → 0x0503 END：校验长度、CRC、manifest、向量表
  → 0x0504 INSTALL：Backup → 擦除 App → 编程 App → 整体校验
  → TRIAL_PENDING
  → App 健康确认 CONFIRMED
  → active=pending 原子提交
  → 新 App 正常运行
~~~

0x0505 ABORT 用于原子清除在线升级上下文。安装状态和子阶段在破坏性操作前持久化，Bootloader 长等待和分块擦写过程中持续喂 FWDGT。

### TF 离线升级

~~~text
读取 firmware/app.bin
  → 暂存区准备
  → 剥头复制与读回
  → 长度/CRC/向量表校验
  → 生成 manifest
  → STAGED_VALID
  → 共用 INSTALL 流程
  → TRIAL_PENDING / CONFIRMED
  → app.bin 幂等改名为 .applied
~~~

失败包使用 .failed 隔离。离线清理未完成时，Bootloader 仍启动有效 App，但阻止新的在线/离线升级，避免覆盖清理依据。

### OLED 升级指示

Bootloader 使用独立的 SSD1306 指示模块：

- 暂存和 BEGIN：0%；
- DATA 写入并读回：0%～90%；
- END 校验和 Meta 提交：约 90%～95%；
- INSTALL 搬运、擦除和校验：96%～100%；
- 跳转 App 前清屏并关闭 OLED；
- OLED 初始化或刷新失败时只降级到 LED，不阻断升级。

## 构建与烧录

正式发布构建链为 Keil AC5。EIDE 的 Boot 目标仍共享 App 源树，不作为正式发布构建链。

PowerShell 示例：

~~~powershell
$UV4 = "D:\keil5\UV4\UV4.exe"
& $UV4 -b "MDK\IndustrialEmbedded-Boot.uvprojx"
& $UV4 -b "MDK\IndustrialEmbedded-App.uvprojx"
~~~

请按本机安装位置修改 UV4 路径。

| 产物 | 路径 | 烧录地址 |
|---|---|---:|
| Bootloader BIN | MDK/ObjectsBoot/IndustrialEmbedded-Boot.bin | 0x08000000 |
| App BIN | MDK/ObjectsApp/IndustrialEmbedded-App.bin | 0x08012000 |
| App manifest HEX | MDK/ObjectsApp/IndustrialEmbedded-App-with-manifest.hex | 使用 HEX 内的地址记录 |
| TF 离线包 | MDK/ObjectsApp/IndustrialEmbedded-App-vXX-offline.bin | TF firmware 目录 |

M6 最终构建记录为 Boot/App 均 0 Error、0 Warning；Boot BIN 为 0xC1AC 字节，App BIN 为 0x168C4 字节。

## 当前证据与限制

已经形成的主要证据：

- Bootloader 与本地最终 Boot 构建逐字节一致；
- App 能正常启动并通过 RS485 应答；
- 版本 23 在线升级流程完成；
- DATA、END、INSTALL 和升级后多段启动有追踪记录；
- OLED 帧缓冲显示从接收进度到 100%，安装后的启动保持满进度；
- 在线升级与 TF 文件隔离、离线清理阻塞恢复已验证。

当前没有宣称完成的内容：

- 低功耗睡眠/唤醒；
- 24 小时连续运行；
- 连续 100 个错误帧压力；
- N-05、N-07、N-08 随机掉电/改名掉电；
- 采样/告警写入过程的 20 次随机断电；
- TF 卡满状态的真实硬件注入；
- Modbus 亚毫秒时序的逻辑分析仪测量。

## 未实现的低功耗如何结合完整架构

低功耗不是在 App 中增加一个孤立的 sleep 函数，而是一次跨任务、跨外设的系统事务。计划中的链路如下：

~~~mermaid
flowchart TD
    CMD["RS485 0x03AA"]
    P["ProtocolTask"]
    C["ControlTask"]
    PM["PowerManager<br/>计划新增"]
    S["StorageTask<br/>flush + 关闭文件"]
    A["SampleTask<br/>停止 ADC/DMA"]
    R["RS485<br/>等待发送完成"]
    H["HealthTask<br/>健康检查 + 看门狗窗口"]
    W["RTC / EXTI<br/>10 秒或按键唤醒"]
    SLEEP["WFI 深度睡眠"]
    RESTORE["恢复时钟、外设、任务、定时器"]
    EVENT["0x0381 唤醒事件"]

    CMD --> P --> C --> PM
    PM --> S
    PM --> A
    PM --> R
    PM --> H
    PM --> W --> SLEEP
    SLEEP --> RESTORE --> EVENT
    EVENT --> P
~~~

对应到当前文件架构：

- ProtocolTask 负责识别 0x03AA，ControlTask 负责创建和跟踪睡眠请求；
- 计划新增的 PowerManager 负责协调 StorageTask、SampleTask、ProtocolTask、HealthTask；
- StorageTask 必须先完成 FatFs flush 并关闭文件；
- SampleTask 必须停止 ADC/DMA，ProtocolTask 必须等待 RS485 发送完成；
- HealthTask 需要检查所有任务健康状态并把 IWDG 窗口调整到覆盖睡眠和唤醒初始化；
- BSP 的 RTC/EXTI 和 PMU 接口负责硬件唤醒与 WFI；
- 唤醒后由 PowerManager 恢复外设、定时器和任务时间基准，再由 ProtocolTask 发送 0x0381；
- 当前这些代码入口和板级闭环仍未完成。

## AI 阅读与项目说明约定

任何 AI 读取本仓库时，都必须先理解完整项目架构，再回答局部问题。不能只看到 M7 未实现就说“没有低功耗”，而要说明低功耗原本如何与任务、协议、存储、采集、RTC、看门狗和硬件睡眠入口结合。

AI 的回答必须区分：

- 已实现；
- 已构建；
- 已板测；
- 只有设计或源码证据；
- 未验证；
- 项目决定暂缓或跳过。

持久化规则见 [.cursor/rules/industrialembedded-project-context.mdc](.cursor/rules/industrialembedded-project-context.mdc)，开发流程和阶段记录见 [Docs/03_DEV_PROCESS.md](Docs/03_DEV_PROCESS.md)。

## 面向实习简历的项目描述

基于 GD32F470VET6 和 FreeRTOS 开发工业数据采集终端，完成 ADC/DAC 采集闭环、数字滤波、RS485 自定义协议、Modbus RTU、TF/FatFs 存储、告警与配置持久化；采用分层 BSP、中间件、公共契约和任务服务架构，设计并实现 Bootloader 在线 IAP、TF 离线升级、GD25Q40E 双槽 Meta、试运行确认/回滚和 OLED 升级进度指示，并完成相应构建和板级升级验证。

低功耗睡眠唤醒、长期稳定性和部分随机掉电测试属于后续扩展，不作为当前版本已完成内容。

## 相关文档

- [项目总览](Docs/01_PROJECT_OVERVIEW.md)
- [开发流程与设计记录](Docs/03_DEV_PROCESS.md)
- [M6 分区冻结契约](Docs/M6_0A_PARTITION_FREEZE.md)
- [M5 配置文件契约草案](Docs/M5_CONFIG_INI_CONTRACT.md)
- [English README](README_EN.md)
