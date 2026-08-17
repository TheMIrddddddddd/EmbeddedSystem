# 工业数据采集终端 —— 技术栈详解

> 文档版本:V1.0 | 日期:2026-08-09
> 定位:本项目用到的每一项技术的**原理、在项目中的具体用途、简历价值**。
> 与《01_PROJECT_OVERVIEW.md》(做什么)配套,本文回答"用什么做、为什么"。

---

## 一、总览

```
┌─────────────────────────────────────────────────────────┐
│  PC 工具层    Python 自动测试脚本                        │
├─────────────────────────────────────────────────────────┤
│  软件架构层    FreeRTOS 多任务 | Bootloader 裸机状态机   │
│                | Tasks/Application → Domain Services     │
│                | → Protocol/Middleware → BSP/Drivers     │
├─────────────────────────────────────────────────────────┤
│  公共/中间件    Common 契约 | FatFs | RingBuffer          │
│                | CRC16/CRC32 | Flash KV | CLI Shell      │
├─────────────────────────────────────────────────────────┤
│  外设驱动层    USART×2 | RS485 | SPI | I2C | SDIO        │
│                | ADC-DMA | DAC | RTC | PMU | FMC | GPIO  │
├─────────────────────────────────────────────────────────┤
│  固件库        GD32F4xx 标准外设库(官方 V3.3.3)          │
├─────────────────────────────────────────────────────────┤
│  硬件平台      GD32F470VET6 (Cortex-M4F, 240MHz)         │
└─────────────────────────────────────────────────────────┘
```

---

## 二、硬件平台层

### 2.1 GD32F470VET6(主控)

| 项目 | 参数 | 说明 |
|---|---|---|
| 内核 | ARM Cortex-M4F | 带 FPU(浮点单元),硬件单精度浮点运算 |
| 主频 | 240MHz | 25MHz 外部晶振 PLL 倍频 |
| Flash | 512KB | 分 Boot/参数/App/备份/暂存 5 区 |
| RAM | 256KB(192KB SRAM + 64KB TCM) | **DMA 缓冲(ADC/USART/SDIO)只能放普通 SRAM,不能放仅 CPU 访问的 TCM SRAM**(DMA 不可达 TCM) |
| 外设资源 | 6×USART、3×SPI、2×I2C、2×ADC(16通道)、2×DAC、SDIO、RTC、USB | 满足全部项目需求 |

**简历价值:** "基于 GD32F470(Cortex-M4F @240MHz)开发",体现国产 MCU 开发能力(工业界越来越普遍)。

### 2.2 GD32F4xx 标准外设库(固件库)

- 与 STM32 标准库同源思路,但 API 按 GD32 寄存器设计(`gpio_mode_set` / `usart_baudrate_set` / `rcu_*`)
- 项目使用官方 V3.3.3 版本
- **面试对比点**:能讲清 GD32 与 STM32 标准库的差异(结构体初始化 vs 逐函数配置)

---

## 三、外设驱动层

### 3.1 USART1 + MAX3485(RS485 业务主通信口)

- **原理**:USART1 的 TX/RX 经 MAX3485 转成 RS485 差分信号(A/B 两根线),半双工,工业长距离(1200m)多点通信
- **项目用途**:跑业务协议(自定义帧 / Modbus RTU),连接上位机与 PLC
- **关键技术点**:
  - **USART DMA 循环接收** + **IDLE 空闲中断**:串口数据自动进内存,出现总线空闲时触发中断(注意:IDLE 不保证"一帧一次中断",半帧/多帧仍需 RingBuffer 解析器处理);避免逐字节中断开销
  - **DE 方向控制**:半双工,发送时拉高 DE,发送完成中断后拉低
  - 波特率 19200 默认(2026 赛题规定)
- **简历价值**: "USART DMA + IDLE 中断 + RS485 的工业总线通信实现"

### 3.2 USART0 + CH340C(调试口)

- **原理**:USART0 接 CH340C 转 USB,虚拟串口连 PC
- **项目用途**:CLI 调试命令行、printf 日志输出
- **简历价值**: "调试串口与命令行交互设计"

