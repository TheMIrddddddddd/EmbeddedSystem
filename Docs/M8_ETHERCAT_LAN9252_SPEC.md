# M8：GD32F470 + LAN9252 EtherCAT 从站与本地监控平台规格

> 文档版本：V1.0  
> 日期：2026-09-16  
> 阶段：M8 规划基线  
> 状态：规格已冻结；本轮只新增文档，不代表驱动、从站栈或板级功能已经实现

## 1. 文档目的

本文档定义当前 IndustrialEmbedded 项目接入 LAN9252 EtherCAT 从站模块的开发边界、硬件连接、软件分层、过程数据、诊断数据、本地 HTML 监控链路、开发顺序和验收标准。

后续开发必须以本文档为接口基线。若卖家提供的 LAN9252 原理图、EEPROM 配置、ESI 文件、SSC 示例与本文档存在差异，应先记录差异并更新本规格，再进入代码实现；不能用猜测的引脚、对象字典或 PDO 偏移直接开始联调。

## 2. 目标与范围

### 2.1 总体目标

在不破坏当前 GD32F470 工业数据采集终端功能的前提下，增加一条 EtherCAT 从站数据链路：

    ADC / DAC / TF / LED / App / FreeRTOS 诊断
                         │
                         ▼
                GD32F470 应用控制器
                         │ SPI PDI
                         ▼
                   LAN9252 ESC
                         │ EtherCAT 网线
                         ▼
                TwinCAT EtherCAT 主站
                         │ ADS
                         ▼
               C#/.NET 本地 ADS-WebSocket 桥
                         │ WebSocket/HTTP
                         ▼
                     本地 HTML 页面

### 2.2 M8 v1 纳入范围

- GD32F470 通过独立 SPI2 访问 LAN9252 PDI。
- 完成 LAN9252 复位、Probe、寄存器访问、DPRAM 访问和 IRQ 唤醒。
- 移植卖家提供的 EtherCAT Slave Stack Code（SSC）或等价从站栈。
- 使用卖家的 ESI、对象字典和初始 PDO 配置作为从站描述基线。
- 使用 TwinCAT 作为第一阶段 EtherCAT 主站完成扫描、状态机和 PDO 验证。
- 通过 ADS 读取 TwinCAT 变量，并转换为本地 WebSocket/JSON 数据流。
- HTML 页面显示过程数据、TF 卡、LED、版本、EtherCAT 链路和全部任务健康信息。
- HTML 发送的 DAC、继电器和控制命令经过桥接服务校验后再写入 EtherCAT RxPDO。

### 2.3 M8 v1 不纳入范围

- CiA402 伺服驱动器或运动控制对象字典。
- EtherCAT FoE 固件升级。
- Distributed Clock 精密同步和同步运动控制。
- 自定义 EtherCAT 主站。
- 使用 GD32 内部 ENET 外设直接收发 EtherCAT 帧。
- 让 LAN9252 直接提供 HTTP、WebSocket 或 HTML 服务。
- 用 EtherCAT 取代当前 USART/RS485/Modbus 业务协议。
- 在 EtherCAT 任务中直接调用 FatFs、Flash 擦写或 OLED 驱动。
- 一开始建立大规模 PDO 或完整工业设备对象字典。

## 3. 当前工程基线

M8 是新增阶段，不改变 M0～M6 已有功能边界，也不把尚未完成的 M7 低功耗和总验收写成已完成。当前开发流程明确说明 M7 暂缓，后续恢复项目时仍需保留原有 M7 边界；EtherCAT 作为新的 M8 扩展阶段单独管理。

### 3.1 已占用的板级资源

| 资源 | 当前用途 | 证据位置 | M8 约束 |
|---|---|---|---|
| SPI1 | GD25Q40E 外部 Flash | BSP/board_config.h:75-82 | 不得改作 LAN9252 |
| PB12 | 外部 Flash CS | BSP/board_config.h:80-81 | 保持不变 |
| SDIO | TF 卡数据、时钟、命令 | BSP/board_config.h:84-98 | 保持不变 |
| PE2 | TF 卡检测 | BSP/board_config.h:96-97 | 不得被 SPI3 方案占用 |
| I2C0 | OLED | BSP/board_config.h:70-73 | DisplayTask 独占 |
| ADC1 | PC0、PC1 模拟采集 | BSP/board_config.h:100-106 | 继续由 SampleTask 管理 |
| DAC0 | PA4 模拟输出 | BSP/board_config.h:108-109 | 继续由 ControlTask 管理 |
| USART0 | CLI、调试日志 | BSP/board_config.h:52-55 | 不用于 EtherCAT |
| USART1/RS485 | 业务协议通信 | BSP/board_config.h:57-64 | 不并入 EtherCAT |

### 3.2 当前任务基线

当前任务由 Tasks/src/app_task.c 创建，包含 Display、Sample、Storage、Alarm、Control、Protocol 和 Health 任务。M8 新增 EtherCATTask 后，必须同步接入任务创建失败返回路径和 HealthTask 监测表。

