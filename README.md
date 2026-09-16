# GD32F470 工业数据采集终端

[English version](README_EN.md)

> 一个基于 GD32F470VET6、FreeRTOS、RS485、Modbus RTU、TF/FatFs 和 Bootloader 升级链路的工业数据采集终端项目。

## 项目定位

本项目面向工业现场数据采集与设备维护场景，围绕一块 GD32F470VET6 控制板完成以下系统链路：

- ADC 电压采集、滤波、变比换算和 DAC 回读闭环；
- USART0 CLI、USART1 RS485 自定义协议和 Modbus RTU；
- OLED、LED、按键和 RTC 基础服务；
- TF 卡/FatFs 采样、告警、审计和配置持久化；
- GD25Q40E 外部 SPI NOR Flash 参数/告警存储；
- Bootloader 在线 IAP、TF 离线升级、双槽升级 Meta、启动确认和回滚；
- 面向后续低功耗的完整系统架构设计。

项目重点不是单一外设 Demo，而是展示嵌入式系统中的任务分工、资源所有权、异步请求、可靠存储、通信协议和可恢复升级。

## 当前状态与结项范围

**当前状态：M0～M6 核心功能按选定范围完成，项目已进入最终阶段。**

由于实习求职安排，项目在 M6 后暂停新增开发和测试，不继续实现 M7。M7 的低功耗与总验收保留为后续扩展，不能被描述为当前已经完成。

| 阶段 | 当前结论 | 主要内容 |
|---|---|---|
| M0 | 已完成 | 硬件资源、时钟、DMA、Flash 边界和接口职责冻结 |
| M1 | 已完成 | Boot/App 双工程、分区链接、向量表和 Boot→App 跳转 |
| M2 | 已完成 | CRC、RingBuffer、CLI、协议帧、Flash KV 和升级序列化 |
| M3 | 核心已完成 | FreeRTOS 静态任务、驱动链路、StorageTask 和健康监控 |
| M4 | 核心已完成 | ADC/DAC、采样换算、CLI、自定义协议、Modbus 和自动上报 |
| M5 | 核心已完成 | TF/FatFs、配置持久化、告警、审计、拔卡恢复和 TF 状态管理 |
| M6 | 按选定范围完成 | 在线/离线升级、Meta 双槽、确认/回滚、OLED 升级进度 |
| M7 | 暂缓 | 低功耗睡眠/唤醒和最终全量稳定性验收 |

M6 的 N-05、N-07、N-08 随机掉电/改名掉电项目按当前项目决定未执行，不能写成全部 N 类验收通过。24 小时连续运行、100 个错误帧压力和完整掉电矩阵也不属于当前结项证据。

## 系统总体架构

~~~mermaid
flowchart LR
    HOST["PC / USB-RS485"]
    BOOT["Bootloader<br/>在线 IAP + TF 离线升级<br/>Meta 双槽 + 回滚"]
    APP["App<br/>FreeRTOS 静态任务"]
    META["GD25Q40E<br/>业务 KV + 升级 Meta"]
    FLASH["内部 Flash<br/>App / Backup / Staging"]
    TF["TF 卡<br/>SDIO + FatFs"]
    ADC["ADC / DAC<br/>采集闭环"]
    OLED["OLED / LED / KEY"]
    RTC["RTC / 看门狗"]

    HOST <--> BOOT
    BOOT <--> META
    BOOT <--> FLASH
    BOOT --> APP
    APP <--> HOST
    APP <--> ADC
    APP <--> TF
    APP <--> META
    APP --> OLED
    APP --> RTC
~~~

系统分为两个互相衔接但职责独立的运行映像：

1. Bootloader 位于内部 Flash 起始区域，负责启动分派、升级协议、镜像校验、备份/搬运、Meta 原子提交、试运行和回滚。
2. App 位于独立的内部 Flash 区域，运行 FreeRTOS 和全部采集、通信、存储、告警业务。

## App 任务与资源所有权

App 使用静态 FreeRTOS 对象，关闭动态内存分配，不链接 heap_4。任务之间通过队列、事件组、互斥锁和任务通知传递请求及完成结果。