### 3.3 ADC + DMA(模拟量采集)

- **原理**:ADC 采样电位器电压,DMA 自动搬运到内存,不占 CPU
- **项目用途**:CH0 通道**底层 100ms 常驻采集**(ADC DMA),连续 3 次取均值滤波;CLI 打印/TF 存储周期(5/10/15s)由软件定时器独立控制
- **关键技术点**:
  - DMA 循环模式 + 双缓冲
  - 多通道扫描(为 CH2 扩展预留)
  - 12 位分辨率 → 3.3V 量程换算
- **简历价值**: "ADC-DMA 减少 CPU 搬运与逐点中断开销,常驻连续采样 + 均值滤波"

### 3.4 DAC(输出)

- **原理**:DAC 输出可编程电压(PA4)
- **项目用途**:CH1 信号源——设定 DAC 值 → PA4 跳线接 PC1 → ADC 回读 → 验证"输出-采集闭环"
- **简历价值**: "DAC 输出-回读闭环校验链路设计"

### 3.5 SPI + GD25Q40E(外部 Flash)

- **原理**:SPI 同步串行总线,读写 GD25Q40E NOR Flash(4Mbit/512KB)
- **项目用途**:板载外部存储;APP 阶段保存参数/告警记录,后续可按升级状态机保存升级包或备份镜像;APP 中由 StorageTask 承载的 PersistenceService 访问,Bootloader 只在升级阶段按需访问
- **器件规格**:4KB 扇区、256B 页;地址范围 `0x000000~0x07FFFF`;`0x9F` JEDEC ID 预期为 `0xC84013`,实测读回待完成
- **启动边界**:GD25Q40E 通过 SPI1 连接,只作为外部存储介质;Bootloader 与 App 的执行映像位于 GD32F470 内部 Flash,不从 GD25Q40E 直接复位启动
- **关键技术点**:
  - **Flash KV 存储**:追加式写入 + CRC 校验 + 提交标志,掉电不损坏
  - 扇区擦除/页写入(4KB 扇区)
  - ControlTask/AlarmTask 只发送异步持久化请求,不得直接擦除、GC 或持有 SPI 锁
- **简历价值**: "SPI 总线外设驱动 + Flash KV 参数管理(掉电一致性)"

### 3.6 I2C + SSD1306(OLED)

- **原理**:I2C 总线驱动 SSD1306 OLED 控制器(0.91 英寸,128×32)
- **项目用途**:双行状态显示(第一行队伍编号,第二行 Bootloader/AutoSample/IDLE)
- **简历价值**: "I2C OLED 显示驱动"

### 3.7 SDIO + TF 卡

- **原理**:SDIO 高速接口读写 TF 卡(比 SPI 模式快)
- **项目用途**:FatFs 文件系统底层接口 + 离线升级固件读取
- **关键技术点**:
  - SDIO 4 位模式
  - 拔卡检测 / 无卡降级运行
- **简历价值**: "SDIO 驱动 TF 卡 + FatFs 文件系统"

### 3.8 RTC(实时时钟)

- **原理**:独立时钟域,靠 32.768kHz 晶振(OSC32K)走时
- **项目用途**:① 时间戳(UTC)与文件命名 ② 低功耗睡眠定时唤醒(闹钟)
- **简历价值**: "RTC 时间服务 + 闹钟唤醒低功耗"

### 3.9 PMU(电源管理)

- **原理**:GD32 电源管理单元,支持多种低功耗模式
- **项目用途**:深度睡眠(Deepsleep)+ RTC 闹钟 10s 自动唤醒
- **简历价值**: "深度睡眠 + 定时唤醒的低功耗管理"

### 3.10 FMC(Flash 控制器)

- **原理**:内部 Flash 擦写控制器
- **项目用途**:① 内部参数区读写 ② Bootloader 固件搬运(从 TF/GD25Q40E 等外部暂存介质读取镜像→写内部 App 区)
- **简历价值**: "内部 Flash 分区管理与固件搬运"

### 3.11 GPIO + EXTI(通用 IO 与外部中断)