当前最高业务优先级为 HealthTask/ProtocolTask=5U，FreeRTOS 优先级范围为 0～7，配置见 App/config/FreeRTOSConfig.h。当前任务的栈深度和优先级必须在新增任务后重新做 Flash/RAM 和运行时水位评估。

### 3.3 当前可复用的诊断来源

| 诊断项 | 当前接口或来源 | M8 使用方式 |
|---|---|---|
| TF 卡诊断 | Tasks/inc/storage_task.h 中的 storage_task_sdio_diag_get | 复制到只读诊断快照 |
| TF 卡挂载 | storage_task_fatfs_mounted_get | 只读读取 |
| TF 卡存储使能 | storage_task_fatfs_storage_enabled_get | 只读读取 |
| TF 卡满卡 | storage_task_fatfs_full_get | 只读读取 |
| TF 卡物理存在 | BSP/board_sdio.h 中的 board_sdio_card_present | 只读读取 |
| App 版本 | App/inc/app_cli.h 中的 APP_CLI_VERSION | 编译期固定版本字段 |
| 构建时间 | App/src/app_cli.c 中的 __DATE__/__TIME__ | 编译期构建信息 |
| 协议版本 | App/src/app_protocol.c 中的协议版本字段 | 映射到固件诊断 |
| LED 逻辑状态 | BSP/board_gpio.h 的 LED API及其调用者 | 增加状态影子或统一状态接口 |
| 任务栈高水位 | 各任务 *_task_get_stack_high_water_mark 接口 | 汇总到 HealthTask 快照 |
| 任务健康 | Tasks/src/health_task.c 的心跳和健康判断 | 新增 EtherCATTask 项 |

当前 StorageTask 是 FatFs 的唯一所有者，DisplayTask 是 OLED/I2C 的唯一所有者。EtherCAT 只读取已发布的快照，不能绕过任务边界访问私有变量或直接调用底层文件系统。

## 4. LAN9252 角色与通信边界

### 4.1 LAN9252 的角色

LAN9252 作为 EtherCAT 从站控制器（ESC）和以太网物理接口使用。GD32 不负责解析 EtherCAT 原始以太网帧，而是通过 LAN9252 的 PDI 接口访问 ESC 寄存器、过程数据 RAM、邮箱和状态信息。

因此，M8 的通信链路分为两层：

1. 设备内部 PDI 链路：GD32 SPI2 ↔ LAN9252 PDI。
2. 设备外部 EtherCAT 链路：LAN9252 RJ45 IN/OUT ↔ EtherCAT 主站网卡。

普通浏览器不能直接读取 LAN9252 的 EtherCAT 帧，也不能用 JavaScript 直接代替 EtherCAT 主站。浏览器只连接本机的 HTTP/WebSocket 服务；本地桥接服务负责把 TwinCAT 的 ADS 变量转换为 JSON。

### 4.2 EtherCAT 与普通 Ethernet 的区别

| 项目 | 普通 Ethernet/TCP/IP | EtherCAT/M8 |
|---|---|---|
| 典型端点 | IP 地址、TCP/UDP 端口 | EtherCAT MainDevice/SubDevice 和 ESC |
| 数据处理 | 网卡、协议栈、操作系统网络栈 | LAN9252 在硬件中处理 EtherCAT 帧并交换过程数据 |
| 主站 | 应用程序或操作系统网络协议栈 | TwinCAT 等 EtherCAT 主站 |
| 从站 | TCP/UDP 服务端或普通网络设备 | LAN9252 + GD32 的 EtherCAT 从站 |
| 浏览器接入 | 可直接访问 HTTP 服务 | 必须先经过 TwinCAT/ADS/桥接服务 |
| M8 中 GD32 的角色 | 不直接做 IP 网络服务 | 通过 SPI 提供应用数据给 LAN9252 |

LAN9252 的 RJ45 接口外形与普通网口相似，但 M8 的验证对象是 EtherCAT 从站，而不是“GD32 开一个 TCP 服务器”。

## 5. 硬件方案

### 5.1 已冻结的 SPI 方案

选择独立 SPI2，不复用当前 SPI1，也暂不使用 SPI3。

| LAN9252 功能 | GD32F470 推荐引脚 | 电气/软件配置 |
|---|---|---|
| SCK | PB3 | SPI2_SCK，AF5 |
| MISO | PB4 | SPI2_MISO，AF5 |
| MOSI | PB5 | SPI2_MOSI，AF5 |
| CS/SCS_N | PE3 | 普通 GPIO，低有效，软件控制 |
| RESET_N | PE4 | 普通 GPIO，按模块电平定义控制 |
| IRQ | PE5 | 输入，EXTI，具体触发沿以模块原理图和波形为准 |
| GND | GND | GD32 与模块共地 |
| 3V3 | 3.3 V | 以模块原理图、电流和电平说明为准 |

GD32F470 数据手册列出 PB3/PB4/PB5 的 SPI2 复用功能。当前工程的 SPI1 已固定使用 PB13/PB14/PB15，片选使用 PB12；SPI3 的常用复用引脚会碰到当前 TF 卡检测资源，因此不作为第一方案。