| 任务 | 主要职责 | 典型资源 |
|---|---|---|
| ProtocolTask | USART 接收、帧边界处理、CRC、协议分派、响应发送 | USART1、RS485、请求/结果队列 |
| SampleTask | ADC/DMA 采集、三点均值、变比换算、最新值快照 | ADC1、DAC 回读、采样队列 |
| StorageTask | TF/FatFs、配置、采样、告警、审计和外部 Flash 持久化 | TF、FatFs、GD25Q40E |
| AlarmTask | 超限判断、连续次数、滞回、ACTIVE/RECOVERED 状态机 | 采样结果、告警请求 |
| ControlTask | 命令协调、配置应用、异步请求和结果匹配 | CLI、按键、业务请求 |
| DisplayTask | OLED 刷新、按键扫描和显示服务 | I2C0、SSD1306、按键 |
| HealthTask | 任务心跳、deadline/busy 状态、栈水位和看门狗决策 | IWDG、健康状态槽 |

关键所有权约束：

- ISR 只负责接收字节、记录时间和投递事件，不解析协议、不打印、不擦写 Flash；
- StorageTask 是 App 侧 FatFs 和持久化请求的唯一执行上下文；
- ControlTask、AlarmTask 不直接调用 FatFs、Flash KV GC 或持有 SPI 锁；
- DisplayTask 是 App 侧 OLED/I2C 显示所有者；
- ProtocolTask 负责收发与协议编码，ControlTask 负责业务执行；
- Bootloader 运行期间与 App 不并发，Bootloader 在升级阶段独占升级所需的外部 Flash/TF 资源。

典型业务数据流：

~~~text
USART / 按键 / CLI
        ↓
ProtocolTask / ControlTask
        ↓
SampleTask ──→ AlarmTask ──→ StorageTask ──→ TF / GD25Q40E
        │                         ↑
        └────────→ DisplayTask    │
                                  │
HealthTask ──健康状态/睡眠就绪────┘
~~~

## 硬件与接口

| 功能 | MCU 资源 | 板级用途 |
|---|---|---|
| 系统时钟 | 25 MHz HXTAL → 240 MHz | GD32F470 主时钟 |
| RTC | LSE 32.768 kHz | 日历、Unix 时间和后续唤醒 |
| ADC CH0/CH1 | PC0 / PC1 | 两路模拟采样 |
| DAC | PA4 | DAC 输出，经跳线回读至 PC1 |
| USART0 | PA9 / PA10 | H7、板载 CH340、CLI |
| USART1 | PA2 / PA3，PA1 方向控制 | H6、RS232/RS485 |
| USART2 | PB10 / PB11 | CN1 外部串口 |
| OLED | I2C0 PB8 / PB9 | SSD1306 128×32 |
| SPI NOR | SPI1 PB13～PB15，CS=PB12 | GD25Q40E |
| TF 卡 | SDIO PC8～PC12、PD2，检测=PE2 | SDIO 块设备和 FatFs |
| LED | PD8～PD13 | 系统、协议、告警和升级指示 |
| KEY | PE15、PE13、PE11、PE9、PE7、PB0 | 按键扫描和事件输入 |

## Flash 与存储分区

### GD32F470 内部 Flash

芯片内部 Flash 为 512 KiB，地址范围为 0x08000000～0x0807FFFF。

| 区域 | 起始地址 | 结束地址 | 大小 | 作用 |
|---|---:|---:|---:|---|
| Bootloader | 0x08000000 | 0x0800FFFF | 64 KiB | 启动与升级 |
| 旧 Meta 保留区 | 0x08010000 | 0x08011FFF | 8 KiB | 已退役，仅保留布局占位 |
| App | 0x08012000 | 0x08031FFF | 128 KiB | FreeRTOS 应用 |
| Backup | 0x08032000 | 0x08051FFF | 128 KiB | 旧 App 备份与回滚 |
| Staging | 0x08052000 | 0x08071FFF | 128 KiB | 新固件暂存 |
| 尾部保留区 | 0x08072000 | 0x0807FFFF | 56 KiB | 当前不占用 |

App、Backup、Staging 各保留最后 64 字节作为 manifest 区域。App manifest 地址为 0x08031FC0。旧的内部 Flash Meta 槽已废弃，避免烧录 App 时被整扇区擦除。

### GD25Q40E 外部 SPI NOR Flash

GD25Q40E 容量为 4 Mbit，即 512 KiB，sector 大小为 4 KiB。

| 区域 | Offset | 作用 |
|---|---:|---|
| Business KV slot A/B | sector 0/1 | 变比、阈值、设备 ID、波特率、协议模式等 |
| Upgrade Meta slot A/B | sector 2/3 | 升级状态、active/pending/backup、版本、CRC 和来源 |
| Diagnostic sector | 0x7F000 | 诊断用途 |

升级 Meta 为 72 字节固定序列化记录，使用 CRC、generation 和最后写入的 commit marker 实现双槽轮换与原子选择。

## 通信接口

### USART0 CLI