- **原理**:GPIO 控制电平,EXTI 边沿触发中断
- **项目用途**:LED 状态指示、按键输入(消抖)、RS485 DE 控制
- **简历价值**: "GPIO 输出 + EXTI 中断输入 + 软件消抖"

---

## 四、中间件层

### 4.1 FreeRTOS(实时操作系统)

- **原理**:抢占式实时内核,任务/队列/信号量/互斥锁/事件组/软件定时器
- **项目用途**:APP 主架构,7 任务调度(Protocol/Sample/Storage/Alarm/Control/Display/Health,详见《01_PROJECT_OVERVIEW.md》第十六章)
- **简历价值**: "基于 FreeRTOS 的多任务架构,队列/事件组/互斥锁解耦模块"

### 4.2 FatFs(文件系统)

- **原理**:开源的 FAT 文件系统模块(FAT12/16/32),与底层 SDIO 解耦
- **项目用途**:TF 卡目录管理(采样/告警/日志/配置/固件)
- **简历价值**: "移植 FatFs 实现嵌入式文件系统 + 掉电安全写盘"

### 4.3 RingBuffer(环形缓冲区)

- **原理**:定长数组 + 头尾指针循环利用,FIFO 语义
- **项目用途**:USART DMA 接收缓存,解决粘包/拆包;**容量 2048B**(容纳一个最大帧 1040B 并留有余量)
- **简历价值**: "环形缓冲区处理流式数据"

### 4.4 CRC16-Modbus / CRC32

- **原理**:循环冗余校验,多项式除法
- **项目用途**:
  - CRC16-Modbus:通信帧校验(自定义帧 + Modbus RTU 都用它)
  - CRC32:固件完整性校验、Flash KV 记录校验
- **简历价值**: "CRC 校验保证通信与固件完整性"

### 4.5 Flash KV 参数存储

- **原理**:轻量级键值存储,追加式写记录(记录头+数据+CRC+提交标志)
- **架构归属**:FlashKV 是 Middleware 存储引擎,上层通过 PersistenceService 使用;APP 的实际读写/擦除/GC 全部在 StorageTask 单一执行上下文完成
- **垃圾回收(双扇区,掉电安全,强制)**:
  ① 旧扇区有效(继续读)
  ② 新扇区写入最新配置 → CRC 校验
  ③ 写提交标志
  ④ 切换活动扇区(指针)
  ⑤ 最后擦除旧扇区
  (提交标志只保护单条记录,不能保护"擦除整扇区"过程,必须双扇区轮换)
- **GD25Q40E 候选用途/分区方向(具体地址待 M5/M6 实现时定稿)**:

> 下表只表达当前项目的粗略存储方向,不是当前硬件验收依据,也不是已经冻结的最终地址。参数/告警在 M5 实现时确定,升级包/备份镜像在 M6 升级流程实现时确定;升级包不进入 Flash KV 的 GC 管理。

| 扇区 | 内容 |
|---|---|
| 0x000000~0x001FFF | 业务配置 KV(sector 0/1 两个 4KB 扇区轮换) |
| 0x002000~0x002FFF | 告警记录(最近 10 条) |
| 0x003000~0x07FFFF | 上电计数、升级包、备份镜像或其他预留(具体用途和边界待 M5/M6 确定) |

- **项目用途**:设备参数掉电持久化
- **简历价值**: "自研 Flash KV 存储,双扇区 GC 掉电一致性设计"

### 4.6 CLI Shell(命令行)

- **原理**:字符串解析 + 命令表分发
- **项目用途**:USART0 调试指令(test/conf/ratio/limit/start/stop/rtc)
- **简历价值**: "命令行接口支持系统自检与配置"

### 4.7 ebtn 按键状态机

- **原理**:事件驱动的按键状态机库——自动完成去抖、单击/长按/保持识别,以事件回调(ONPRESS/ONCLICK/KEEPALIVE)通知应用层;支持组合键
- **项目用途**:全部 6 个按键的事件识别与分发;与多级菜单解耦(ebtn 管"按键是什么事件",菜单管"事件怎么响应")
- **内部结构**(面试可讲):
  - 静态按键:结构体数组(编译期固定)
  - 动态按键:链表(`ebtn_btn_dyn_t`,用户静态定义节点,`ebtn_register` 只串链不分配内存)