PB3/PB4 具有 JTAG 相关默认功能。当前调试链路采用 SWD，必须保留 PA13/PA14 作为 SWD 调试，并在实际 PCB 上确认 PB3/PB4/PB5 未被其他外部电路占用。若这些引脚未引出或被占用，必须先更新硬件引脚表，不允许仅修改代码假定接线已经成立。

### 5.2 硬件确认门槛

进入代码阶段前，必须读取并核对以下资料：

    D:\BaiduNetdiskDownload\01_iRDT-EtherCAT\硬件\SCH_EtherCAT_LAN9252_V1.2.pdf

至少确认：

- 模块 SPI 接口的实际针脚名称和顺序；
- SCS_N 是否由模块引出，是否要求外部上拉；
- RESET_N 的输入电平、复位脉宽和上电默认状态；
- IRQ 的有效电平、输出类型和 EXTI 触发沿；
- 模块是否已经通过 EEPROM 配置为 SPI PDI；
- 模块 3.3 V 供电能力和 IO 电平；
- 25 MHz 晶振、PHY 和 RJ45 屏蔽的板级连接；
- LAN9252 IN/OUT 端口的连接方向和链路 LED 定义。

如果卖家资料只给出了 STM32 示例的命名，应建立一张“卖家信号名 ↔ GD32 信号名”映射表，禁止凭相似名字直接连接。

### 5.3 SPI 初始参数

| 参数 | M8-1 默认值 | 变更条件 |
|---|---:|---|
| 数据宽度 | 8 bit | 只有卖家 PDI 配置明确要求时才改变 |
| 主从关系 | GD32 Master，LAN9252 PDI Client | 固定 |
| 位序 | MSB first | 固定为首选，按数据手册/示例确认 |
| 片选 | GPIO 软件控制 | 不使用硬件 NSS |
| SPI 模式 | Mode 0 | Probe 阶段默认；若读回异常，优先核对 EEPROM/PDI 配置和逻辑分析仪 |
| Probe 频率 | 2 MHz | 用于最初通信确认 |
| 稳定目标 | 8 MHz | 必须不超过 LAN9252 模块和配置允许值 |
| 收发方式 | 轮询 | M8-1 不引入 DMA |
| DMA | 后续优化项 | 只有性能测试证明轮询不足时再评估 |

SPI 事务必须由任务或同步调用上下文执行，禁止在 IRQ ISR 中进行寄存器读写、等待 BUSY 或执行长事务。LAN9252 IRQ ISR 只允许清除/记录必要的硬件状态、设置事件或发送 FreeRTOS 任务通知。

## 6. 软件架构

### 6.1 建议目录

    BSP/
    ├── board_lan9252.c
    └── board_lan9252.h

    Middleware/EtherCAT/
    ├── inc/
    │   ├── ecat_pdi.h
    │   ├── ecat_esc.h
    │   └── ecat_stack_port.h
    └── src/
        ├── ecat_pdi.c
        ├── ecat_esc.c
        └── ecat_stack_port.c

    Tasks/
    ├── inc/ecat_task.h
    └── src/ecat_task.c

    App/
    ├── inc/app_ecat_data.h
    └── src/app_ecat_data.c

    tools/ethercat_dashboard/
    ├── bridge/
    └── web/

目录只是 M8 的目标布局。M8-0/M8-1 阶段不要为了“先建空文件”而把未验证的栈文件加入正式构建；只有对应阶段完成接口确认后，才把源文件纳入 Keil/EIDE 目标。

### 6.2 分层职责

| 层 | 模块 | 责任 | 禁止事项 |
|---|---|---|---|
| BSP | board_lan9252 | SPI2、CS、RESET、IRQ、时序和引脚 | 不解释 PDO 业务含义 |
| PDI | ecat_pdi | LAN9252 寄存器、CSR、DPRAM 读写 | 不直接访问 FatFs/OLED |
| ESC | ecat_esc | 状态寄存器、邮箱、过程数据区抽象 | 不绑定具体业务变量 |
| 栈适配 | ecat_stack_port | SSC 的临界区、计时器、PDI、平台回调 | 不重写卖家对象字典 |
| 任务 | EtherCATTask | 从站状态机、PDO 同步、事件、错误恢复 | 不做长时间文件/Flash 操作 |
| 数据聚合 | app_ecat_data | 过程数据和诊断快照 | 不持有 FatFs 私有状态 |
| 主站 | TwinCAT | 扫描、ESI、状态机、PDO、ADS | 不由 GD32 直接控制 |
| 桥接 | C#/.NET | ADS 读取/写入、JSON、WebSocket、校验 | 不让 HTML 直接写原始帧 |
| 前端 | HTML | 展示、曲线、控制按钮、断线提示 | 不假定数据永远实时 |

EtherCAT 不加入当前串口 ProtocolTask，也不复用当前 GD32 内部 ENET 驱动。RS485/Modbus 与 EtherCAT 是两条并行的业务通道。

### 6.3 EtherCATTask 配置