USART0 用于本地配置、诊断和模式控制，CLI 在 ControlTask 上下文执行，ISR 只接收并入队。主要能力包括：

- 设备 ID、波特率、采样周期、变比和阈值配置；
- protocol 模式切换；
- RTC 配置和查询；
- 采样启动/停止、显示隐藏、状态诊断；
- config save/read 和 TF 状态查询。

### RS485 自定义协议

USART1 通过 PA1 控制 RS485 收发方向。协议具备帧头搜索、半帧、粘包、噪声前缀、CRC 错误重同步、重复帧缓存和异步请求/结果匹配能力。

已覆盖的业务方向包括：

- 设备信息、重启、配置查询和修改；
- ADC/DAC、阈值、变比和 TF 状态；
- 自动上报启停和间隔；
- 告警事件、心跳和状态查询；
- 错误帧、非法命令、忙碌状态和广播静默。

### Modbus RTU

Modbus 使用 USART1 的 8E1，支持 03、04、06、10 功能码、标准异常响应、CRC16-Modbus、广播写静默和模式往返切换。Modbus 业务在 ControlTask 中执行，不能绕过任务边界直接操作存储或硬件。

## Bootloader 升级链路

### 在线升级

在线升级由 App 和主机协作完成：

1. 主机发送 0x0500 ENTER_BOOT；
2. App 持久化升级请求并应答 READY；
3. App 复位进入 Bootloader；
4. Bootloader 等待 0x0501 BEGIN；
5. 接收 0x0502 DATA，写入 Staging 后读回校验，再发送 ACK；
6. 0x0503 END 校验长度、CRC、manifest 和向量表；
7. 0x0504 INSTALL 执行 Backup、App 擦除、App 编程和整体校验；
8. 新 App 进入 TRIAL_PENDING；
9. App 满足启动健康条件后写 CONFIRMED；
10. Bootloader 提交 active=pending，失败时按状态机回滚。

0x0505 ABORT 用于原子清除在线升级上下文。安装过程中不接受普通通信命令，所有破坏性操作前先持久化状态和安装子阶段。

### TF 离线升级

Bootloader 检查 TF 的 firmware/app.bin，执行：

~~~text
读取固件头
  → 暂存区准备
  → 剥头复制与读回
  → 长度/CRC/向量表校验
  → 生成 manifest
  → STAGED_VALID
  → 共用 INSTALL 流程
  → CONFIRMED
  → app.bin 幂等改名为 .applied
~~~

失败包通过 .failed 隔离。离线清理未完成时，Bootloader 仍启动有效 App，但会阻止新的在线/离线升级，直到原 TF 文件被正确清理。

### OLED 升级指示

Bootloader 独立使用 SSD1306 指示升级进度：

- BEGIN/暂存准备：0%；
- DATA 接收：0%～90%；
- END 校验和 Meta 提交：约 90%～95%；
- INSTALL 搬运、擦除和校验：96%～100%；
- 跳转 App 前清屏并关闭 OLED；
- OLED 初始化或刷新失败时自动降级为 LED，不阻断升级。

## 构建与烧录

正式发布构建以 Keil AC5 为准。EIDE 的 Boot 目标仍共享 App 源树，不作为本项目的正式发布构建链。

PowerShell 构建示例：

~~~powershell
$UV4 = "D:\keil5\UV4\UV4.exe"
& $UV4 -b "MDK\IndustrialEmbedded-Boot.uvprojx"
& $UV4 -b "MDK\IndustrialEmbedded-App.uvprojx"
~~~

请根据本机 Keil 安装位置修改 UV4 路径。

主要产物：

| 产物 | 路径 | 烧录地址 |
|---|---|---:|
| Bootloader BIN | MDK/ObjectsBoot/IndustrialEmbedded-Boot.bin | 0x08000000 |
| App BIN | MDK/ObjectsApp/IndustrialEmbedded-App.bin | 0x08012000 |
| App manifest HEX | MDK/ObjectsApp/IndustrialEmbedded-App-with-manifest.hex | 由 HEX 地址记录决定 |
| TF 离线包 | MDK/ObjectsApp/IndustrialEmbedded-App-vXX-offline.bin | TF firmware 目录 |

M6 最终记录的 Boot/App 构建均为 0 Error、0 Warning；Boot BIN 为 0xC1AC 字节，App BIN 为 0x168C4 字节。具体版本、manifest 和 CRC 以对应打包产物为准。

## 当前证据与限制

当前已记录的 M6 证据包括：