- **简历价值**: "事件驱动按键状态机(去抖/单击/长按/组合键),与菜单导航解耦"

---

## 五、软件架构层

### 5.1 Bootloader(裸机状态机)

- **原理**:独立小程序,负责启动 App 和固件升级;用状态机组织升级流程,不跑 RTOS(越简单越可靠)
- **运行位置**:Bootloader 与 App 都位于 GD32F470 内部 Flash;GD25Q40E 不作为复位后的直接执行区
- **项目用途**:上电跳转 + RS485 在线 IAP + TF 卡离线升级 + 可选 GD25Q40E 升级包/备份镜像 + 双槽元数据 + 备份回滚
- **公共契约**:Bootloader 与 APP 必须共同引用 `Common/` 中的 Flash 分区、固件格式、升级元数据、CRC 参数、规范化复位原因与错误码;持久化结构逐字段编解码,禁止两工程复制定义或直接 `memcpy` C 结构体
- **初始化策略**:时钟、看门狗、升级元数据、FMC、RS485、SDIO/FatFs 等按状态机需要显式初始化;Bootloader 禁止 section 自动注册
- **可靠性约束**:RECEIVING/STAGED_VALID/INSTALLING/TRIAL_PENDING/CONFIRMED/ROLLBACK 状态可恢复;离线成功包与失败包使用版本+CRC 唯一文件名隔离;64KB Bootloader 只是当前候选容量,最终门槛以 M1 实际构建结果为准
- **安全边界**:项目假设 RS485 上位机和 TF 介质可信,CRC32 仅检测传输/存储损坏,不提供身份认证或 Secure Boot;V1 头保持 32B,未来签名能力通过 `package_version + header_size` 引入不兼容的 V2 扩展头
- **简历价值**: "自研 Bootloader:双槽元数据、掉电续做、启动确认与失败回滚"

### 5.2 分层架构

- **唯一依赖模型**:`Tasks/Application → Domain Services → Protocol/Middleware → BSP/Device Drivers → GD32 SPL/CMSIS`,只允许单向依赖
- **层级职责**:
  - Tasks/Application:七任务执行上下文、业务用例编排;ControlTask 决定“做什么”,不亲自执行文件、Flash GC、协议编码或显示 IO
  - Domain Services:Persistence、Display、Indicator、PowerManager、TimeService 等跨中间件/硬件能力
  - Protocol/Middleware:自定义协议/Modbus、FatFsAdapter、FlashKV、RingBuffer、CRC、ebtn、CLI
  - BSP/Device Drivers:GPIO/DMA/UART、SDIO 块读写、SPI Flash 原始读写、RTC 原始读写和最终硬件低功耗入口
  - Common:Boot/App 横向共享的纯契约模块,不依赖 FreeRTOS/FatFs/UI/GD32 寄存器,不改变上述运行时依赖方向
- **初始化策略**:APP 核心路径显式初始化;section 自动注册仅允许非关键可选 APP 模块且默认关闭,AC5/GCC 链接脚本分别使用 `KEEP` 防止 LTO/段回收
- **可移植性边界**:同 GD32F470 系列且外设能力一致的换板主要调整 `board_config.h + board_dma_map.h`;跨芯片系列仍需适配时钟、中断、DMA、启动文件和 BSP 后端
- **简历价值**: "Common+BSP+Middleware+Service+Task 单向分层,共享契约防止 Boot/App ABI 漂移"

### 5.3 有限状态机(FSM)

- **原理**:状态 + 事件 → 迁移,程序行为可预测
- **项目用途**:① 协议解析(逐字符状态机) ② Bootloader 升级流程 ③ OLED 显示状态
- **简历价值**: "有限状态机驱动协议解析与升级流程"

---

## 六、PC 工具层

### 6.1 Python + pyserial(自动测试)