| 项目 | 初始值 | 说明 |
|---|---:|---|
| 任务名 | EtherCATTask | 与现有任务命名保持一致 |
| 优先级 | 6U | 高于当前最高业务任务；须验证不会饿死 HealthTask |
| 栈深度 | 512U 个 StackType_t | 仅为首版起点，按栈高水位调整 |
| 内存 | 静态 TCB + 静态栈 | 遵守当前静态分配配置 |
| 启动周期 | 10 ms | 用于 Probe、状态机和初始 PDO |
| 稳定后测试 | 1 ms | 只有 10 ms 验收通过后进入 |
| 等待方式 | IRQ 任务通知或周期事件 | 避免纯忙轮询 |
| 日志 | 限速、异步 | 不在周期路径中长时间 printf |

任务创建顺序建议放在 HealthTask 之前或由 app_tasks_create() 的明确阶段管理，并补充对应失败状态。任务必须把心跳、栈高水位、最后一次成功 PDI 事务和最近错误码发布给 HealthTask。

## 7. 设备数据模型

### 7.1 过程数据原则

过程数据和低频诊断数据分开：

- **快速 PDO**：固定长度、固定字节序、无动态字符串，服务 ADC、DAC、控制字和序号。
- **低频诊断**：100 ms～1 s 更新，允许拆分成多个 PDO 或通过 SDO/邮箱读取。
- **版本、构建时间和错误文本**：不放在高频 PDO 中，使用定长字段、枚举和错误码。
- **任务诊断**：使用固定任务槽位和固定字段，避免运行时动态分配。

所有跨 EtherCAT 边界的字段都必须使用明确宽度的整数类型，并用显式序列化/反序列化处理字节序；不能依赖编译器对 C 结构体的隐式填充。

### 7.2 快速 TxPDO：GD32 → TwinCAT

第一版过程数据固定为：

| 字段 | 建议类型 | 说明 |
|---|---|---|
| status_word | uint16_t | 设备运行状态、就绪、采样、故障等位 |
| alarm_bits | uint32_t | 当前有效报警位图 |
| adc0_raw | int32_t | ADC 通道 0 原始或已校准整数值 |
| adc1_raw | int32_t | ADC 通道 1 原始或已校准整数值 |
| sample_sequence | uint32_t | 采样递增序号，用于判断丢帧/停更 |
| uptime_seconds | uint32_t | 设备运行时间 |

最终对象索引和 PDO 偏移必须以卖家对象字典/ESI 为准。若卖家示例已有等价对象，优先保留其索引和数据类型，只将当前项目变量接入对象。

### 7.3 快速 RxPDO：TwinCAT → GD32

第一版控制数据固定为：

| 字段 | 建议类型 | 说明 |
|---|---|---|
| control_word | uint16_t | 使能、模式、复位、命令有效等控制位 |
| dac0_command | int32_t | DAC 通道 0 命令，必须做范围检查 |
| dac1_command | int32_t | DAC 通道 1 命令；若硬件只有一路 DAC，保留为预留并明确无效 |
| relay_bits | uint16_t | 继电器输出位图 |
| command_sequence | uint32_t | 上位机命令序号，用于检测重复/跳变 |

控制命令必须经过：连接状态检查、数据范围检查、序号检查、设备状态检查和失败回报。HTML 不能直接构造或写入 EtherCAT 原始帧。

### 7.4 低频诊断数据

诊断数据至少覆盖以下字段：

#### 存储与 TF 卡

- card_present：物理卡存在；
- fatfs_mounted：FatFs 已挂载；
- storage_enabled：业务存储使能；
- storage_full：空间不足/满卡状态；
- free_percent：剩余空间百分比；
- last_sdio_status：最近 SDIO 状态；
- last_read_status、last_write_status：最近读写结果；
- storage_error_count：累计错误计数。

#### LED

- led_state_mask：LED1～LED6 当前逻辑状态；
- led_phase：当前闪烁阶段或状态机阶段；
- led_semantics_version：LED 语义版本，避免主机误解灯态。

当前 LED 语义以项目任务书为基线：LED1 系统心跳、LED2 采样/上报、LED3 报警、LED4 参数错误、LED5 TF 状态、LED6 启动升级。具体逻辑仍以现有控制代码和板级验收为准。

#### 固件与设备

- app_version；
- protocol_version；
- build_date、build_time；
- device_id；
- uptime_seconds；
- reset_reason；
- firmware_feature_bits。

#### EtherCAT

- al_state：当前 AL 状态；
- link_state：端口链路状态；
- working_counter；
- pdi_error_count；
- mailbox_error_count；
- state_transition_error；
- last_ecat_error_code；
- last_pdi_transaction；
- last_pdi_timestamp。

#### FreeRTOS/任务健康

每个任务固定一个诊断槽位，至少提供：

- task_id、task_name；
- heartbeat 或递增心跳序号；
- healthy；
- stack_high_water_mark；
- stack_used_percent；
- last_run_timestamp；
- error_count；
- timeout_count。

必须包含现有 Display、Sample、Storage、Alarm、Control、Protocol、Health 任务以及新增 EtherCATTask。HealthTask 自身也要提供自检结果，不能只监测其他任务而缺少自身状态。