- Bootloader 和本地最终 Boot 构建逐字节一致；
- App 能正常启动并通过 RS485 应答；
- 23 号在线升级流程通过；
- DATA、END、INSTALL 和升级后多次启动状态均有追踪记录；
- OLED 帧缓冲显示从接收进度到 100%，安装后的启动保持满进度；
- N-09 在线升级与 TF 文件隔离、N-10 离线清理阻塞恢复已形成闭环。

以下内容不属于当前已完成证据：

- M7 低功耗睡眠/唤醒；
- 24 小时连续运行和最终稳定性验收；
- M6 N-05、N-07、N-08 随机掉电/改名掉电；
- 采样和告警写入过程的 20 次随机断电；
- TF 卡满状态的真实硬件注入；
- Modbus 亚毫秒时序的逻辑分析仪测量。

## M7 未实现时如何与完整项目结合

M7 不是孤立增加一个 sleep 函数，而是对现有任务和资源所有权做一次系统级协调。计划中的结合方式如下：

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
    WFI["WFI 深度睡眠"]
    RES["恢复时钟、外设、任务和定时器"]
    EVT["0x0381 唤醒事件"]

    CMD --> P --> C --> PM
    PM --> S
    PM --> A
    PM --> R
    PM --> H
    PM --> W --> WFI
    WFI --> RES --> EVT
    EVT --> P
~~~

因此，当前 M7 “未实现”具体意味着：

- 当前没有完成 PowerManager 的项目级协调入口；
- 当前 App 协议中 0x03AA 仍是 M7 预留命令；
- 当前没有把 StorageTask、SampleTask、ProtocolTask、HealthTask 和 RTC/EXTI 连接成完整睡眠事务；
- 当前没有对唤醒后的软件定时器、vTaskDelayUntil 基准和业务使能状态做闭环验证。

## 仓库结构

~~~text
Common/       Boot/App 共用的 CRC、Flash 布局、升级序列化和复位契约
BSP/          GPIO、ADC、DAC、USART、RS485、RTC、OLED、SPI、SDIO 等底层驱动
Bootloader/   启动分派、在线/离线升级、Meta、安装、试运行和回滚
App/          FreeRTOS 配置、应用入口、CLI、协议和升级确认
Tasks/        Protocol/Sample/Storage/Alarm/Control/Display/Health 任务
Middleware/   FreeRTOS、FatFs、Protocol、Modbus 等中间件
test/         PC 组件测试、协议测试和主机回归工具
tools/        manifest、离线包和在线升级主机工具
MDK/          Keil AC5 工程、链接和构建输出目录
Docs/         项目总览、执行流程、分区契约和阶段记录
.cursor/rules/项目级 AI 阅读与架构说明约定
~~~

## AI 阅读与项目说明约定

任何 AI 读取本仓库时，都应先按完整 M0～M7 架构理解项目，再回答当前进度。对于当前没有实现的模块，必须说明它应该怎样接入现有任务、协议、硬件、存储和验收链路。

AI 的回答应明确区分：

- 已实现；
- 已构建；
- 已板测；
- 只有设计或源码证据；
- 未验证；
- 项目决定暂缓或跳过。

例如，不能只说“M7 没做”，而应继续解释 M7 如何通过 0x03AA、ControlTask、PowerManager、StorageTask、ADC/DMA、RS485、RTC/EXTI、看门狗、WFI 和 0x0381 与现有项目结合，同时明确这些代码和验证目前仍未完成。

持久化规则见 [.cursor/rules/industrialembedded-project-context.mdc](.cursor/rules/industrialembedded-project-context.mdc)，完整执行流程见 [Docs/03_DEV_PROCESS.md](Docs/03_DEV_PROCESS.md)。

## 简历项目描述

基于 GD32F470VET6 和 FreeRTOS 开发工业数据采集终端，完成 ADC/DAC 采集闭环、数字滤波、RS485 自定义协议、Modbus RTU、TF/FatFs 存储、告警与配置持久化；设计并实现 Bootloader 在线 IAP、TF 离线升级、GD25Q40E 双槽 Meta、启动确认/回滚和 OLED 升级进度指示，完成相应构建和板级升级验证。

低功耗睡眠唤醒和长期稳定性/随机掉电测试属于本项目后续扩展，不作为当前版本已完成内容。

## 相关文档

- [项目总览](Docs/01_PROJECT_OVERVIEW.md)
- [开发流程与阶段记录](Docs/03_DEV_PROCESS.md)
- [M6 分区冻结契约](Docs/M6_0A_PARTITION_FREEZE.md)
- [M5 配置文件契约草案](Docs/M5_CONFIG_INI_CONTRACT.md)
- [English README](README_EN.md)