- **原理**:PC 端脚本发串口帧、收应答、自动断言
- **项目用途**:协议回归测试(正常帧/错误帧/异常帧批量验证)、升级流程自动化
- **简历价值**: "Python 自动化测试:协议一致性验证与回归测试"

### 6.2 Keil + EIDE 双工具链

- **原理**:Keil(AC5)+ VSCode EIDE(GCC)两套编译链,共用源码
- **项目用途**:Bootloader 用 Keil 主力,App 双工具链开发
- **简历价值**: "多工具链协作开发(Keil + EIDE/GCC)"

---

## 七、数据结构应用

> 本项目数据结构的使用原则:**优先静态分配,避免裸机动态 malloc**(防内存碎片与泄漏)。
> 链表在 RTOS 内核与文件系统中自然存在;应用层用"数组 + 索引池"模拟链表,保证确定性。

### 7.1 环形缓冲区(RingBuffer)

- **位置**:RS485 接收链路(USART1 DMA → 环形缓冲 → 协议解析)
- **作用**:DMA 循环写接收缓冲区,IDLE/半满/满事件只更新生产者位置并通知任务;ProtocolTask 再按生产者/消费者索引解析字节流。无需逐字节搬移,可处理**粘包**(一次收到多帧)与**拆包**(一帧分多次到达)
- **关键设计**:容量 2048B(容纳一个最大帧 1040B 并留有余量),读/写指针原子操作(单生产者单消费者无需锁)
- **简历话术**: "环形缓冲区解耦中断与协议解析,零拷贝处理流式数据"

### 7.2 消息队列(FreeRTOS Queue)

- **位置**:任务间数据传递
  - 采样任务 → 存储任务:采样结果队列
  - 采样任务 → 告警任务:超限事件队列
  - ProtocolTask → ControlTask:命令请求队列(含 request_id/来源/协议序列号)
  - ControlTask → ProtocolTask:执行完成/异步应答队列(回显 request_id/协议序列号)
  - DisplayTask/按键 → ControlTask:菜单导航事件
  - ControlTask → StorageTask:配置读取/保存、KV、审计、flush 等 Persistence 请求
  - StorageTask → ControlTask:配置读取/写盘/睡眠前 flush 完成消息
  - ControlTask → SampleTask:采样周期修改
  - AlarmTask → ProtocolTask:主动告警上报事件
  - AlarmTask → StorageTask:TF/Flash 告警持久化请求
  - HealthTask → ControlTask:睡眠前健康检查完成/故障状态
- **消息协议**:跨任务请求携带 32 位单调 `request_id`、操作类型、来源和 deadline;完成消息回显关联 ID。ControlTask 使用固定 pending 表异步协调,禁止同步等待其他任务、持锁等待、无界等待或传递任务栈指针
- **作用**:生产者/消费者解耦,任务阻塞等待,不忙轮询;StorageTask 的长 IO 使用 **busy-but-alive** 契约,另通过原子健康槽向 HealthTask 报告 busy/progress/deadline
- **简历话术**: "消息队列实现任务间解耦与流量缓冲"

### 7.3 事件组(FreeRTOS EventGroup)

- **位置**:系统级状态广播
- **作用**:升级中/采样中/睡眠中/告警中 等状态位,多任务可按位等待、组合判断(如"采样中且告警中")
- **简历话术**: "事件组实现跨任务状态同步"

### 7.4 互斥锁(FreeRTOS Mutex)

- **使用原则**:优先用单所有者任务消除跨任务总线竞争;只有资源确有多个并发客户端且不能归并所有权时才使用互斥锁
- **当前所有权**:
  - GD25Q40E/SPI:APP 侧由 StorageTask 单一所有者;Bootloader 仅在升级状态机阶段独占访问,不向业务公开 `bsp_spi_lock/unlock`,无需并行访问
  - OLED/I2C:DisplayTask 单一所有者,其他任务只提交显示模型
  - RS485/调试串口发送:若存在多个发送入口,由 ProtocolTask/ControlTask 内部串行化;必要时使用带优先级继承的发送互斥锁