## 8. 固件公共接口规格

以下接口是 M8 的建议公共边界，第一阶段可以先实现最小版本，后续再补充错误码和版本字段。

### 8.1 LAN9252 BSP 接口

    int board_lan9252_init(void);
    int board_lan9252_reset(void);
    int board_lan9252_spi_transfer(const uint8_t *tx,
                                   uint8_t *rx,
                                   size_t length);
    int board_lan9252_irq_read(void);
    void board_lan9252_cs_assert(void);
    void board_lan9252_cs_deassert(void);

约束：

- board_lan9252 只处理 SPI2 和 GPIO；
- 片选的建立、事务、释放必须形成完整事务；
- 超时必须返回错误，不得无限等待；
- SPI 错误必须可计数；
- 复位后必须允许再次 Probe；
- ISR 不得调用 board_lan9252_spi_transfer。

### 8.2 PDI/ESC 接口

    int ecat_esc_reg_read(uint16_t address,
                          uint8_t *data,
                          size_t length);
    int ecat_esc_reg_write(uint16_t address,
                           const uint8_t *data,
                           size_t length);
    int ecat_process_data_read(uint16_t address,
                               uint8_t *data,
                               size_t length);
    int ecat_process_data_write(uint16_t address,
                                const uint8_t *data,
                                size_t length);

第一版不把具体寄存器地址散落到业务任务中。寄存器地址、CSR/DPRAM 访问序列、忙等待和超时集中在 ecat_pdi/ecat_esc 中。

### 8.3 诊断快照接口

    int app_ecat_diagnostics_get(ecat_diagnostics_t *snapshot);
    int health_task_snapshot_get(health_snapshot_t *snapshot);
    uint16_t board_led_state_mask_get(void);
    int storage_task_fatfs_free_percent_get(uint8_t *free_percent);

快照接口要求：

- 调用方拿到的是一次性一致快照，而不是指向任务私有变量的指针；
- 读快照不能触发 FatFs 访问、Flash 擦写或 OLED 刷新；
- 多字节字段必须使用临界区、锁、序号快照或单生产者/单消费者协议保证一致性；
- 字符串使用固定长度并明确截断规则；
- 失败时返回明确错误码，不能返回半更新数据；
- EtherCATTask 只读取快照并序列化到 PDO/邮箱区。

## 9. 本地 ADS-WebSocket 监控平台

### 9.1 固定链路

    TwinCAT EtherCAT Master
            ↓ ADS
    C#/.NET ADS-WebSocket Bridge
            ↓ WebSocket / HTTP
    Local HTML Dashboard

第一阶段不直接开发 EtherCAT 主站。TwinCAT 负责网卡绑定、从站扫描、ESI 识别、状态机、PDO 映射和 ADS 变量发布；桥接服务只处理已验证的 TwinCAT 变量。

电脑的普通物理网口可以作为 EtherCAT 主站网口，但是否能被 TwinCAT 正常绑定必须以 TwinCAT 网卡扫描结果为准。浏览器是否能打开页面，与该网口是否直接支持 EtherCAT 是两件不同的事情。

### 9.2 HTTP/WebSocket 接口

第一版固定接口：

    GET    /api/snapshot
    GET    /api/health
    GET    /api/metadata
    WS     /ws
    POST   /api/control

建议本地地址：

    HTML      http://127.0.0.1:8080
    WebSocket ws://127.0.0.1:8765/ws

端口只是 M8 v1 默认值，实际实现可配置，但 HTML、桥接服务和测试脚本必须使用同一份配置文件，不能在代码中散落硬编码。

### 9.3 JSON 顶层结构

    {
      "timestamp": "2026-09-16T00:00:00.000Z",
      "sequence": 12345,
      "stale": false,
      "ethercat": {},
      "process": {},
      "storage": {},
      "led": {},
      "firmware": {},
      "tasks": [],
      "alarms": []
    }

字段约束：

- timestamp 是桥接服务生成的接收时间；设备运行时间另放在 firmware.uptime_seconds；
- sequence 每次完整快照递增；
- stale=true 表示超过数据超时阈值，页面必须显示“数据过期/离线”，不能继续显示为正常实时值；
- 缺少的诊断字段应返回 null 或明确的 available=false，不能用 0 冒充有效值；
- tasks 使用固定 task_id，前端不依赖任务名称做主键。

### 9.4 页面最小功能

页面至少包含：

- EtherCAT Link、AL 状态、WKC、PDI/邮箱错误和最后错误码；
- ADC0、ADC1 当前值和实时曲线；
- DAC0、DAC1 当前值和命令值；
- TF 卡插入、挂载、存储使能、剩余空间和最近错误；
- LED1～LED6 逻辑状态、闪烁阶段和语义说明；
- App 版本、协议版本、构建日期时间、设备 ID、运行时间和复位原因；
- 所有任务的心跳、健康状态、栈剩余空间和使用率；
- 报警列表和报警计数；
- DAC、继电器和允许的控制命令按钮；
- ADS、WebSocket、数据新鲜度和设备在线状态。

### 9.5 控制命令规则

桥接服务在写入 TwinCAT 变量前必须检查：

- WebSocket/ADS 连接是否有效；
- JSON 字段和类型是否正确；
- DAC 命令是否落在硬件允许范围；
- 继电器位图是否只包含已声明的位；
- 命令序号是否重复、倒退或跨越异常；
- 设备是否处于允许控制的 EtherCAT/应用状态；
- 写入结果是否在指定超时内被确认。

控制失败应返回结构化错误，不允许静默丢弃，也不允许页面自行重试高风险命令。

## 10. 卖家资料与移植基线

M8-0 需要整理以下资料，推荐保存在项目外部的受控资料目录中，不把未授权的商业协议栈或工具包直接提交到公共仓库：

    D:\BaiduNetdiskDownload\01_iRDT-EtherCAT\硬件\SCH_EtherCAT_LAN9252_V1.2.pdf
    D:\BaiduNetdiskDownload\01_iRDT-EtherCAT\软件\上位机软件工具\SSC_使用手册.pdf
    D:\BaiduNetdiskDownload\01_iRDT-EtherCAT\软件\上位机软件工具\SSC_IO示例.zip
    D:\BaiduNetdiskDownload\01_iRDT-EtherCAT\软件\上位机软件工具\SSC_V5i11.zip

向卖家确认并记录：

1. 模块原理图和 SPI 接口定义；
2. SPI 最大频率、模式和事务时序；
3. RESET_N、IRQ、CS 的有效电平和引脚定义；
4. LAN9252 EEPROM 配置或烧录文件；
5. ESI XML 文件；
6. STM32G474/F407 示例工程；
7. LAN9252 PDI 驱动；
8. EtherCAT Slave Stack 源码或移植说明；
9. 对象字典、PDO 表和默认状态机要求；
10. TwinCAT 示例工程、网卡绑定说明和故障排查方法。

移植策略：保留卖家的 EtherCAT 栈、对象字典、ESI 和 PDO 配置，优先只替换 STM32 HAL 的 SPI、GPIO、RESET、IRQ、Timer、临界区和平台类型接口，改成 GD32 SPL/CMSIS 实现。未经验证不得同时重写栈和重排对象字典，否则无法区分平台移植问题与 EtherCAT 配置问题。

## 11. M8 开发流程

每个子阶段都必须有明确入口、产出和退出验收。阶段只允许按顺序推进；前一阶段未通过时，不能用后一阶段的界面或“能看到网口”代替底层证据。

### M8-0：资料、构建和硬件确认

输入：模块实物、原理图、卖家 STM32 示例、SSC、ESI、对象字典和当前工程。

动作：

- 建立 LAN9252 模块引脚表和卖家信号映射表；
- 确认模块工作在 SPI PDI 模式；
- 确认 PB3/PB4/PB5/PE3/PE4/PE5 的实际引出和电气占用；
- 确认 GD32 使用 SWD，未依赖 PB3/PB4 做 JTAG；
- 从卖家资料提取 SPI 模式、频率、复位、IRQ 和 EEPROM 配置；
- 检查当前 App Flash/RAM 余量；
- 确定卖家栈的授权、源文件边界和可提交内容。

产出：

- M8-0 硬件引脚确认表；
- M8-0 PDI/EEPROM 配置表；
- ESI/对象字典/PDO 基线；
- STM32 示例 HAL 依赖清单；
- Flash/RAM 预算记录；
- 未决风险清单。

停止条件：任何 SPI、RESET、IRQ 或 PDI 模式不明确时停止，不进入代码。

### M8-1：LAN9252 PDI Probe

实现范围：

    SPI2 初始化
    LAN9252 硬件复位
    寄存器读取
    芯片识别
    PDI 配置读取
    EEPROM 配置读取
    Link 状态读取

验收：

- 连续 Probe 1000 次，识别结果一致；
- 逻辑分析仪能看到正确的 CS、SCK、MOSI、MISO；
- Mode、位序、事务长度与卖家资料一致；
- RESET 后可以重新 Probe；
- SPI1 Flash 读写无回归；
- LAN9252 无响应时能超时退出并增加错误计数；
- ISR 中没有阻塞 SPI 或打印长日志。

### M8-2：DPRAM 与 IRQ

实现范围：

    DPRAM 写入
    DPRAM 读回
    IRQ GPIO
    EXTI 中断
    任务通知
    超时与错误恢复

验收：

- GD32 写入测试模式后能正确读回；
- IRQ 能唤醒 EtherCATTask；
- 异常 IRQ 电平不会造成无限中断或忙等；
- PDI 超时能恢复到可诊断状态；
- ISR 只做最小通知动作。

### M8-3：从站栈与 ESI

实现范围：

- 接入卖家 SSC 或等价从站栈；
- 接入 PDI、Timer、临界区、IRQ 和平台回调；
- 保留卖家对象字典和 ESI 的 Vendor ID、Product Code、Revision、PDO 索引；
- 只增加当前项目业务变量的映射；
- 让 EtherCATTask 能处理 INIT、PREOP、SAFEOP、OP 和错误恢复。