- **作用**:互斥锁只保护无法通过所有权消除的短临界资源,不得在持锁期间等待队列、文件 IO 或跨任务完成消息
- **简历话术**: "单所有者任务优先,必要时用优先级继承互斥锁保护短临界资源"

### 7.5 链表(FreeRTOS 内核内部)

- **位置**:FreeRTOS 内核本身
  - 就绪任务链表(按优先级)
  - 延时任务链表
  - 队列/信号量等待链表
- **作用**:任务调度、延时、阻塞的全部数据结构基础——**双向链表**
- **简历话术**: "理解 FreeRTOS 内核链表结构(就绪表/延时表),能分析调度行为"

### 7.6 结构体数组命令表(CLI/协议)

- **位置**:CLI 指令表、协议命令字表
- **作用**:

```c
/* CLI 命令表(USART0 调试用,独立于协议表) */
typedef struct {
    const char *cmd;            /* 指令名: "ratio" */
    void      (*handler)(void); /* 处理函数指针 */
} cli_cmd_table_t;

/* RS485 协议命令表(独立,面向二进制帧) */
typedef struct {
    uint16_t    cmd_code;       /* 协议命令字(2 字节) */
    void      (*handler)(const uint8_t *data, uint16_t len);
} proto_cmd_table_t;
/* 两个表分开维护,业务层复用服务函数 */

/* CLI 命令表示例(与协议表分开) */
static const cli_cmd_table_t cli_table[] = {
    { "ratio", ratio_handler   },
    { "limit", limit_handler   },
    { "start", start_handler   },
    ...
};

/* RS485 协议命令表示例(独立) */
static const proto_cmd_table_t proto_table[] = {
    { 0x0201, query_ch0_handler },
    { 0x0402, set_ch0_limit_handler },
    ...
};
```

- **优势**:增删指令只需加一行表项,比 switch-case 可维护;查表用遍历(条目少)或二分(排序后)
- **强制**:CLI 命令表与 RS485 协议命令表**分开维护**(两个表),只在业务层复用服务函数——CLI 面向调试文本,协议面向二进制帧,避免耦合
- **简历话术**: "函数指针数组实现命令分发表,CLI 与协议命令表解耦"

### 7.7 Flash KV 记录(参数持久化)

- **位置**:参数区存储
- **结构**:`记录头 | 参数版本 | 参数内容 | CRC32 | 提交标志`
- **作用**:追加式写入,启动时顺序扫描最后一条有效记录;活动扇区写满后,将最新有效记录迁移到备用扇区并校验 CRC,最后写提交标志并切换活动扇区,确认新扇区有效后才擦除旧扇区;提交标志保护单条记录,双扇区迁移保护 GC 全过程
- **简历话术**: "追加式 KV 存储 + 双扇区 GC + 提交标志,实现掉电一致性"

### 7.8 FAT 目录树(FatFs 内部)

- **位置**:TF 卡文件系统
- **作用**:FAT 表(链式簇索引)+ 目录项(文件树)。文件追加写即链式查找下一空闲簇
- **简历话术**: "理解文件系统底层数据结构(FAT 链/目录树),能解释文件读写路径"

### 7.9 静态对象池(替代动态链表)

- **位置**:帧缓冲池、告警记录池(最近 10 条)
- **作用**:预分配固定大小数组 + 空闲索引链表,分配/释放 O(1) 且无碎片——裸机嵌入式标准做法
- **简历话术**: "预分配对象池替代动态内存,确定性分配,无内存碎片"

---

## 八、面试技术栈一句话版

> **Cortex-M4F 裸机 Bootloader + FreeRTOS 七任务 APP | Common/BSP/Middleware/Service 单向分层 | USART-DMA/RS485、SPI、I2C、SDIO、ADC/DAC、RTC、PMU | FatFs、RingBuffer、CRC、Flash KV、CLI | 在线 IAP + TF 离线升级 + 启动确认回滚 | Python 自动化测试**
>
> **数据结构**:环形缓冲区(串口流)、消息队列/事件组/互斥锁(RTOS 同步)、函数指针命令表(CLI/协议)、Flash KV 记录(持久化)、静态对象池(帧/告警缓冲)