TwinCAT 验证顺序：

    网卡识别 → 扫描从站 → 加载 ESI → PREOP → SAFEOP → OP

每一步都记录主站日志、从站 AL 状态、错误码和必要的 WKC，不以“RJ45 Link 灯亮”作为从站状态成功的依据。

### M8-4：最小 PDO

第一版只验证：

    GD32 ADC0/ADC1 → TwinCAT
    TwinCAT DAC0/DAC1 → GD32

验收：

- 使用递增测试值时，ADC 变量连续变化且 sample_sequence 连续；
- TwinCAT 写入 DAC 命令后，GD32 的 DAC 输出或内部命令值变化；
- command_sequence 能识别重复命令和跳变；
- WKC 在稳定周期内连续有效；
- 先通过 10 ms，再测试 1 ms；
- 1 ms 测试失败时回退到 10 ms，并记录 CPU、SPI 和任务栈证据。

### M8-5：诊断数据

加入并验证：

    TF 卡存在/挂载/存储使能/满卡/剩余空间
    LED1~LED6 状态与闪烁阶段
    App/协议/构建信息
    运行时间与复位原因
    EtherCAT AL/Link/WKC/错误
    所有任务心跳与栈高水位
    HealthTask 总体健康状态

至少完成以下状态变化验证：

| 操作 | 预期诊断变化 |
|---|---|
| 拔出 TF 卡 | card_present、mounted、storage_enabled 和 LED5 按实际策略变化 |
| 插回 TF 卡 | 检测、挂载和存储状态按恢复流程变化 |
| 模拟满卡/剩余空间阈值 | storage_full 或空间告警变化 |
| 启动采样 | LED2 和采样状态变化 |
| 触发报警 | LED3 和 alarm_bits 变化 |
| 产生参数错误 | LED4 和错误项变化 |
| 增加任务负载 | 对应任务栈水位下降，HealthTask 仍可运行 |
| 修改固件版本 | 页面显示新的 App 版本/构建信息 |

### M8-6：ADS 桥接与 HTML

实现范围：

- 创建 TwinCAT 全局变量到桥接变量的映射表；
- ADS 读取快速 PDO 和低频诊断变量；
- 转换为固定 JSON 顶层结构；
- WebSocket 推送最新快照；
- HTML 实时刷新卡片、曲线、灯态、任务表和报警表；
- 实现断线重连和数据过期提示；
- 实现 DAC/继电器的范围检查、序号检查和写入结果反馈。

边界：

- 浏览器只处理 JSON；
- 浏览器不打开 EtherCAT 原始网卡；
- 桥接服务不绕过 TwinCAT 直接猜测 PDO 偏移；
- 所有 PDO 偏移和 ADS 变量名来自已验证的 ESI/TwinCAT 配置。

### M8-7：正式验收

固件：

- App 可编译、链接，Flash/RAM 预算有记录；
- SPI1 Flash、TF、OLED、ADC、DAC、USART/RS485 无回归；
- EtherCATTask 创建失败能返回明确状态；
- 所有任务栈水位和心跳可读；
- HealthTask 不被高优先级 EtherCATTask 饿死。

EtherCAT：

- TwinCAT 可重复扫描从站；
- 从站可从 INIT 进入 PREOP、SAFEOP、OP；
- PDO 数据连续且 WKC 稳定；
- 网线拔插后可恢复；
- TwinCAT 重启后可恢复；
- LAN9252 复位后可重新 Probe 并恢复通信；
- GD32 复位后可重新进入期望状态；
- Link、AL、PDI、邮箱错误可被记录和显示。

监控平台：

- HTML 能显示 ADC/DAC、TF、LED、版本、EtherCAT 和所有任务水位；
- 数据断开后显示 stale/offline，不继续显示为实时正常；
- 控制命令越界、重复和设备未就绪时被拒绝；
- WebSocket 重连后能重新获取完整快照；
- 页面时间戳、序号和设备运行时间含义不混淆。

## 12. 资源与风险控制

### 12.1 Flash/RAM

当前 App 分区约为 128 KiB。现有 map 文件显示 App 镜像约 90 KiB 量级；引入 SSC、对象字典、PDO 缓冲和新增任务后，必须重新完成链接尺寸检查，不能凭当前剩余空间推断“肯定够用”。

最低检查项：

- .text、.rodata、.data、.bss 大小；
- EtherCATTask 栈和静态缓冲；
- DPRAM/PDO/邮箱镜像的 RAM 占用；
- ESI/对象字典是否只在主机侧保存；
- App 分区边界是否仍满足 Bootloader 校验和升级约束；
- Keil AC5 与 EIDE 构建结果是否一致。

### 12.2 并发和所有权

- LAN9252 IRQ 只产生事件，EtherCATTask 消费事件；
- EtherCATTask 与 HealthTask 之间使用快照或受控接口；
- StorageTask 独占 FatFs；
- DisplayTask 独占 OLED/I2C；
- ControlTask 独占实际 DAC/继电器输出；
- 任何跨任务共享的多字节数据都必须有一致性保护；
- 不在 EtherCAT 周期路径执行文件写入、Flash 擦写或无界限日志。

### 12.3 主要风险

| 风险 | 影响 | 防护 |
|---|---|---|
| 模块实际 PDI 不是 SPI | 完全无法 Probe | M8-0 先核对 EEPROM/原理图 |
| SPI 引脚未引出或被占用 | 代码正确但硬件无通信 | 先做连通性和万用表/示波器确认 |
| PB3/PB4 调试复用冲突 | 调试或 SPI 不稳定 | 固定 SWD，确认无 JTAG 外部依赖 |
| 卖家栈接口与 GD32 不兼容 | 编译或状态机失败 | 只替换 HAL/平台层，保留栈和 OD |
| ESI/PDO 与固件不一致 | TwinCAT 扫描或 OP 失败 | 使用单一版本化 ESI/OD/PDO 基线 |
| EtherCATTask 优先级过高 | 其他任务饿死 | 心跳、栈水位、CPU 和周期测试 |
| 诊断直接读私有状态 | 数据竞争或破坏任务所有权 | 统一 snapshot 接口 |
| App 空间不足 | 无法链接或破坏升级边界 | M8-0 先做尺寸预算 |
| HTML 显示过期值 | 误判设备正常 | sequence、时间戳、stale 机制 |
| 普通网口误认为已支持 EtherCAT | 联调方向错误 | 先在 TwinCAT 中确认主站网卡和从站扫描 |

## 13. 版本、配置和变更管理

M8 的以下内容必须作为一个版本化集合管理：

    LAN9252 硬件引脚表
    PDI/EEPROM 配置
    ESI XML
    对象字典
    PDO 映射
    固件栈适配版本
    TwinCAT 工程变量映射
    ADS-WebSocket bridge 配置
    HTML JSON schema

任何一项变化都必须记录：变更原因、影响范围、旧版本、新版本、编译结果、TwinCAT 扫描结果和回归项目。不能只替换 ESI 或只修改 PDO 偏移而不更新另一端。

## 14. 阶段状态定义

后续报告必须按以下证据层级描述 M8 状态：

- **源码证据**：接口、任务、配置或文档已经存在；
- **构建证据**：目标工程成功编译/链接，尺寸和错误警告已记录；
- **PDI 运行证据**：逻辑分析仪、寄存器读回、Probe/DPRAM 测试通过；
- **EtherCAT 主站证据**：TwinCAT 扫描、状态机、PDO/WKC 通过；
- **板级证据**：真实模块、网线、网卡和传感器/执行器已验证；
- **监控平台证据**：ADS、WebSocket 和 HTML 页面实时数据与断线行为通过。

“文件已经创建”“驱动已编译”“网口 Link 灯亮”都不能单独等价于 EtherCAT 从站已接入或 HTML 已能显示真实数据。

## 15. 参考资料

- 当前项目任务书：Docs/01_PROJECT_OVERVIEW.md；
- 当前项目技术栈：Docs/02_TECH_STACK.md；
- 当前项目执行级开发流程：Docs/03_DEV_PROCESS.md；
- 当前 GD32F470 板级资源：BSP/board_config.h；
- 当前任务创建入口：Tasks/src/app_task.c；
- 当前任务健康监测：Tasks/src/health_task.c；
- 当前 TF/FatFs 诊断接口：Tasks/inc/storage_task.h；
- 当前 App/协议版本来源：App/inc/app_cli.h、App/src/app_cli.c、App/src/app_protocol.c；
- Microchip LAN9252 数据手册、PDI/SPI 示例和 EVB 资料；
- EtherCAT Technology Group 的 EtherCAT、ESC 和从站开发资料；
- Beckhoff TwinCAT EtherCAT 扫描、ESI 和 PDO 配置资料；
- 卖家提供的 LAN9252 原理图、EEPROM 配置、SSC、ESI、对象字典和 STM32 示例。

## 16. M8-0 开始前的执行清单

在下一轮代码实现前，按以下顺序完成：

1. 取得并打开 LAN9252 模块原理图，确认 SPI/RESET/IRQ 的实际引脚；
2. 用万用表确认模块 3.3 V、GND 和接口引脚没有短路或反接；
3. 将 PB3/PB4/PB5/PE3/PE4/PE5 与 GD32 板实际排针逐一对应；
4. 用 SWD 方式连接调试器，确认不依赖 PB3/PB4 的 JTAG；
5. 记录 EEPROM/PDI 模式、SPI Mode、最高频率和 IRQ 极性；
6. 从卖家示例提取最小 Probe 调用链；
7. 检查当前 App 的 Flash/RAM 余量；
8. 只在以上结果齐全后，创建 board_lan9252 和 ecat_pdi 的首个最小实现；
9. 先通过 PDI Probe，再进入 DPRAM、SSC、TwinCAT、PDO 和 HTML 阶段。

本清单完成前，禁止把普通 TCP/HTTP 联通、RJ45 Link 灯亮或浏览器能打开页面当成 EtherCAT 接入成功。
