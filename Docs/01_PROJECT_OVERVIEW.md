# 工业数据采集终端 —— 项目任务书(赛题式)

> 文档版本:V1.0 | 日期:2026-08-09
> 说明:本文件为项目的完整任务书,定义**每一项功能的输入、输出、行为、联动与验收标准**。
> 开发时逐条实现,验收时逐条对照。技术选型见《02_TECH_STACK.md》。

---

## 一、任务细则

借鉴 CIMC 赛题业务场景,自主设计并实现一个基于 GD32F470VET6 的工业数据采集终端,支持电压采集、处理、显示、存储、通信、告警、低功耗与固件升级功能。**不以官方自动评测兼容为目标**,重点展示 FreeRTOS 架构、可靠存储、RS485 协议、Bootloader 升级与故障恢复能力。

全文统一采用一套逻辑依赖模型:`Tasks/Application → Domain Services → Protocol/Middleware → BSP/Device Drivers → GD32 SPL/CMSIS`。Tasks 是 APP 的执行上下文,Domain Services 承载采样/配置/告警/显示/持久化/电源等业务能力,Protocol/Middleware 提供协议编解码、FatFs 适配、Flash KV、ebtn、RingBuffer 等通用机制,BSP 只封装纯硬件访问。依赖必须单向向下,禁止业务层直接调用 GD32 标准库。

**通信资源约定:**

| 串口 | 物理链路 | 用途 |
|---|---|---|
| USART0 + CH340C | USB 转串口(调试口) | CLI 指令交互、调试日志(以下"串口"均指此口) |
| USART1 + MAX3485 | RS485 总线 | 业务协议通信(二进制帧 / Modbus RTU) |

---

## 二、系统功能

### 1、系统自检

利用命令实现系统的自检,通过串口输出自检结果。

**具体要求:** 通过串口输入 `test` 指令,开始进行系统自检,串口返回自检结果。自检项及判定标准:

| 自检项 | 通过标准 | 失败表现 |
|---|---|---|
| Flash | 能读取到外部 Flash ID(GD25Q40E,预期 JEDEC ID=0xC84013) | Flash ID 读取失败 |
| TF Card | TF 卡存在且能挂载 | 未检测到 TF 卡 |
| OLED | I2C 通信正常 | OLED 初始化失败 |
| RTC | 时间有效(非 2000 年 1 月 1 日 0 点默认值) | RTC 未初始化 |

**操作示例:**

> 以下 Flash ID 为 GD25Q40E 的设计预期值,实际板级 ID 需在 SPI 驱动完成后读回确认。

```
串口输入: test
串口返回:
=== System Test ===
Flash ID   : 0xC84013  [PASS]
TF Card    : Found     [PASS]
OLED       : OK        [PASS]
RTC        : 2026-08-09 12:30:45 [PASS]
=== Test Result: PASS ===
```

**失败样例**(TF 卡未插入时):

```
串口输入: test
串口返回:
=== System Test ===
Flash ID   : 0xC84013  [PASS]
TF Card    : Not Found [FAIL]
OLED       : OK        [PASS]
RTC        : 2026-08-09 12:30:45 [PASS]
=== Test Result: FAIL ===
```

### 2、时间设置

通过串口指令 `rtc config` 设置基准时间,更新至 RTC 模块并反馈结果。

**具体要求:** 通过串口一次性输入标准时间(例如:2026 年 08 月 09 日 12:00:30),串口返回设置结果。时间格式中有无年月日等分割文字或符号不影响,输入 `2026-08-09 12:00:30` 亦可。

**操作示例:**

```
串口输入: rtc config
串口返回: Please input time (YYYY-MM-DD HH:MM:SS):
串口输入: 2026-08-09 12:00:30
串口返回: RTC set success: 2026-08-09 12:00:30
```

**错误处理:** 输入格式非法(缺字段、数字越界如 13 月 99 日、秒 60)时,返回:

```
串口返回: RTC set failed: invalid time format
```

时间格式不改变,保留原值。

### 3、时间显示

通过串口指令 `rtc now` 显示当前时间。

**操作示例:**

```
串口输入: rtc now
串口返回: 2026-08-09 12:30:45
```

---

## 三、配置管理

### 1、读取配置

从 TF 卡的文件系统中读取 `config.ini` 文件,更新变比、阈值、采样周期、设备 ID 与协议模式至 Flash;若文件不存在则返回"无此文件"。

**具体流程:** 串口输入 `conf`:

- 文件系统中没有该文件:

```
串口输入: conf
串口返回: config file not found
```

- 读取到该文件,解析并写入 Flash:

```
串口输入: conf
串口返回:
config.ini loaded:
  device_id : 0001
  sample_period : 5
  ch0_ratio : 1.00
  ch0_limit : 2.50
  ch1_ratio : 1.00
  ch1_limit : 2.50
  protocol_mode : 0
config saved to flash [OK]
```

`config.ini` 文件格式(放在 TF 卡 `/config/config.ini`,UTF-8 无 BOM,键值用 `=`):

```
# 工业数据采集终端配置文件
device_id=0001
sample_period=5
ch0_ratio=1.00
ch0_limit=2.50
ch1_ratio=1.00
ch1_limit=2.50
protocol_mode=0
alarm_mode=2(01 主动上报 / 02 被动存储,默认 02)
```

**错误处理(原子提交,强制):**
- 读取整个文件 → 解析到**临时结构体** → 全部字段及字段间约束校验(值域/范围/互斥)
- 任一行解析失败 → **整组配置不生效**,保持原运行配置,返回 `config parse error at line N`(避免"设备 ID 已改、协议模式未改"的半更新状态)
- 全部校验通过 → 一次性替换 RAM 运行配置 → 原子写入 Flash(单条记录含整体 CRC)
- TF 卡未插入 → 返回 `TF card not found`

### 2、变比设置

通过指令实现变比设置,需验证输入有效性(负值、超量程)。

**具体流程:** 输入 `ratio`,先读取原有变比值,再提示输入新的变比,有效值范围为 0~100,类型为浮点数。需指定通道(CH0/CH1):

```
串口输入: ratio ch0
串口返回:
current ch0 ratio: 1.00
please input new ratio (0-100):
串口输入: 5.5
串口返回: ch0 ratio set to 5.50 [OK]
```

**错误处理:** 输入无效值(负数、>100、非数字):

```
串口输入: -1
串口返回: parameter invalid, ratio unchanged
```

变比值不改变,保持原值。

### 3、阈值设置

通过指令设置警报阈值,需验证输入有效性。

**具体流程:** 输入 `limit`,先读取原阈值,再提示输入新阈值,有效值范围为 0~500,浮点数。需指定通道:

```
串口输入: limit ch1
串口返回:
current ch1 limit: 2.50
please input new limit (0-500):
串口输入: 10.5
串口返回: ch1 limit set to 10.50 [OK]
```

**错误处理:**

```
串口输入: 501
串口返回: parameter invalid, limit unchanged
```

### 4、参数存储

将参数存储至外部 Flash,掉电后参数保持。

**具体流程:**

- 输入 `config save`,先读出当前参数打印,再存储至 Flash:

```
串口输入: config save
串口返回:
device_id=0001  sample_period=5
ch0_ratio=5.50  ch0_limit=2.50
ch1_ratio=1.00  ch1_limit=10.50
protocol_mode=0 alarm_mode=2(01 主动上报 / 02 被动存储,默认 02)
save to flash [OK]
```

- 输入 `config read`,从 Flash 读出参数并打印:

```
串口输入: config read
串口返回:
device_id=0001  sample_period=5
ch0_ratio=5.50  ch0_limit=2.50
ch1_ratio=1.00  ch1_limit=10.50
protocol_mode=0 alarm_mode=2(01 主动上报 / 02 被动存储,默认 02)
```

**要求:** 断电重启后执行 `config read`,参数必须与断电前一致(验收项 F)。

**存储分域(强制,两个存储域职责明确):**

| 存储域 | 存放内容 | 访问方 |
|---|---|---|
| **外部 SPI Flash(GD25Q40E,4Mbit/512KB)** | 业务配置(变比/阈值/采样周期/设备ID/波特率/协议模式)、告警记录(最近 10 条)、上电次数计数、**升级状态元数据(固定双槽,M6)**、后续可选保存升级包/备份镜像 | APP 的 StorageTask 负责参数/告警;Bootloader 仅在升级状态机阶段按需独占访问元数据槽/升级包/备份镜像 |
| **内部 Flash 保留区(0x08010000~0x08011FFF,2 × 4KB 独立页:slot A / slot B)** | **已废弃(仅保留 M1 布局占位)**:实测两个烧录工具(OpenOCD stm32f2x 与 Keil GD32F4xx_512KB.FLM)烧 App 时均按整扇区擦除,该区与 App 同处扇区 4,元数据必被连擦 → 升级状态转存 GD25Q40E 固定元数据槽,此区退役 | (无访问方) |

**规则:**
- 业务参数走外部 Flash(独立存储、擦写不占用内部空间);Bootloader 和 App 的可执行映像仍在 MCU 内部 Flash;升级状态(元数据/试运行状态/回滚计数)存 GD25Q40E 固定元数据双槽,Bootloader 在升级状态机阶段经 SPI1 独占访问(SPI 原始驱动由 M3 提供)
- APP 运行配置与升级状态互不干扰;升级固件不会触碰业务参数区
- ControlTask/AlarmTask 只能向 StorageTask 发送持久化请求,不得直接擦除 GD25Q40E、执行 Flash KV GC 或持有 SPI 锁;Bootloader 访问外部 Flash 只允许发生在升级状态机的明确阶段

### 5、协议模式设置

通过指令切换 RS485 通信协议(0=自定义帧协议 / 1=Modbus RTU)。

**具体流程:** 输入 `protocol`:

```
串口输入: protocol
串口返回:
current protocol: 0 (custom)
please input protocol mode (0=custom, 1=modbus):
串口输入: 1
串口返回: protocol set to 1 (modbus), saved [OK]
```

切换后 RS485 口立即按新协议解析帧;重启后保持(持久化至 Flash)。

### 6、设备 ID 设置

通过指令设置设备 ID(有效范围 0001~FFFE,十六进制)。

**具体流程:** 输入 `id`:

```
串口输入: id
串口返回:
current device id: 0001
please input new id (0001-FFFE):
串口输入: 0008
串口返回: device id set to 0008, saved [OK]
```

**错误处理:** 输入 `0000` 或 `FFFF`、非十六进制字符 → `parameter invalid, id unchanged`。

### 7、波特率设置

通过指令设置 RS485 口波特率(9600 / 19200 / 38400 / 57600 / 115200)。

**具体流程:** 输入 `baud`:

```
串口输入: baud
串口返回:
current baudrate: 19200
please input baudrate (9600/19200/38400/57600/115200):
串口输入: 115200
串口返回: baudrate set to 115200, saved [OK]
```

**注意:** 修改后立即生效,上位机需同步修改波特率才能继续通信;重启后保持。

---

## 四、采样控制

### 1、采样启停(串口指令)

通过指令 `start` / `stop` 控制采样过程。

**启动采样** 输入 `start`,进入周期采样模式(默认周期 5s),每 5 秒串口输出一条采样数据:

```
串口输入: start
串口返回:
2026-08-09 12:30:45  CH0=1.25V  CH1=3.30V
2026-08-09 12:30:50  CH0=1.26V  CH1=3.30V
2026-08-09 12:31:00  CH0=1.24V  CH1=3.30V
```

(通道电压值保留小数点后两位)

**停止采样** 输入 `stop`,停止周期采样,LED 状态切换:

```
串口输入: stop
串口返回: sampling stopped
```

### 2、采样启停(按键)

通过按下 KEY1 控制采样过程,按下后状态翻转:

| 当前状态 | 按下 KEY1 后 |
|---|---|
| 停止 | 启动采样 |
| 采样中 | 停止采样 |

内容显示与串口指令方式一致。

### 3、周期调整(按键)

通过按键 KEY2 / KEY3 / KEY4 动态修改采样周期(5s / 10s / 15s),配置需持久化(断电重启后生效):

| 按键 | 采样周期 | 串口输出 |
|---|---|---|
| KEY2 | 5s | `sample period set to 5s, saved [OK]` |
| KEY3 | 10s | `sample period set to 10s, saved [OK]` |
| KEY4 | 15s | `sample period set to 15s, saved [OK]` |

### 4、超限提示

当采样值超过对应通道的 `limit` 设置时:

| 动作 | 行为 |
|---|---|
| LED3 | 点亮(超限指示) |
| 串口(CLI) | 打印 `OverLimit` 字样和具体阈值 |

```
2026-08-09 12:30:45  CH0=2.51V  CH1=3.30V  [OverLimit] ch0>2.50
```

当采样值恢复低于阈值后,LED3 熄灭,不再打印 OverLimit。

---

### 5、调试 CLI(USART0)

**CLI 实现约束(强制):**
- USART0 ISR **仅收数据并入队**,不得在 ISR 中解析、打印或写 Flash
- CLI 解析/执行在 ControlTask 上下文中完成(收到完整行后通知任务)
- 日志打印走发送队列(任务上下文输出),ISR 不直接调用 printf

| 指令 | 功能 |
|---|---|
| test | 系统自检(Flash ID / TF 卡 / OLED / RTC) |
| rtc config | 设置时间(YYYY-MM-DD HH:MM:SS) |
| rtc now | 显示当前时间 |
| conf | 从 TF 卡导入 config.ini(原子提交) |
| ratio ch0/ch1 | 设置变比(0~100) |
| limit ch0/ch1 | 设置阈值(0~500) |
| config save / read | 参数存 Flash / 读 Flash |
| protocol | 切换 RS485 协议(0 自定义 / 1 Modbus) |
| id | 设置设备 ID(0001~FFFE) |
| baud | 设置波特率 |
| start / stop | 启动 / 停止采样(CLI 打印与存储) |
| upgrade cleanup clear CONFIRM | 仅维护使用:要求 TF 卡已挂载且 `/firmware/app.bin` 不存在,随后原子清除遗留的离线文件清理标志;条件不满足时拒绝执行 |
| hide / unhide | 隐藏格式 / 恢复 |
| help | 显示指令列表 |

**CLI 与 RS485 协议命令表分开维护**(两个表),仅在业务层复用服务函数。

---

## 五、数据处理(隐藏格式)

通过指令 `hide` 将时间戳与电压值编码为 HEX 格式(时间戳 4B + 通道电压 4B,共 8B),通过指令 `unhide` 恢复。

**编码规则:**

| 字段 | 长度 | 规则 | 示例 |
|---|---|---|---|
| 时间戳 | 4 字节 | Unix 时间戳 | 2025-01-01 12:30:45 → 0x6774C4F5 |
| 电压整数部分 | 2 字节 | 高位在前 | 12.5V → 12 → 0x000C |
| 电压小数部分 | 2 字节 | 小数 × 65536 | 12.5V → 0.5×65536=32768 → 0x8000 |

**编码总长:8 字节 = 时间戳(4B)+ 通道电压(4B:整数 2B + 小数 2B)。**

因此 2025-01-01 12:30:45 ch0=12.5V 隐藏后数据为:

```
6774C4F5 000C 8000
```

(CH1 需另行编码时,每通道追加 4B:整数 2B + 小数 2B)

**操作示例:**

```
串口输入: hide
串口返回:
6774C4F5000C8000
6774C4F5000C8000
(后续采样数据均按隐藏格式输出)
```

**超限标注:** 若采样值超过阈值,在数据末尾标注 `*`:

```
6774C4F5000C8000*
```

**恢复:** 输入 `unhide`,恢复原有格式输出。

---

## 六、数据存储

### 1、采样数据存储

在 TF 卡下建立 `sample` 文件夹,将实时采样数据存储到该文件夹中:

- 每个文件存储 10 条数据,超过 10 条后新建文件
- 文件名为 `sample_YYYYMMDD_HHMMSS.csv`,其中时间为文件建立时间
- 文件内格式(CSV):

```csv
2026-08-09 12:30:45,1.25,3.30
2026-08-09 12:30:50,1.26,3.30
```

**同步策略(强制):** 每条采样记录写入后执行 `f_sync()`(采样周期 5~15s,频率低,可承受);降低随机断电时丢失窗口至"正在写入的一条"。

### 2、超阈值数据存储

在 TF 卡下建立 `alarm` 文件夹,存储超限采样数据:

- 每个文件 10 条,超过 10 条新建文件
- 文件名为 `alarm_YYYYMMDD_HHMMSS.csv`
- 文件内格式:时间,通道,阈值,实际值

```csv
2026-08-09 12:30:45,CH0,2.50,2.51
```

**同步策略(强制):** 告警是低频关键记录,每写入一条立即执行 `f_sync()`;同步失败按“存储异常处理”进入重挂载/降级流程,同时保留外部 Flash 中的最近 10 条告警作为兜底。

### 3、审计日志存储

在 TF 卡下建立 `audit` 文件夹,记录操作日志:

- 每次上电新建一个文件,断电前所有操作记录在该文件中
- 文件名为 `boot_000001.log`,编号从 1 开始,每次上电自增(第 N 次上电 = boot_00000N.log;首次为 boot_000001.log,第五次为 boot_000005.log)
- 上电次数记录在 Flash 中(TF 卡清空后,第五次上电生成 `boot_000005.log`)
- 文件内容示例:

```
[2026-08-09 12:00:01] boot #1
[2026-08-09 12:00:05] system test: PASS
[2026-08-09 12:00:10] ratio ch0 set to 5.50
[2026-08-09 12:00:15] sampling started
[2026-08-09 12:00:30] alarm: CH0 2.51 > 2.50
[2026-08-09 12:00:35] sampling stopped
```

**同步策略(强制):** boot、配置修改、升级结果、告警确认等关键审计事件写入后执行 `f_sync()`;高频非关键调试信息不得写入审计文件,避免无意义同步和 TF 卡磨损。

### 4、存储异常处理

| 场景 | 行为 |
|---|---|
| TF 卡未插入 | 系统正常运行(仅不落盘),串口提示 `TF card not found`,降级运行 |
| 采样中拔卡 | 检测到拔卡,停止写入,继续采集与上报;重新插卡后恢复落盘 |
| 写入失败 | 自动关闭文件并重新挂载;**最多重试 3 次(退避 100/300/900ms)**,仍失败进入降级状态(仅上报不落盘),避免 StorageTask 永久阻塞 |
| 卡空间不足(剩余 < 5%) | 串口告警 `TF card full`;**采样存储与告警存储同时停止**(卡满时两者都写不进);最近 10 条告警改存外部 Flash,待插卡/清空间后补写 |

---

## 七、RS485 通信协议(二进制帧协议)

### 1、传输方式

**二进制字节直接传输**(不经 ASCII 转换)。与 ASCII-Hex 方式的区别:

| 项 | ASCII-Hex | 二进制(本项目) |
|---|---|---|
| 帧长度 | 每字节展开为 2 字符 | 减半 |
| 解析 | 需字符→十六进制转换 | 直接读字节 |
| RingBuffer 压力 | 大 | 减半 |
| 升级传输 | 慢 | 快 |
| 工业设备惯例 | 少见 | 常见 |

### 2、帧格式(强制)

```
帧头 | 协议版本 | 设备地址 | 帧类型 | 命令字 | 序列号 | 数据长度 | 数据 | CRC16 | 帧尾
A5B6 | 1B=0x02  | 2B大端  | 1B     | 2B大端 | 2B大端 | 2B大端  | NB   | 2B    | B6A5
```

**字段规则(强制):**

| 字段 | 长度 | 规则 |
|---|---|---|
| 帧头 | 2B | 固定 0xA5B6 |
| 协议版本 | 1B | 固定 0x02 |
| 设备地址 | 2B | 大端;0xFFFF 广播,0x0000 保留 |
| 帧类型 | 1B | 01 命令 / 02 应答 / 05 心跳 / FF 错误 |
| 命令字 | 2B | 大端,见命令字范围表 |
| 序列号 | 2B | 大端,请求方自增,应答回显(用于乱序/重发匹配) |
| 数据长度 | 2B | 大端,仅数据区字节数,最大 1024 |
| 数据 | NB | 大端序 |
| CRC16 | 2B | CRC-16/Modbus,计算范围 = 帧头 A5B6 起至数据末尾,大端发送 |
| 帧尾 | 2B | 固定 0xB6A5 |

**最大帧长:** 2+1+2+1+2+2+2+1024+2+2 = 1040 字节;RingBuffer 容量 ≥ 2048 字节(含余量)。

### 3、帧类型

| 帧类型 | 方向 | 含义 |
|---|---|---|
| 0x01 | 主机→设备 | 命令下发帧 |
| 0x02 | 设备→主机 | 应答帧(数据区首字节 0xFF = OK) |
| 0x05 | 设备→主机 | 设备主动事件/心跳帧(心跳 0x8888、唤醒 0x0381、自动上报 0x0382、告警 0x0681) |
| 0xFF | 设备→主机 | 错误应答帧(数据区首字节 = 错误码) |

### 4、错误码

| 错误码 | 含义 |
|---|---|
| 0x01 | CRC 错误 |
| 0x02 | 长度错误 |
| 0x03 | 非法命令字 |
| 0x04 | 非法参数值 |
| 0x05 | 设备忙(自动上报中,拒绝其他命令) |
| 0x06 | 固件校验失败 |
| 0x60 | 升级:固件头/元数据非法 | 
| 0x61 | 升级:分片 CRC 错误 |
| 0x62 | 升级:分片长度错误 |
| 0x63 | 升级:偏移越界 |
| 0x64 | 升级:序号不连续 |
| 0x65 | 升级:END 校验失败 |
| 0x66 | 升级:状态不允许该命令 |
| 0x67 | 升级:内部 Flash 擦除/写入/读回校验失败 |

### 5、命令字定义(自定义,项目内部一致)

| 模块 | 范围 | 命令字 | 功能 |
|---|---|---|---|
| 系统管理 | 0x0100~0x01FF | 0x0101 | 设备重启 |
| | | 0x0102 | 查询固件版本(应答 4B:主.次.修订.构建) |
| | | 0x0103 | 查询设备 ID |
| | | 0x0104 | 设置设备 ID(2B) |
| | | 0x0105 | 查询波特率 |
| | | 0x0106 | 设置波特率(应答 OK 后重启生效) |
| 数据查询 | 0x0200~0x02FF | 0x0201 | 查询 CH0(4B IEEE754,已乘变比) |
| | | 0x0202 | 查询 CH1(4B IEEE754) |
| | | 0x0203 | 查询 CH0+CH1(8B) |
| 采样控制 | 0x0300~0x03FF | 0x0301 | 设置 DAC 输出(2B 0x0000~0x0FFF) |
| | | 0x0302 | 启动自动上报(周期随上报间隔参数) |
| | | 0x0303 | 停止自动上报 |
| | | 0x0304 | 设置上报间隔(2B,秒) |
| | | 0x03AA | 进入睡眠 |
| | | 0x0381(事件) | 唤醒通知(设备→主机) |
| | | 0x0382(事件) | 自动上报数据(设备→主机) |
| 参数配置 | 0x0400~0x04FF | 0x0401 | 读取阈值(批量 CH0/CH1) |
| | | 0x0402 | 写入 CH0 阈值(4B IEEE754) |
| | | 0x0403 | 写入 CH1 阈值(4B IEEE754) |
| | | 0x0404 | 写入 CH0 变比(4B) |
| | | 0x0405 | 写入 CH1 变比(4B) |
| 固件升级 | 0x0500~0x05FF | 0x0500 | 进入 Bootloader(ENTER_BOOT,APP 处理,复位) |
| | | 0x0501 | 升级开始(BEGIN,Bootloader 校验固件头) |
| | | 0x0502 | 升级数据(DATA,分片,先写后 ACK) |
| | | 0x0503 | 升级结束(END,整体校验) |
| | | 0x0504 | 执行安装(INSTALL) |
| | | 0x0505 | 中止升级(ABORT,放弃暂存区) |
| 告警日志 | 0x0600~0x06FF | 0x0601 | 设置告警上报模式(01 主动/02 被动) |
| | | 0x0602 | 查询告警记录 |
| | | 0x0603 | 清除告警记录 |
| | | 0x0681(事件) | 主动告警上报(设备→主机) |
| 文件管理 | 0x0700~0x07FF | 0x0701 | 查询 TF 卡状态 |
| | | 0x0702 | 查询存储统计 |
| 诊断测试 | 0x0800~0x08FF | 0x0801 | 系统自检(等价 CLI test) |

**特殊值:** 0x8888 为心跳命令字;0xFFFF 是帧内“设备地址”字段的广播地址,不是命令字。

### 6、广播与多设备冲突(强制)

- 地址 0xFFFF 只用于“所有设备执行”的广播写命令,**任何设备均不得对广播帧应答**,从协议层消除多机同时驱动总线的冲突
- 查询、设备发现与需要确认结果的操作必须由主机按设备地址逐个轮询;广播帧不得用于查询命令、固件升级、设备 ID 修改或任何要求应答的操作
- 设备收到不允许广播的命令时静默丢弃,不返回错误帧(返回错误同样会造成多机冲突)

### 7、应答与超时

| 参数 | 值 |
|---|---|
| 命令应答超时 | 2000ms |
| 最大重发 | 2 次(共 3 次) |
| 心跳周期 | 上电/复位后发送一次,之后每 30s 一次 |
| 序列号 | 主机自增(16 位回绕),设备应答回显;**重复帧判定 = 地址+命令字+序列号完全相同且命中最近应答缓存**(仅此判为重复,重发上次应答;不做"回退即重发"的简单判断) |

### 8、自动上报期间的命令策略

- 自动上报(0x0302 开启)期间,仅接受 0x0303(停止上报)与 0x03AA(睡眠)
- 其他命令返回错误码 0x05(设备忙)
- 上报数据来自共享区最新采样(采集引擎持续运行,见十三-4)

---

## 八、Modbus RTU 从站协议

当参数 `protocol_mode = 1` 时,RS485 口按 Modbus RTU 从站协议通信。

### 1、寄存器映射表

**保持寄存器(可读写):**

| 寄存器地址 | 参数 | 类型 |
|---|---|---|
| 0x0000~0x0001 | CH0 变比 | float32(大端) |
| 0x0002~0x0003 | CH1 变比 | float32 |
| 0x0004~0x0005 | CH0 阈值 | float32 |
| 0x0006~0x0007 | CH1 阈值 | float32 |
| 0x0010 | 设备 ID | uint16 |
| 0x0011 | 波特率(枚举:0=9600,1=19200,2=38400,3=57600,4=115200) | uint16 |

**输入寄存器(只读):**

| 寄存器地址 | 参数 | 类型 |
|---|---|---|
| 0x0000~0x0001 | CH0 采样值 | float32 |
| 0x0002~0x0003 | CH1 采样值 | float32 |

### 2、Modbus RTU 约束(强制)

| 约束 | 规则 |
|---|---|
| **CRC 发送字节序** | Modbus RTU 标准 **低字节先发**(与自定义协议"高字节先发"相反;两协议可复用 CRC 算法,不能复用发送字节序) |
| **从站地址** | Modbus 从站地址范围 **1~247**;设备 ID 0x0001~0xFFFE 超出范围时,Modbus 模式仅接受 1~247 的子集(设备 ID > 247 无法作为 Modbus 从站,需提示) |
| **波特率寄存器** | 存枚举值(单 uint16 无法表示 115200):0=9600, 1=19200, 2=38400, 3=57600, 4=115200 |
| float32 字序 | 固定 AB CD(高字在前),与 IEEE754 大端一致;PLC 侧默认 CDAB 时需上位机交换 |

### 3、寄存器数据编码(强制)

- float32 在 Modbus 保持寄存器中占 2 个寄存器(4 字节),**字序固定 AB CD**(高字在前,与 IEEE754 大端字节序一致)
- 若上位机 PLC 默认使用 CDAB 字序,需在上位机侧配置交换(设备侧固定 AB CD)
- int32/uint16 均大端(高字节在前)

### 4、支持的功能码

| 功能码 | 名称 | 说明 |
|---|---|---|
| 0x03 | 读保持寄存器 | 批量读 |
| 0x04 | 读输入寄存器 | 批量读 |
| 0x06 | 写单个寄存器 | 单写 |
| 0x10 | 写多个寄存器 | 批量写 |

### 5、异常响应

| 异常码 | 含义 | 触发条件 |
|---|---|---|
| 0x01 | 非法功能码 | 不支持的功能码 |
| 0x02 | 非法数据地址 | 寄存器地址越界 |
| 0x03 | 非法数据值 | 写入值非法(变比>100 等) |

**示例(读保持寄存器):**

```
主站发送: 01 03 00 00 00 02 C4 0B
从站应答: 01 03 04 3F 80 00 00 xx xx   (CH0 变比 = 1.0)
```

---

## 九、告警管理

### 1、告警状态机(强制)

**为避免"持续超限时每个采样周期重复触发/重复写入/Flash 快速磨损",告警采用状态机管理:**

```
NORMAL(正常)
  │  采样值 > 阈值 且 连续 N 次超限(N=3,滤除毛刺)
  ▼
ACTIVE(告警激活)
  │  → 触发一次:LED3 亮 / 记录一次 / 按模式上报一次(不重复触发)
  │  采样值 < 阈值 - 回差(滞回,默认 0.05V)
  ▼
RECOVERED(恢复)
  │  → LED3 灭,可选记录恢复事件
  ▼
NORMAL
```

**关键规则:**
- **连续 3 次采样超限才进入 ACTIVE**(防毛刺误报)
- ACTIVE 期间即使持续超限,**只记录/上报一次**,不重复触发
- 恢复条件带**回差**(阈值 - 0.05V),避免在阈值附近反复进出告警(抖动)
- 如需持续提醒,设置最小重复上报周期(默认 60s),而非每次采样都触发

**联动行为(进入 ACTIVE 时):**

| 动作 | 行为 |
|---|---|
| LED3 | 点亮(告警指示) |
| CLI 串口 | 打印 OverLimit(见四-4) |
| TF 卡 | 写入 `alarm_YYYYMMDD_HHMMSS.csv` |
| Flash | 记录告警(最近 10 条,倒序) |
| RS485 | 按告警模式决定是否主动上报 |

### 2、告警模式(RS485 指令 0x0601)

**主动上报模式(01):** 进入 ACTIVE 时,发送**事件帧**(帧类型 05,命令字 0x0681,一次):

```
事件帧数据区: 时间戳(4B) | 通道号(1B) | 阈值(4B IEEE754) | 实际值(4B IEEE754)
```

同时存储至 Flash。

**被动上报模式(02):** 仅存储至 Flash,待上位机查询(0x0602)后返回。

### 3、查询告警记录(0x0602)

正常**应答帧**(0x0602)返回最近十次超阈值告警(按时间倒序),数据区格式:

```
记录数(1B) | 记录1: 时间戳(4B) | 通道(1B) | 阈值(4B) | 实际值(4B) | 记录2 ...
```

无告警时数据区仅 1 字节:`0x00`(空)。

### 4、清除告警记录(0x0603)

清除 Flash 中全部告警记录,应答 OK。清除后查询返回空(数据区 1 字节 0x00)。

---

## 十、低功耗管理

### 1、睡眠指令(RS485 0x03AA)

收到睡眠指令后:

```
上位机下发(新帧格式):
  A5B6 | 02 | 0001 | 01 | 03AA | 0001 | 0000 | (空) | CRC16 | B6A5
  帧头  版本  地址   类型  命令字  序列号  长度
设备应答:
  A5B6 | 02 | 0001 | 02 | 03AA | 0001 | 0001 | FF | CRC16 | B6A5
  帧头  版本  地址   类型  命令字  序列号  长度  OK
```

应答后设备进入深度睡眠(Deepsleep)。

**睡眠前静默序列(强制,进入 Deepsleep 前必须依次完成):**

```
① 停止接收新的存储请求(StorageTask 队列暂停入队)
② 等待 StorageTask 完成当前写入并 f_sync(数据落盘)
③ 关闭 TF 卡当前文件
④ 停止 ADC/DMA 采集
⑤ 等待 RS485 发送完成,拉低 DE(释放总线)
⑥ 保存当前业务状态(采样/上报/告警使能位,见 十三-3)至 RAM/Flash
⑦ 配置 RTC 闹钟(10s)+ EXTI 唤醒源(按键)
⑧ **IWDG 窗口调整:睡眠前将 IWDG 超时改为 ≥15s(> 10s 睡眠 + 时钟误差 + 唤醒初始化时间)**,防止 5s IWDG 在深睡期间提前复位
⑨ **暂停调度器(vTaskSuspendAll)**:关闭外设后、进入 WFI 前必须暂停,避免 DisplayTask/ProtocolTask/HealthTask 继续访问已关闭外设
⑩ 熄灭 OLED / LED
⑪ 进入深度睡眠(WFI)
```

**睡眠看门狗所有权(强制,消除冲突):**

- ControlTask **仅发起睡眠请求**(不直接喂狗)
- **HealthTask 执行**:检查任务健康 → 调整 IWDG 窗口(15s)→ **执行最后一次喂狗** → 返回 sleep_ready
- 唤醒后:恢复外设 → **恢复调度器**(xTaskResumeAll)→ HealthTask 恢复 5s 策略

**唤醒后恢复(强制):**
- 恢复 IWDG 为正常 5s 策略(HealthTask 接管喂狗)
- **FreeRTOS Tick 策略(固定):** 深度睡眠期间 SysTick 暂停,RTOS 相对定时器/任务周期暂停,**不补偿 Tick 计数**;RTC 只用于恢复绝对时间。
  - 唤醒后恢复序列(强制,软件定时器显式重对齐,**复用不重建**):
    ① 读取 RTC 更新墙上时间(绝对时间正确)
    ② **复用启动时创建的定时器**:睡眠前 xTimerStop() → 唤醒后 xTimerChangePeriod() → xTimerStart()(禁止每次唤醒 xTimerCreate,避免 heap 泄漏与重复回调)
    ③ 使用 vTaskDelayUntil() 的任务:收到唤醒事件后自行 `xLastWakeTime = xTaskGetTickCount()` 重置基准(不重新创建任务)
    ④ 睡眠期间不补发历史采样与历史上报

### 2、自动唤醒

- RTC 闹钟 10s 后自动唤醒
- 唤醒后发送**事件帧**(帧类型 05,命令字 0x0381,数据区 1 字节 0xFF):

```
事件帧: A5B6 02 设备地址 05 0381 序列号 0001 FF CRC B6A5
```

- **唤醒后恢复流程(强制):**
  - 重建系统时钟(PLL 重新配置)
  - 重新初始化外设(USART/ADC/DMA/OLED 等)
  - 恢复睡眠前保存的业务状态(采样/上报使能位按原状态恢复)
  - 恢复采集与通信功能

### 3、睡眠期间行为

| 外设 | 睡眠期间 |
|---|---|
| CPU | 停止(深度睡眠) |
| USART1(RS485) | 停止接收 |
| OLED | 熄灭 |
| LED | 全灭 |
| RTC | 运行(负责唤醒) |
| 外部 Flash / TF 卡 | 不访问 |

---

## 十一、人机交互(外设联动规格)

### 1、LED 行为定义(强制)

| LED | 名称 | 点亮条件 | 熄灭条件 | 特效 |
|---|---|---|---|---|
| LED1 | 系统状态灯 | App 区运行期间 | 设备睡眠 | 1s 周期闪烁 |
| LED2 | 采集工作灯 | 采样或自动上报任一进行中 | **采样与上报均关闭** | 常亮 |
| LED3 | 告警灯 | 任一通道超阈值 | 全部恢复阈值内 | 触发时 0.2s 快闪;按下 KEY5 确认后变为常亮 |
| LED4 | 参数异常灯 | 参数加载失败(Flash 无有效记录) | 参数加载成功 | 常亮 |
| LED5 | TF 卡状态灯 | TF 卡挂载成功 | TF 卡未插/掉卡 | 常亮 |
| LED6 | 升级指示灯 | Bootloader 升级阶段由 Bootloader 状态指示器接管 | App 运行期间不单独占用 | 状态/进度指示(见本节点 5/6) |

**全局规则:**
- Bootloader 运行期间(未来 M6):LED 由 Bootloader 状态指示器独占;等待、写入、校验、成功和失败状态按节点 6 显示
- 设备进入睡眠:LED 全部熄灭
- LED1 的 1s 闪烁与采样周期无关,恒定 1s
- 按键有效触发后,对应功能 LED 闪烁 1 次作为操作反馈(见节点 2 通用规则)

### 2、按键行为定义(强制)

**按键框架:** 全部按键由 ebtn 按键状态机管理(事件驱动,自动去抖)。

**ebtn 事件类型:**

| 事件 | 含义 |
|---|---|
| ONPRESS | 有效按下(去抖后) |
| ONRELEASE | 有效释放 |
| ONCLICK | 有效单击(按下+释放序列) |
| KEEPALIVE | 保持按压(周期性上报,用于长按识别) |

**双模式按键语义:**

系统分为**正常模式**与**菜单模式**,按键行为随模式切换:

| 按键 | 正常模式(默认) | 菜单模式 |
|---|---|---|
| KEY1 | 采样启停翻转(ONCLICK) | 光标上移 |
| KEY2 | 采样周期 5s(ONCLICK) | 光标下移 |
| KEY3 | 采样周期 10s(ONCLICK) | 确认/进入子菜单 |
| KEY4 | 采样周期 15s(ONCLICK) | 返回上级菜单 |
| KEY5 | 告警确认(ONCLICK) | 退出菜单(KEEPALIVE 长按 2s) |
| KEY6 | 系统自检(ONCLICK) | 进入菜单(KEEPALIVE 长按 2s) |

**模式切换规则:**
- KEY6 长按 2s(KEEPALIVE 累计)→ 从正常模式进入菜单模式
- KEY5 长按 2s → 从菜单模式退出回正常模式
- 进入菜单时,采样等业务功能**继续运行不受影响**(仅按键语义切换)

**按键通用规则:**
- 去抖、单击判定、长按判定均由 ebtn 完成(不自行轮询 GPIO)
- 睡眠状态下按键可唤醒设备(EXTI 唤醒,恢复后发送 0x0381 事件帧)
- **操作反馈**:按键有效触发后,LED1 以 0.1s 亮 0.1s 灭闪烁 1 次(共 0.2s),表示按键已识别

### 3、多级菜单(菜单模式)

**进入菜单(KEY6 长按 2s)后,OLED 进入菜单导航界面。**

**菜单结构(树形):**

```
主菜单
├── 1.采样控制
│    ├── 启动/停止采样
│    └── 采样周期 → 5s / 10s / 15s
├── 2.参数配置
│    ├── CH0 变比
│    ├── CH1 变比
│    ├── CH0 阈值
│    ├── CH1 阈值
│    ├── 设备 ID
│    ├── 波特率
│    └── 协议模式(自定义 / Modbus)
├── 3.数据查看
│    ├── CH0/CH1 实时值
│    └── 最近告警
└── 4.系统
     ├── 系统自检
     ├── 固件版本
     ├── 恢复出厂设置
     └── 进入睡眠
```

**导航按键(菜单模式下):**

| 按键 | 行为 |
|---|---|
| KEY1 | 光标上移(循环滚动) |
| KEY2 | 光标下移(循环滚动) |
| KEY3 | 进入子菜单 / 确认执行当前项 |
| KEY4 | 返回上级菜单 |
| KEY5(长按 2s) | 退出菜单,回正常模式 |
| KEY6 | 无效(避免误触) |

**OLED 菜单显示(双行):**

```
>1.采样控制        ← 第一行:当前选中项(带 > 光标)
 2.参数配置        ← 第二行:下一项预览
```

进入子菜单后同理逐级下钻;返回时逐级回退;参数修改项(变比/阈值/ID/波特率)在选中后进入"数值编辑"界面(KEY1/KEY2 增减,KEY3 确认,KEY4 取消)。

**实现要求(技术选型):**
- 菜单导航逻辑自研:菜单项结构体数组 + 状态机,子菜单用指针挂接(树形结构)
- 菜单结构编译期静态定义(结构固定,只变状态),不使用动态内存
- 与 ebtn 事件解耦:ebtn 负责识别按键事件,菜单状态机负责响应导航

### 4、OLED 显示定义(强制)

**双行文本显示,始终刷新:**

| 位置 | 默认内容 | 说明 |
|---|---|---|
| 第一行 | 设备状态(见下表) | 常显 |
| 第二行 | 详情/数值 | 见下表 |

**第一行(设备状态)定义:**

| 系统状态 | OLED 第一行 | 说明 |
|---|---|---|
| 正常运行 | `RUNNING` | App 运行,非采样 |
| 采样中 | `SAMPLING` | 周期采集进行中 |
| 参数异常 | `CONFIG ERR` | 参数区无有效记录 |
| 通信故障 | `COMM ERR` | RS485 通信异常 |
| 存储故障 | `SD ERR` | TF 卡异常 |

**第二行(详情/数值)定义:**

| 系统状态 | OLED 第二行 | 说明 |
|---|---|---|
| 正常运行 | `CH0:x.xxV CH1:x.xxV` | 实时显示通道值 |
| 采样中 | `CH0:x.xxV CH1:x.xxV` | 随采样刷新 |
| 超限告警 | `CH0 OVER! 2.51V` | 超限通道高亮提示 |
| 参数异常 | `FLASH FAIL` | 参数区读写失败 |
| 通信故障 | `NO DATA` | 超时未收到主站帧 |
| 存储故障 | `CARD NOT FOUND` | TF 卡未挂载 |

**Bootloader 期间 OLED(第一行/第二行):**

| 阶段 | OLED 第一行 | OLED 第二行 |
|---|---|---|
| 等待跳转 | `BOOTLOADER` | `WAIT...` |
| 升级等待 | `BOOTLOADER` | `READY...` |
| 固件接收中 | `UPDATING` | 进度条 + 百分比(见节点 4) |
| 校验中 | `UPDATING` | `VERIFY...` |
| 搬运中 | `UPDATING` | `FLASHING...` |
| 升级完成 | `BOOTLOADER` | `DONE!` |

### 5、升级进度条与百分比(OLED)

**固件接收阶段**(占 0~90%):

```
第二行: [████████░░░░░░░░░░░░] 45%
        ↑ 进度条(20 格)     ↑ 百分比
```

- 进度条按已接收字节数 / 固件总长度实时更新
- 每接收完一片(256 字节)刷新一次进度

**校验与搬运阶段**(90~100%):

```
第二行: [████████████████████] 100%
```

- 校验中:进度条保持,百分比 90%,第二行上方显示 `VERIFY...`
- 搬运中:百分比 90→100 推进
- 完成:满格 `100%`,切换为 `DONE!`

### 6、升级专用 LED 状态/进度指示（M6 目标,当前未实现）

**本节保留为后续 Bootloader 的目标规格。当前工程不创建 Bootloader LED 模块,也不在 APP 层接入升级状态机。升级阶段不再使用波浪呼吸灯,未来改为无 PWM 的静态状态显示和六级累计进度条。**

**状态定义:**

| Bootloader 状态 | LED 行为 | 说明 |
|---|---|---|
| 等待升级 | LED1 慢闪,周期 1s | Bootloader 已启动,等待升级请求 |
| 擦除/写入 | LED1~LEDn 累计点亮 | `n` 由已成功写入字节数计算 |
| 正在校验 | 六个 LED 同时慢闪,周期 1s | CRC/镜像完整性校验进行中 |
| 升级成功 | 六个 LED 常亮 | 校验通过,短暂显示后跳转 App |
| 升级失败 | 六个 LED 快闪两次后停顿,重复保持 | 不跳转 App,等待复位或新的升级请求 |

**六级进度映射:**

| 固件进度 | LED 显示 |
|---|---|
| 0% | 全灭 |
| 1%~16% | LED1 |
| 17%~33% | LED1~LED2 |
| 34%~50% | LED1~LED3 |
| 51%~66% | LED1~LED4 |
| 67%~83% | LED1~LED5 |
| 84%~100% | LED1~LED6 |

**实现约束:**

- 写入进度由 `已成功写入字节数 / 固件总长度` 计算,不能只按已接收字节数估计。
- 固件总长度来自升级包头;协议未提供总长度时,只能显示状态,不能声称有准确百分比。
- 写入/校验阶段只在状态或进度变化时更新 GPIO,不使用 `sinf()`、`powf()`、浮点亮度计算或软件 PWM。
- 1ms 节拍只用于慢闪和错误双闪的状态机;擦写内部 Flash 造成 CPU 暂停时,LED 保持当前电平。
- Bootloader 与 App 分别拥有 LED 控制权,不能在同一运行路径中同时刷新 `PD8~PD13`。

### 7、完整状态联动矩阵(核心目标)

> 下表是最终 Bootloader/App 联动目标,不是当前单工程的已实现状态。当前只保留 `APP/led_app.c` 的应用层 LED 接口。

| 状态 | LED1 | LED2 | LED3 | LED4 | LED5 | LED6 | OLED 第一行 | OLED 第二行 | KEY1 | 采样 | 上报 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Bootloader 等待 | 慢闪 | 灭 | 灭 | 灭 | 灭 | 灭 | BOOTLOADER | WAIT... | 无效 | 停 | 无 |
| 升级写入中 | 按进度 | 按进度 | 按进度 | 按进度 | 按进度 | 按进度 | UPDATING | 进度条+% | 无效 | 停 | 无 |
| Bootloader 校验中 | 慢闪 | 慢闪 | 慢闪 | 慢闪 | 慢闪 | 慢闪 | UPDATING | VERIFY... | 无效 | 停 | 无 |
| Bootloader 升级成功 | 常亮 | 常亮 | 常亮 | 常亮 | 常亮 | 常亮 | BOOTLOADER | DONE! | 无效 | 停 | 无 |
| Bootloader 升级失败 | 双闪保持 | 双闪保持 | 双闪保持 | 双闪保持 | 双闪保持 | 双闪保持 | BOOTLOADER | UPDATE FAIL | 无效 | 停 | 无 |
| App 空闲 | 1s 闪 | 灭 | 按告警 | 按参数 | 按卡 | 灭 | RUNNING | CH0/CH1 值 | 启动采样 | 停 | 无 |
| 自动采集 | 1s 闪 | 亮 | 按告警 | 按参数 | 按卡 | 灭 | SAMPLING | CH0/CH1 值 | 停止采样 | 周期运行 | 周期帧 |
| 超限告警 | 1s 闪 | 亮 | 0.2s 快闪 | 按参数 | 按卡 | 灭 | SAMPLING | CH0 OVER! | 停止采样 | 周期运行 | 周期帧+告警 |
| 告警已确认 | 1s 闪 | 亮 | 常亮 | 按参数 | 按卡 | 灭 | SAMPLING | CH0 OVER! | 停止采样 | 周期运行 | 周期帧+告警 |
| 菜单模式 | 1s 闪 | 按状态 | 按告警 | 按参数 | 按卡 | 灭 | 菜单当前项 | 菜单下一项 | 导航(上移) | 按原状态 | 按原状态 |
| 深度睡眠 | 灭 | 灭 | 灭 | 灭 | 灭 | 灭 | (灭) | (灭) | 唤醒 | 停 | 无 |

**矩阵说明:**
- 升级写入中:LED1~LED6 按六级累计进度条显示,矩阵中"按进度"指当前进度及以前的 LED 累计点亮
- "告警已确认"状态由按下 KEY5 进入(告警闪烁→常亮)

---

## 十二、固件升级(Bootloader)

### 1、内部 Flash 分区(当前候选方案)

> Bootloader 和 App 都在 GD32F470 内部 Flash 中运行。下面的地址用于 M1 双工程的初始布局，必须等链接脚本、镜像大小和跳转实测后再定稿；GD25Q40E 不作为复位后的直接执行区。
>
> **⚠️ M1 实测(双工具确认):** OpenOCD(stm32f2x 驱动)与 Keil(GD32F4xx_512KB.FLM)烧录 App 均按**整扇区**擦除,App 覆盖扇区 4+5 全擦。因此任何与 App 同处扇区 4 的槽位(0x08010000~0x08011FFF 的 slot A/B)都会在烧 App 时被连擦——下方参数区 slot A/B 已废弃,升级状态元数据外部化至 GD25Q40E 固定元数据槽(M6),此两槽仅保留为布局占位。

| 区域 | 起始地址 | 结束地址 | 大小 |
|---|---|---|---|
| Bootloader 区 | 0x08000000 | 0x0800FFFF | 64KB |
| 保留区 slot A(旧参数区,**已废弃**) | 0x08010000 | 0x08010FFF | 4KB(独立擦除页) |
| 保留区 slot B(旧参数区,**已废弃**) | 0x08011000 | 0x08011FFF | 4KB(独立擦除页) |
| App 区 | 0x08012000 | 0x08031FFF | 128KB |
| App 备份区 | 0x08032000 | 0x08051FFF | 128KB |
| 固件暂存区 | 0x08052000 | 0x08071FFF | 128KB |

**统一地址宏(当前候选,最终代码只引用 Common 定义,禁止业务代码硬编码):**

```c
#define FLASH_BOOT_BASE     0x08000000UL   /* Bootloader 区 */
#define FLASH_META_SLOT_A   0x08010000UL   /* 参数区 slot A(独立 4KB 页) */
#define FLASH_META_SLOT_B   0x08011000UL   /* 参数区 slot B(独立 4KB 页) */
#define APP_BASE            0x08012000UL   /* App 区 */
#define APP_END             0x08031FFFUL
#define BACKUP_BASE         0x08032000UL   /* App 备份区 */
#define BACKUP_END          0x08051FFFUL
#define STAGING_BASE        0x08052000UL   /* 固件暂存区 */
#define STAGING_END         0x08071FFFUL

#define MANIFEST_RESERVED_SIZE   64UL
#define MAX_IMAGE_SIZE           (128UL * 1024UL - MANIFEST_RESERVED_SIZE)  /* 128KB - 64B */
#define APP_MANIFEST_ADDR        (APP_END + 1UL - MANIFEST_RESERVED_SIZE)     /* 0x08031FC0 */
#define BACKUP_MANIFEST_ADDR     (BACKUP_END + 1UL - MANIFEST_RESERVED_SIZE)  /* 0x08051FC0 */
#define STAGING_MANIFEST_ADDR    (STAGING_END + 1UL - MANIFEST_RESERVED_SIZE) /* 0x08071FC0 */
```

**当前设计约束(待 M1 构建与跳转验证):**
- 参数区扩展为 **2 × 4KB 独立擦除页**(slot A / slot B),App 起始后移至 0x08012000;512KB Flash 尾部 0x08072000~0x0807FFFF 保留,不得被当前 Boot/App 链接脚本占用
- 所有新边界仍为 4KB 对齐,`fmc_page_erase()` 页擦除安全
- 每个槽独立页擦除:**擦除一个槽不会影响另一个槽**(双槽掉电原子性成立的前提)
- App 区/备份区/暂存区大小不变,起始地址整体后移 4KB
- **Bootloader 初始容量门槛:** 若暂采用 64KB 候选区,发布构建的 `Code + RO-data + RW-data load image` 目标为 ≤ 60KB,至少保留 4KB 余量;最终容量等 M1 实际构建后再决定,不得通过关闭校验/回滚功能硬塞

### 2、固件格式(自定义固件包)

**固件包 = firmware_header_t + 原始 APP 映像**(在线升级与 TF 卡离线升级共用同一套解析代码):

`firmware_header_t`、映像 Manifest、升级状态枚举、安装子阶段、Flash 分区地址、CRC 参数与升级错误码均由 `Common/` 共享契约模块提供。Bootloader 与 APP 必须编译同一份定义和逐字段序列化实现,禁止在两个工程中复制结构体或地址宏。

```c
typedef struct {
    uint32_t magic;            /* 魔术字,固定 0x5AA5C33C(按字节比较) */
    uint16_t package_version;  /* 固件包格式版本,当前 = 1 */
    uint16_t header_size;      /* 固件头长度,固定 = FW_HEADER_V1_SIZE(32U),不依赖编译器结构体布局 */
    uint32_t firmware_version; /* 固件版本号(0x01000400 = 1.0.4.0) */
    uint32_t image_size;       /* APP 映像长度(不含固件头) */
    uint32_t target_address;   /* 目标地址,固定 0x08012000(APP_BASE) */
    uint32_t image_crc32;      /* APP 映像 CRC32 */
    uint32_t flags;            /* 标志位:bit0=允许降级,bit1=强制升级(跳过版本检查) */
    uint32_t header_crc32;     /* 从 magic 到 flags 的 CRC32(不含自身) */
} firmware_header_t;
```

**映像独立 manifest(强制,双槽元数据全部损坏时仍可独立校验 App/Backup):**

```c
typedef struct {
    uint32_t magic;            /* 固定 0x4D4E4653("MNFS") */
    uint32_t image_version;    /* 映像版本号 */
    uint32_t image_size;       /* 映像长度 */
    uint32_t image_crc32;      /* 映像 CRC32 */
    uint32_t manifest_crc32;   /* 上述字段自身 CRC32(不含自身) */
} image_manifest_t;            /* 20 字节,放各 128KB 分区末尾保留区 */

_Static_assert(sizeof(image_manifest_t) == 20, "image_manifest_t must be 20 bytes");
```

- 每个映像(App/Backup/Staging)分区末尾预留 64B 保留区存放 manifest
- **最大 App 映像大小 = 128KB - 64B**(manifest 占位)
- 双槽元数据损坏时:读分区末尾 manifest 独立验证映像(MSP/复位向量/CRC)
- **manifest 生命周期(强制,不单独传输):**
  - 在线升级:END 校验成功后,由 Bootloader **根据已验证的 firmware_header 生成** manifest,写 STAGING_MANIFEST_ADDR 并读回校验
  - 备份:安装时根据 active 元数据(或读取 App 末尾 manifest)**复制/重新生成** BACKUP manifest
  - 安装:新 App 写入且整体 CRC 通过后,**最后写** APP manifest(写映像 → 校验 → 写 manifest 的次序保证掉电时 manifest 不会指向不完整映像)
  - **初始出厂(强制,固定烧录四件套)**:
    Bootloader + 原始 App + APP_MANIFEST_ADDR 处的 manifest + 初始元数据槽
    初始元数据槽固定为:`state=IDLE,generation=1,upgrade_source=NONE,active_size/version/crc=APP manifest,pending/backup/failed 字段清零`
    ("首次升级时由 Bootloader 补齐"不成立:首次启动时双元数据槽为空,Bootloader 依赖 APP manifest 判断 App 有效性,若没有 manifest 会误入安全升级模式导致 App 无法启动)
- Python 打包工具输出两类产物:`app.bin`(固件头+原始 APP 映像,用于在线/TF 升级)与 `app_manifest.bin`/工厂合并映像(仅用于首次出厂烧录);在线/离线升级过程中的 Staging/App/Backup manifest 均由 Bootloader 根据已验证 header 生成

**序列化规则(强制,Python 打包工具与 MCU 必须一致):**

| 规则 | 说明 |
|---|---|
| **字节序** | 固件头所有多字节字段**固定大端**(网络字节序)传输/存储 |
| **禁止直接发结构体** | 不得 `memcpy(&hdr, buf)` 发送 C 结构体内存(编译器对齐/填充会导致字节偏移不一致);必须**逐字段序列化/反序列化** |
| Manifest 存储格式 | `image_manifest_t` 的 5 个 uint32 字段固定**小端**存储;`manifest_crc32` 覆盖前 16 个已序列化字节;Python 与 MCU 均逐字段编解码,不得依赖结构体内存布局 |
| header_crc32 范围 | 从 `magic` 起至 `flags` 止(不含 `header_crc32` 自身) |
| CRC32 参数 | 多项式 0x04C11DB7,初值 0xFFFFFFFF,输入反射,输出异或 0xFFFFFFFF(标准 CRC-32/ISO-HDLC);**测试向量:CRC32("123456789") = 0xCBF43926** |
| CRC16 参数 | 多项式 0xA001,初值 0xFFFF,RefIn/RefOut = true,XorOut = 0x0000(CRC-16/Modbus);**测试向量:CRC16("123456789") = 0x4B37**;本项目发送顺序:高字节先发 |
| header_size | **固定 32 字节**(`#define FW_HEADER_V1_SIZE 32U`),解析器按序列化字段长度处理,不依赖编译器结构体布局;用于兼容以后增加字段(header_size ≥ 当前结构长度时按新格式解析) |
| flags | bit0 = 允许降级安装;bit1 = 强制升级(跳过相同版本拒绝逻辑) |

**固件安全边界(强制声明):**

- 本项目定位为个人演示终端,假设 RS485 主机和 TF 卡由可信维护人员控制,**不覆盖对抗性攻击场景**
- CRC32 只保证传输/存储完整性,不提供固件来源认证;项目不得宣称已经实现 Secure Boot 或密码学安全 OTA
- V1 固件头继续固定为 32B,不增加 `reserved` 字段,避免破坏现有包格式
- 未来如需真实性校验,通过现有 `package_version + header_size` 定义 V2 扩展头,增加 SHA-256 与 ECDSA/Ed25519 签名描述;V1 解析器按版本拒绝未知安全格式,不得静默降级校验

**存储规则(强制):**

| 位置 | 内容 |
|---|---|
| TF 卡 `/firmware/app.bin` | **完整固件包**(固件头 + APP 映像) |
| 固件暂存区(0x08052000) | **仅原始 APP 映像**(固定方案):firmware_header 内容写入 RAM,必要字段保存到 pending 元数据;**固件头永远不写暂存映像区**(避免 DATA 偏移/向量表位置/安装源地址歧义) |
| App 区(0x08012000) | 仅 **原始 APP 映像**,向量表位于 0x08012000 |

**规则:**
- **固件头绝对不能写入 App 区向量表地址**(会覆盖 MSP/复位向量)
- `image_size ≤ MAX_IMAGE_SIZE(128KB-64B)`(暂存区容量扣除 manifest 保留区),`固件包大小 = header_size + image_size`
- 版本比对:升级前比较 `firmware_version` 与当前运行版本,相同版本拒绝(防重复安装);`flags.bit1`(强制升级)可跳过此检查,`flags.bit0`(允许降级)可安装旧版本

### 3、升级状态机(可掉电恢复)

```c
typedef enum {
    FW_STATE_IDLE,               /* 空闲,无升级活动 */
    FW_STATE_RECEIVING,          /* 接收固件分片中 */
    FW_STATE_STAGED_VALID,       /* 暂存区固件校验通过,等待安装 */
    FW_STATE_INSTALLING,         /* 正在安装(备份/搬运) */
    FW_STATE_TRIAL_PENDING,      /* 新固件试运行,等待启动确认 */
    FW_STATE_CONFIRMED,          /* 新固件确认成功 */
    FW_STATE_ROLLBACK_REQUIRED,  /* 需要回滚 */
    FW_STATE_ROLLED_BACK         /* 已回滚到旧固件 */
} firmware_state_t;
```

**存储:** 升级状态 + 固件元数据存于内部 Flash 参数区(0x08010000~0x08011FFF,双槽区域),Bootloader 与 APP 共享;掉电后按状态机恢复。

### 4、在线升级流程(五阶段)

**命令职责(强制):**

| 命令字 | 处理者 | 动作 |
|---|---|---|
| 0x0500 ENTER_BOOT | **仅 APP** | 持久化升级请求标志 → 应答 READY → 复位 |
| 0x0501 BEGIN | **仅 Bootloader** | 接收并校验 firmware_header → 状态 = RECEIVING |
| 0x0502 DATA | **仅 Bootloader** | 每片先写后 ACK |
| 0x0503 END | **仅 Bootloader** | 整体校验,成功设 STAGED_VALID |
| 0x0504 INSTALL | **仅 Bootloader** | 备份→搬运→TRIAL_PENDING→重启 |
| 0x0505 ABORT | **仅 Bootloader** | RECEIVING/STAGED_VALID 下原子放弃本次升级 |

**状态机命令限制(强制,按当前状态只接受合法命令):**

| 当前状态 | 允许命令 | 说明 |
|---|---|---|
| IDLE 且 `upgrade_source=NONE` | 0x0501 BEGIN | 真正空闲时只接受开始升级 |
| IDLE 且 `upgrade_source=TF_OFFLINE` | (不接受升级命令) | 离线文件清理待完成;先重试清理,避免新升级覆盖清理上下文 |
| RECEIVING | 0x0502 DATA、0x0503 END、0x0505 ABORT | 传输中 |
| STAGED_VALID | 0x0504 INSTALL、0x0505 ABORT | 固件就绪,等待安装 |
| INSTALLING | (不接受通信命令) | 安装中,忽略一切帧 |
| TRIAL_PENDING | (仅启动确认流程) | 试运行,不接收升级命令 |

**第一阶段:进入 Bootloader(ENTER_BOOT,0x0500,APP 内)**

```
主机 → 0x0500(APP 运行中)
APP 顺序执行(先持久化后应答):
  ① 读取升级元数据;若 upgrade_source == TF_OFFLINE(文件清理待完成),应答 NACK 0x66,不写请求标志、不复位
  ② 持久化升级请求标志(内部 Flash 参数区,校验写入成功)
  ③ 应答 READY
  ④ 等待 USART 发送完成
  ⑤ 软件复位
重启后进入 Bootloader → 检测到升级请求标志:
  ① 清除请求标志
  ② 状态设为 IDLE
  ③ 发送 Bootloader Ready 事件帧
  ④ 等待 0x0501 BEGIN
```

**第二阶段:开始升级(BEGIN,0x0501,Bootloader 内)**

```
主机 → 0x0501,数据 = firmware_header_t(逐字段序列化)
Bootloader 顺序执行(先持久化后应答):
  ① 校验:magic / header_size / image_size ≤ MAX_IMAGE_SIZE(128KB-64B) / target_address 合法
     └─ 失败 → 应答 NACK(升级错误码 0x60)
  ② 原子持久化:pending 固件元数据 + 状态 = RECEIVING + upgrade_source = ONLINE(校验写入成功)
  ③ **staging_prepare()(强制,Flash 不能把已编程的 0 写回 1,第二次升级必须擦除)**:
     a. 擦除整个 128KB Staging 分区(已包含 STAGING_MANIFEST_ADDR 所在页,禁止重复擦除)
     b. 校验擦除结果(读回全 0xFF)
     c. 清零接收长度/分片序号
     d. 任一步失败 → 原子清除 pending/source、状态回 IDLE,应答 NACK 0x67,不得发送 READY
  ④ 应答 READY(数据区 0xFF)——**必须在擦除完成后才发**;主机 BEGIN 超时应适当放宽(覆盖擦除耗时)
```

**第三阶段:分片传输(DATA,0x0502)**

```
主机逐片下发,每片结构:
  分片序号(2B) | 目标偏移(4B) | 有效长度(2B) | 固件数据(NB ≤ 256) | 分片CRC16(2B)

设备每片处理顺序(强制,先写后 ACK):
  ① 校验帧 CRC 与偏移合法性(非法 → NACK 0x61/0x62/0x63)
  ② 写入暂存区对应偏移
  ③ 等待 Flash 写入完成并逐片读回校验;失败 → NACK 0x67,不更新接收进度
  ④ 更新已接收长度与最后序号
  ⑤ 最后发送 ACK(回显分片序号)

  NACK(回显序号 + 升级错误码)→ 主机重传该片;状态不允许的命令返回 0x66
```

- **顺序强制(防重叠/跳跃/稀疏写入):**
  - `offset == received_length`(偏移必须等于已接收长度,顺序写入)
  - `sequence == expected_sequence`(序号必须连续)
  - `offset + chunk_length <= image_size`(不超界)
  - 未来如需乱序传输,再引入分片位图;当前顺序传输不复杂化
- 只有**真正持久化成功后**才 ACK,否则主机误认为该片成功
- 不需要静默超时判断结束(END 显式声明)
- 重复序号:缓存并重发上次应答,不重复写入(防副作用)
- **断点续传边界(明确):**
  - 串口断线但 MCU 未复位 → 可从 RAM 中最后 ACK 的序号继续
  - MCU 掉电 → 当前设计丢弃 RECEIVING,暂存区作废,不能续传
  - **不要每 256 字节都擦写内部参数区保存序号**(会快速磨损 Flash),序号保存在 RAM,掉电即失

**第四阶段:结束传输(END,0x0503)**

```
主机 → 0x0503
Bootloader 检查:
  ① 实际接收长度 == image_size
  ② 全映像 CRC32 == header 中的 image_crc32
  ③ MSP 位于有效 SRAM 范围 (0x20000000~0x20030000)
  ④ Reset Handler 位于 App 区 (APP_BASE ~ APP_BASE + image_size) 且 < APP_MANIFEST_ADDR,最低位为 1
  ⑤ 根据已验证的 firmware_header 在 RAM 逐字段生成 manifest,按固定小端序列化并计算 manifest_crc32(Manifest 不单独传输,暂存区末尾此时无权威 manifest 可比对)
  ├─ 全部通过 → 写 STAGING_MANIFEST_ADDR → 读回校验 → 持久化 pending 元数据 + 状态 = STAGED_VALID,应答 OK
  └─ 任一失败 → 应答 NACK(0x65 END 校验失败;Flash 写入/读回失败用 0x67),原子清除 pending/source,状态 = FW_STATE_IDLE
```

**ABORT/异常退出统一清理(强制):** 在 RECEIVING 或 STAGED_VALID 收到 0x0505、RECEIVING 掉电恢复、END 校验失败时,均原子写入 `state=IDLE,pending_size/crc/version=0,upgrade_source=NONE`;Staging 物理内容可延迟到下次 `staging_prepare()` 擦除,但不得再被视为有效映像。

**第五阶段:执行安装(INSTALL,0x0504)**

**任何破坏性操作前必须先持久化安装状态与子阶段(掉电可恢复):**

```
主机 → 0x0504
Bootloader 顺序执行(先持久化后应答):
  ① 持久化:状态 = INSTALLING,安装子阶段 = BACKUP_START(校验写入成功)
  ② 应答 ACCEPTED(数据区 0xFF)
  ③ 等待 USART 发送完成
  ④ 备份(正确顺序,先校验后标记):
     a. 擦除备份区
     b. 复制旧 App → 备份区
     c. **校验备份完整性:CRC/向量表(比对 active 元数据)**
     d. 生成 BACKUP manifest → 写 BACKUP_MANIFEST_ADDR → 读回校验
     e. **最后才持久化:子阶段 = BACKUP_VALID + backup 元数据(大小/CRC/版本)**
     (若 c/d 任一失败:备份未标记有效,回滚依据不存在,禁止继续擦除 App)
  ⑤ 擦除 App 区:**先持久化 APP_ERASING 并确认写入成功,才允许擦除**
  ⑥ 安装:暂存区 APP 映像 → App 区:**先持久化 APP_PROGRAMMING 并确认写入成功,才允许写入**
  ⑦ 校验:App 区与暂存区一致(整体 CRC)
     → **最后写 APP manifest(APP_MANIFEST_ADDR)→ 读回校验**
     → 持久化 子阶段 = APP_VALID + pending 元数据
  ⑧ 状态 = FW_STATE_TRIAL_PENDING,失败计数 = 0(不清除升级状态!)
  ⑨ 软件复位 → 启动新 APP(启动确认机制见 十二-5)
```

**安装子阶段掉电恢复规则(强制):**

| 掉电时子阶段 | 恢复动作 |
|---|---|
| BACKUP_START | App 尚未破坏 → **固定恢复为 STAGED_VALID**(保留暂存固件,允许再次 INSTALL);不得保留 INSTALLING/BACKUP_START,也不得无依据改为 IDLE |
| BACKUP_VALID | 备份区有效 → 从备份区回滚 |
| APP_ERASING / APP_PROGRAMMING | 备份区有效 → 从备份区回滚 |
| APP_VALID | 新固件已写入 → 进入 TRIAL_PENDING 启动确认 |
**安装子阶段定义(掉电恢复依据):**

```c
typedef enum {
    INSTALL_BACKUP_START,     /* 备份开始(尚未备份) */
    INSTALL_BACKUP_VALID,     /* 备份完成(备份区有效) */
    INSTALL_APP_ERASING,      /* App 区擦除中 */
    INSTALL_APP_PROGRAMMING,  /* 新固件写入中 */
    INSTALL_APP_VALID         /* 新固件写入并校验完成 */
} install_stage_t;
```

(掉电恢复规则见上方表格:INSTALLING 子阶段恢复矩阵)

**必须持久化的三映像元数据(权威校验来源):**

| 映像 | 元数据(大小/CRC32/版本) |
|---|---|
| active(当前 App 区) | active_image_size / active_image_crc32 / active_version |
| backup(备份区) | backup_image_size / backup_image_crc32 / backup_version |
| pending(暂存区) | pending_image_size / pending_image_crc32 / pending_version |

"校验备份区旧固件完整"必须以 backup 元数据为权威比对来源,而非临时计算。

### 5、启动确认与回滚机制(强制)

```
Bootloader 安装完成 → FW_STATE_TRIAL_PENDING → 启动新 APP
→ APP 完成关键初始化并稳定运行 3s
    ├─ 成功 → APP 写 FW_STATE_CONFIRMED → 正常运行
    └─ 崩溃/死机 → 看门狗复位
→ 再次进入 Bootloader,检测到 TRIAL_PENDING:
    ├─ 读取复位原因:
    │    ├─ 上次复位 = IWDG/HardFault → 失败计数 +1
    │    ├─ 上次复位 = 软件升级复位 → 不计失败
    │    └─ 上次复位 = 普通上电/外部复位 → 不增加
    ├─ 失败计数 < 3 → 再次启动新 APP(允许重试)
    └─ 失败计数 >= 3 → 状态 = ROLLBACK_REQUIRED
        → 从备份区回滚旧固件 → FW_STATE_ROLLED_BACK → 启动旧 APP
```

**APP 写 CONFIRMED 的条件(强制,不止"运行 3 秒"):**

| 条件 | 说明 |
|---|---|
| 调度器正常运行 | FreeRTOS 任务调度启动且无异常 |
| 关键任务心跳上报 | ProtocolTask / SampleTask / ControlTask / HealthTask 均已上报心跳 |
| 关键配置加载成功 | 参数区/外部 Flash 配置读取无致命错误 |
| 无 HardFault / 栈溢出 | 试运行期间无异常发生 |
| 看门狗健康链建立 | HealthTask 已开始正常喂狗 |

**关键规则:**
- 首次启动前**不得清除**升级状态(否则回滚信息丢失)
- 只有 APP 写 CONFIRMED 后,升级才算闭环
- APP 启动早期启用 IWDG;HealthTask 确认所有关键任务心跳正常后才允许喂狗

**HardFault 崩溃标记机制(GD32 复位状态寄存器无独立 HardFault 标志,强制):**
- HardFault Handler 中:
  ① 向 **RTC 备份寄存器(或保留 RAM 的保留区)**写入崩溃标记(如 0xA5F1)
  ② 执行软件复位(或等待 IWDG 复位)
  ③ **不在 HardFault 中直接擦写 Flash**(擦写可能再次触发异常)
- Bootloader 启动时读取"复位标志 + 崩溃标记"组合判断(crash_marker 由状态分派前统一消费,见 十二-9 ③):
  - 有崩溃标记 + TRIAL_PENDING → 计 HardFault 失败并持久化
  - 有崩溃标记 + 其他状态 → 记录普通 App 崩溃,不计升级失败
  - 无标记 + IWDG 复位 → 任务卡死 → 计失败并持久化
  - 软件升级复位 → 不计失败
  - **标记消费后统一清除**(任何分支)

### 6、离线升级流程(TF 卡)

```
上电进入 Bootloader → 检测 TF 卡 /firmware/app.bin
→ 文件不存在 → 跳过,正常等待跳转 App
→ 存在 → 读取 firmware_header_t:
    ├─ header_crc32 校验失败 → 丢弃,跳转 App
    ├─ 按统一版本策略判定:相同版本默认跳过;旧版本仅 flags.bit0 允许降级时接受;flags.bit1 可跳过相同版本/失败包拒绝逻辑
    └─ 版本策略允许 → 校验 image_crc32 / 长度 / 向量表
        → **进入统一暂存流程(强制,防安装中拔卡)**:
          ① **staging_prepare()**:擦除整个 Staging 分区 → 校验擦除结果 → 清零计数;失败则保持旧 App,记录错误并跳过安装
          ② 剥离 firmware_header,把 APP 映像复制到 STAGING_BASE
          ③ 校验暂存映像(CRC/向量表)
          ④ 生成 STAGING manifest → 写 STAGING_MANIFEST_ADDR → 读回校验
          ⑤ 持久化 pending 元数据 + 状态 = STAGED_VALID(upgrade_source = TF_OFFLINE)
          ⑥ **调用统一 INSTALL 流程(与在线升级共用,从内部 STAGING_BASE 搬运)**
        (先完整复制到内部暂存区,安装期间不依赖 TF 卡;中途拔卡不影响安装)
→ 安装完成 → **保留 app.bin**(不重命名)
→ 新 APP 试运行 → APP 写 CONFIRMED(启动确认成功)
→ APP 写 CONFIRMED 并确认元数据提交成功后执行受控软件复位
→ Bootloader 在 CONFIRMED 分支完成 active 提交,并按可恢复文件清理规则将 app.bin → app_<version>_<crc32>.applied(version/crc32 均为 8 位大写十六进制)
   (APP 运行期 FatFs 仍由 StorageTask 唯一拥有;Bootloader 是独立映像,不与 APP 并发,可在启动阶段独占 FatFs)
→ **权威完成条件 = CONFIRMED,不是"搬运完成"**

**失败包隔离(防永久重复安装循环):**
→ 试运行失败回滚后,app.bin **不能继续自动重试**(否则:安装→崩溃→回滚→app.bin 还在→再安装→再崩溃→死循环)
→ Bootloader 记录 failed_package_crc32 / failed_package_version + 失败次数
→ 同一包回滚后:app.bin 重命名为 **app_<version>_<crc32>.failed**(version/crc32 均为 8 位大写十六进制,目标名由失败包元数据确定;**仅在 upgrade_source == TF_OFFLINE 时执行**)
→ 只有以下条件之一才允许再次安装:
  a. 文件哈希变化(新固件包)
  b. 显式强制升级(固件头 flags.bit1)
  c. 人工清除失败记录
```

### 7、可靠性要求(强制)

| 场景 | 行为 |
|---|---|
| BEGIN 元数据非法 | 拒绝,应答 NACK,状态不变 |
| 分片 CRC 错误 | NACK,主机重传该片 |
| 分片序号不连续 | NACK(0x64),主机从断点续传 |
| END 校验失败(长度/CRC32/向量表) | 拒绝安装,原子清除 pending/source,状态 = IDLE |
| 接收中断电 | 状态仍为 RECEIVING → 下次上电原子清除 pending/source,回 IDLE,启动旧 App |
| 搬运中断电 | 备份区保留旧固件 → 检测 INSTALLING 未完成 → 自动回滚 |
| 新固件启动即崩溃 | TRIAL_PENDING + 看门狗计数,累计失败达到 3 次即回滚 |
| 升级成功后首次启动 | APP 稳定 3s 写 CONFIRMED |
| 离线升级重复安装 | CONFIRMED 后由 Bootloader 幂等重命名 `app.bin → app_<version>_<crc32>.applied`;相同版本跳过 |
| TF 卡与在线共用 | 同一套 firmware_header_t 解析 + 校验代码 |
| 离线失败包循环安装 | 回滚后 `app.bin → app_<version>_<crc32>.failed`,记录失败哈希;换新包/强制升级/清记录才能重装 |


### 8、Bootloader 跳转 App 要求(强制)

跳转前必须按顺序执行:

```
① 关闭全局中断 (__disable_irq)
② 停止并关闭 SysTick
③ 反初始化 Bootloader 使用的外设(USART/OLED/SPI 等)
④ 禁止并清除全部 NVIC 已使能中断 (NVIC_ICER/NVIC_ICPR)
⑤ 清除外设挂起标志
⑥ 校验 App 映像合法性:
     - MSP 位于有效 SRAM 范围 (0x20000000~0x20030000)
     - 复位向量满足:reset_addr >= APP_BASE && reset_addr < APP_BASE + active_image_size && reset_addr < APP_MANIFEST_ADDR && (reset_addr & 1U)(不落入 manifest 保留区)
⑦ 设置 SCB->VTOR = 0x08012000 (APP_BASE)
⑧ 设置 MSP = *(uint32_t*)0x08012000
⑨ 跳转复位向量 (*(uint32_t*)(0x08012000 + 4))
⑩ 中断恢复:由 APP 启动代码主动执行 __enable_irq()
    (跳转不会自动清除 PRIMASK;APP 的 SystemInit/启动代码必须重新开中断)
```

### 9、Bootloader 启动流程(完整时序)—— 统一状态分派

> 启动入口位于 MCU 内部 Flash。Bootloader 先完成最小时钟/GPIO 和必要外设初始化,再按升级状态机读取内部元数据以及可选的 GD25Q40E/TF 升级包;外部 SPI Flash 只作为存储介质,不直接提供复位向量。

```
上电 → Bootloader 开始
  ① OLED 显示 "BOOTLOADER"
  ② 读取并校验升级元数据(双槽记录,选有效副本)
  ③ **统一读取 crash_marker(状态分派前,全局消费):**
     if (crash_marker 有效) {
         if (当前状态 == TRIAL_PENDING) { 计一次 HardFault 失败; 持久化 failure_count; }
         else { 记录普通 App 崩溃(不计升级失败,写审计日志); }
         清除 crash_marker;   /* 必须清除,防止残留误判 */
     }
  ④ 按持久化状态统一分派(强制,覆盖全部状态):

     状态 = RECEIVING
       → 暂存区作废(可能接收不完整),原子清除 pending/source,状态回 IDLE
       → 继续 ⑤

     状态 = STAGED_VALID
       → 按 upgrade_source 分派(强制):
         source == ONLINE(1):
           → 等待 RS485 指令:0x0504 INSTALL(执行安装)/ 0x0505 ABORT(原子清除 pending/source,回 IDLE)
           → 等待超时(如 60s)→ 保留暂存映像,启动旧 App(下次上电仍可 INSTALL)
         source == TF_OFFLINE(2):
           → **自动继续 INSTALL 流程**(不掉电等待,离线升级的 STAGED_VALID 由 Bootloader 直接续跑安装)
         → 此路径不经过 ⑤⑥ 的 IDLE 分支

     状态 = INSTALLING
       → 按 install_stage 恢复:
         BACKUP_START   → 固定恢复为 STAGED_VALID,允许重新 INSTALL
         BACKUP_VALID   → 从备份区回滚
         APP_ERASING    → 从备份区回滚
         APP_PROGRAMMING→ 从备份区回滚
         APP_VALID      → 转 TRIAL_PENDING 启动确认

     状态 = TRIAL_PENDING
       → 复位判定(crash_marker 已在 ③ 全局阶段消费并清除,此处只处理无 marker 的复位源):
         if (IWDG 复位) {
             计一次任务卡死失败;
             持久化 failure_count;   /* 必须落盘,否则重启后重读旧计数 */
         } else if (软件升级复位) {
             不计失败;
         } else {
             普通上电/外部复位,不增加;
         }
       → 失败计数 >= 3 → ROLLBACK_REQUIRED → 执行回滚
       → 否则 → 继续启动试运行 APP(跳转)

     状态 = CONFIRMED
       → 先将 upgrade_source 与 pending 元数据复制到 RAM
       → 原子提交 active = pending、清除 pending、state = IDLE;若 failed_package_crc/version 与本次 pending 匹配,同时清除旧失败记录(强制重试成功后不得继续被判为失败包)
       → source == ONLINE(1): 同一次元数据提交中清除 upgrade_source,**不操作 TF 卡**
       → source == TF_OFFLINE(2): 暂时保留 upgrade_source = TF_OFFLINE 作为“文件清理待完成”标志
         → Bootloader 独占 FatFs,校验 app.bin 的版本/CRC 与 active 元数据一致
         → 目标名固定为 `app_<active_version>_<active_crc32>.applied`,执行 app.bin → 目标名
         → 仅当该目标文件存在且其头部版本/CRC 与 active 一致时,才将“app.bin 不存在”视为幂等成功;同名但内容不匹配必须报错并保留 source
         → 成功后再原子清除 upgrade_source
         → 无卡/重命名失败:保留 source 标志并启动已确认 App,下次 Bootloader 启动优先重试,不得重新安装同一包
       → 继续 ⑤

     状态 = ROLLBACK_REQUIRED
       → 若 source == TF_OFFLINE:先将 pending_crc/version 原子写入 failed_package_crc32/version(不得先清除 pending/source)
       → 从备份区回滚旧固件并校验 App/manifest → 状态 = ROLLED_BACK

     状态 = ROLLED_BACK
       → source == ONLINE:原子清除 pending/source、状态回 IDLE → 启动旧 APP
       → source == TF_OFFLINE:清除 pending,保留 source + failed_package 字段作为“失败文件清理待完成”标志
         → Bootloader 校验 app.bin 与 failed_package_crc/version 一致后重命名为 `app_<failed_version>_<failed_crc32>.failed`
         → 成功/文件已改名后清除 source、状态回 IDLE;无卡/失败则保留标志并启动旧 APP,下次启动优先重试且禁止重装同一包

  ⑤ 若 state == IDLE 且 upgrade_source == TF_OFFLINE,先处理遗留文件清理:
     ├─ failed_package 字段匹配 → 幂等执行 app.bin → `app_<failed_version>_<failed_crc32>.failed`(目标文件内容匹配才算成功)
     ├─ 否则 active 字段匹配 → 幂等执行 app.bin → `app_<active_version>_<active_crc32>.applied`(目标文件内容匹配才算成功)
     └─ 无卡/失败 → 保留 source 标志,直接启动有效 App,本次不再执行 ⑥~⑧
     清理待完成期间禁止接受新的在线/离线升级;否则 BEGIN 会覆盖 upgrade_source 与清理依据。恢复方式只有:插回原 TF 卡完成幂等清理,或满足“TF 已挂载且 app.bin 不存在”后执行维护 CLI `upgrade cleanup clear CONFIRM`
  ⑥ 检测升级请求标志(APP 请求升级)?
     ├─ 有 → 清除标志,状态 = IDLE,发送 Bootloader Ready 事件帧
     │        → 延时 10s 等待 0x0501 BEGIN(进入在线升级流程)
     └─ 无 → 继续
  ⑦ 检测 TF 卡 /firmware/app.bin?(仅当状态 = IDLE 且无未完成文件清理)
     ├─ 有且版本新 → 进入离线升级流程
     └─ 无/相同版本 → 继续
  ⑧ 等待 RS485 指令 5s(状态 = IDLE,仅接受 0x0501 BEGIN)
     ├─ 收到 0x0501 → 进入在线升级流程(十二-4)
     └─ 无 → 跳转 App(见 十二-8)
```

### 10、升级元数据双槽记录(强制,掉电原子性)

**内部参数区升级元数据采用双槽记录(slot A / slot B),写入带提交标志:**

```c
typedef struct {
    uint32_t magic;            /* 固定 0x554D4454("UMDT") */
    uint32_t meta_version;     /* 元数据格式版本 = 1 */
    uint32_t generation;       /* 代数,单调递增,选择有效副本依据 */
    uint8_t  state;            /* firmware_state_t */
    uint8_t  install_stage;    /* install_stage_t */
    uint8_t  failure_count;    /* TRIAL 失败计数 */
    uint8_t  upgrade_source;   /* 升级来源:0=NONE, 1=ONLINE(RS485), 2=TF_OFFLINE */
    /* 三映像元数据(权威校验来源) */
    uint32_t active_size,   active_crc32,   active_version;
    uint32_t backup_size,   backup_crc32,   backup_version;
    uint32_t pending_size,  pending_crc32,  pending_version;
    /* 离线失败包隔离 */
    uint32_t failed_package_crc32;
    uint32_t failed_package_version;
    uint32_t crc32;            /* magic~failed_package_version 的 CRC32(不含 crc32 自身与 commit_marker) */
    uint32_t commit_marker;    /* 固定 0xA5C3C3A5,最后写入 = 提交完成 */
} upgrade_meta_t;

/* 固定长度校验:AC5 与 GCC 双工具链布局必须一致(头部16B+三映像36B+失败包8B+crc32 4B+marker 4B=68B);更工程化:定义固定 68B 序列化格式,不依赖结构体布局 */
_Static_assert(sizeof(upgrade_meta_t) == 68, "upgrade_meta_t must be 68 bytes");
```

**写入与选择规则(强制):**

| 规则 | 说明 |
|---|---|
| 双槽轮换 | 每次写入交替使用 slot A / slot B(各自独立 4KB 页);写前先擦除目标槽所在页(另一槽不受影响) |
| 原子提交 | 先写全部字段(不含 crc32/commit_marker)→ 写 crc32 → **最后写 commit_marker**(0xA5C3C3A5);crc32 计算范围 = magic 至 failed_package_version,不含自身与 commit_marker |
| 掉电恢复 | 启动时扫描两槽:选择"CRC 正确 + commit_marker 有效 + generation 最大"的记录 |
| 无有效槽 | **不得直接启动旧 App**(掉电可能发生在 App 擦除阶段,App 已不完整):<br>① 读取 App manifest,验证 size/CRC + MSP/复位向量;<br>② App 有效 → 重建并原子提交元数据(active=App manifest,state=IDLE,generation=1,source=NONE,写 slot A 并校验)→ 启动;<br>③ App 无效 → 用 Backup manifest 校验并恢复,再重建元数据;<br>④ Backup 也无效 → 驻留安全升级模式,仅等待 0x0501 BEGIN |
| 状态与映像元数据同槽 | state 与 active/backup/pending 元数据在同一记录内一次写入,保证原子性 |
| CONFIRMED 后 | active = pending(提交新映像为活动)→ 清除 pending → 状态回 IDLE;ONLINE 同时清 source,TF_OFFLINE 保留 source 直到带版本/CRC 的 `.applied` 目标文件清理成功 |
| ROLLBACK 后 | TF_OFFLINE 先保存 failed_package_crc/version,回滚完成后保留 source 直到带版本/CRC 的 `.failed` 目标文件清理成功;任何掉电点均可在下次启动幂等续做 |
| 下次升级 | backup 在 INSTALL 时轮换(当前 active → backup) |

### 11、Flash 擦除粒度(强制)

**GD32F470 内部 Flash 支持 4KB 页擦除,参数区(4KB)可行,但必须:**

- 参数区擦除**必须使用 `fmc_page_erase()`**(4KB 页擦除)
- **禁止**根据地址换算后调用 `fmc_sector_erase()`(传统 64KB 扇区)——参数区位于扇区内部,误用扇区擦除会连同 App 区前部一起擦掉
- 所有分区边界(0x08010000/0x08011000/0x08012000 等)均为 4KB 对齐,页擦除安全

---

## 十三、RS485 自动上报(0x0302)

### 1、启动自动上报

上位机下发 0x0302 后,设备进入自动上报模式:

```
应答首帧: 正常应答帧(0x0302,数据区 0xFF = OK)
后续: 每"上报间隔"秒发送事件帧(帧类型 05,命令字 0x0382):
  数据区: 时间戳(4B) | CH0(4B IEEE754) | CH1(4B IEEE754)
```

**采样与上报周期独立(强制):**

| 周期 | 控制 | 默认 |
|---|---|---|
| 采样周期 | CLI/按键/菜单(5/10/15s) | 5s |
| 上报间隔 | RS485 0x0304 设置(2B 秒) | 5s |

- 两个周期**互不绑定**:采样周期 15s 时,上报间隔可单独设 5s(上报使用最近一次采样值)
- 上报数据来自共享区**最新采样**(采集引擎持续运行)
- **采集引擎常驻**:自动上报开启时,即使 CLI/按键未启动采样,采集引擎也持续运行(否则上报的是旧数据)

### 2、数据格式

| 字段 | 字节数 | 说明 |
|---|---|---|
| UTC 秒级时间戳 | 4 | 自 1970-01-01 |
| CH0 数据 | 4 | IEEE754 单精度浮点,已乘变比 |
| CH1 数据 | 4 | IEEE754 单精度浮点,已乘变比 |

### 3、自动上报期间的命令屏蔽

| 命令 | 行为 |
|---|---|
| 0x0303(停止自动上报) | ✅ 响应 |
| 0x03AA(睡眠) | ✅ 响应 |
| 其他所有命令 | 返回错误码 0x05(设备忙) |

### 4、停止自动上报

收到 0x0303 后停止上报,应答 OK;**LED2 仅在"采样与上报均关闭"时熄灭**(本地采样仍在运行则保持点亮)

### 5、独立状态拆分(强制)

**"采样""存储""RS485 上报""告警"是四个独立使能状态,互不隐含:**

| 使能位 | 控制源 | 说明 |
|---|---|---|
| sensor_engine_enabled | 系统自动(常驻) | **ADC 底层固定周期(100ms)采集**,不随业务开关 |
| local_sample_enabled | CLI `start/stop`、KEY1、菜单 | CLI 打印与 TF 存储周期(5/10/15s) |
| storage_enabled | 系统自动 | TF 卡存储通道是否可用(插卡且未满) |
| should_store | 组合条件 | **should_store = local_sample_enabled && storage_enabled**(最终是否落盘) |
| custom_report_enabled | RS485 0x0302/0x0303 | 是否周期向 RS485 发数据帧(间隔 0x0304) |
| alarm_monitor_enabled | 系统自动(常驻) | 告警检测始终开启(消费底层快速采样结果) |
| alarm_report_mode | RS485 0x0601 | 01 主动上报 / 02 仅存储待查询 |

**采样引擎设计(强制):**
- **ADC 底层固定 100ms 采集一次**(sensor_engine_enabled 常驻),写入共享区
- CLI 采样周期(5/10/15s)**只控制打印与 TF 存储周期**,不影响底层采集
- RS485 上报间隔(0x0304)**独立控制网络上报**
- 告警任务消费底层快速采样结果(响应更快,且不受"采样周期 15s"拖累)

**联动规则(消除歧义):**

| 场景 | 行为 |
|---|---|
| CLI `stop` 停止采样 | 仅清 local_sample_enabled(停打印/停存储);**底层采集与 RS485 上报不受影响** |
| RS485 0x0303 停止上报 | 仅清 custom_report_enabled;底层采集与 TF 存储不受影响 |
| 菜单启动采样 | 仅开 local_sample_enabled;不自动开启 RS485 上报 |
| 睡眠唤醒 | 按睡眠前保存的使能位**原样恢复**(底层采集引擎恢复常驻) |
| 自动上报开启 | 底层采集引擎已常驻,上报永远用最新数据(不会发旧值) |

---

## 十四、验收评分标准

### 1、功能验收项(逐条对照)

| 编号 | 验收项 | 通过标准 |
|---|---|---|
| A-01 | 逐地址查询设备 ID | 应答正确,ID 与配置一致 |
| A-02 | 广播静默规则 | 合法广播写命令被执行但总线无设备应答;广播查询被静默丢弃 |
| A-03 | 发送重启指令 | 应答正确 |
| A-04 | 重启后心跳 | 15s 内收到心跳,ID 一致 |
| B-01~B-06 | 查询固件版本/时间/波特率/CH0/CH1/阈值 | 应答正确,数值合法 |
| C-01 | 设置时间并回读 | 回读一致 |
| D-01~D-02 | CH0/CH1 变比即时生效 | 查询值与设定一致 |
| E-01~E-02 | CH0/CH1 阈值设置与回读 | 两处回读一致 |
| F-01~F-04 | 重启后变比/阈值持久化 | 重启后参数不变 |
| G-01 | DAC 联动 | DAC 设定与回读一致 |
| H-01 | 自动上报完整测试 | 周期正确,格式正确 |
| H-02 | 上报期间命令屏蔽 | 除停止外不应答 |
| H-03 | 停止自动上报 | 应答正确且停止 |
| I-01 | 开启主动告警 | 应答正确 |
| I-02 | 触发告警并验证 | 主动上报格式正确 |
| I-03 | 关闭告警后再触发 | 无主动上报但有记录 |
| I-04 | 清除告警记录 | 查询返回 empty |
| J-01 | 睡眠后自动唤醒 | 应答、睡眠、唤醒后应答正确 |
| K-01~K-03 | CRC 错/长度错/非法命令 | 返回错误应答帧 |
| L-01 | 修改设备 ID | 生效且持久化 |
| M-01 | 修改波特率 | 生效且持久化 |
| N-01 | 正确固件升级 | 升级成功,版本更新 |
| N-02 | 错误固件拒绝 | 返回 ERROR,旧固件可用 |
| N-03 | BEGIN 后/暂存擦除中断电 | 下次启动清除 RECEIVING 上下文,旧 App 可运行 |
| N-04 | END 成功后、INSTALL 前断电 | STAGED_VALID 与暂存映像保留;在线等待 INSTALL,离线自动续装 |
| N-05 | 备份/App 擦写阶段随机断电 | 备份有效时自动恢复旧 App;不存在跳入损坏 App 的路径 |
| N-06 | 试运行连续故障 | 第 3 次 IWDG/HardFault 失败后回滚,旧 App 可运行 |
| N-07 | CONFIRMED 提交/成功文件改名时断电 | 新 App 保持 active,下次启动幂等完成 `.applied` 清理 |
| N-08 | 回滚/失败文件改名时断电 | 可重复回滚且最终生成匹配版本/CRC 的 `.failed`,不循环安装 |
| N-09 | 在线升级与 TF 隔离 | `upgrade_source=ONLINE` 的全部路径均不创建、删除或改名 TF 固件文件 |
| N-10 | 离线清理未完成 | 仍可启动有效 App,但新在线/离线升级返回状态不允许,清理后恢复 |
| O-01 | Modbus 寄存器读写 | 03/06/10 功能码正确 |
| P-01 | TF 卡采样存储 | 文件正确生成,10 条滚动 |
| P-02 | TF 卡告警存储 | 告警文件正确 |
| P-03 | 审计日志 | 上电次数自增 |
| Q-01 | 参数掉电保持 | 重启后参数一致 |
| Q-02 | 拔卡降级 | 拔卡后系统继续运行 |

### 2、稳定性验收

| 项目 | 标准 |
|---|---|
| 连续运行 | 24h 无看门狗复位、无任务溢出 |
| 掉电测试 | 采样/告警写入中随机断电 20 次;重新上电可正常挂载,已完成 f_sync 的记录完整,最多丢失正在写入的一条记录,参数保持一致 |
| 异常输入 | 连续发送 100 个错误帧,设备不死机 |

---

## 十五、项目里程碑

| 阶段 | 内容 | 验收标准 |
|---|---|---|
| M0 硬件资源冻结 | 引脚/时钟/DMA 通道/中断优先级/Flash 擦除边界表 | 资源表完成,无冲突 |
| M1 工程地基 | Boot/App 双工程、链接地址、向量跳转、最小 LED/OLED 验证 | 上电 Boot 5s 后跳 App,OLED 显示切换 |
| M2 公共基础组件 | RingBuffer、CRC、CLI、Flash KV、协议编解码(单元测试) | 组件级测试通过 |
| M3 FreeRTOS APP 骨架 | 七任务、队列、事件组、FatFs 单任务所有权、看门狗监控 | 连续 24h 无看门狗复位 |
| M4 采集与通信 | ADC/DMA、DAC 回读、自定义协议、Modbus,Python 回归测试同步加入 | A/B/C 类验收过,协议测试脚本通过 |
| M5 TF 卡与告警 | 存储滚动、拔卡恢复、配置原子导入、告警状态机 | P 类验收过 |
| M6 Bootloader | 在线/离线升级、启动确认、回滚、随机断电测试 | N 类验收项全过 |
| M7 低功耗与完整验收 | 全功能 24h 运行、协议异常注入、TF 拔插、升级断电测试 | 全部验收项通过 |

---

## 十六、FreeRTOS 软件架构(附)

### 1、任务总览

| 任务 | 职责 | 优先级 | 栈大小 | 周期/触发 |
|---|---|---|---|---|
| ProtocolTask | RS485 帧解析、CRC、命令分发 | 高(5) | 256 words(≈1KB) | 事件触发(队列) |
| SampleTask | ADC/DAC 采集、滤波、变比换算(消费常驻采样结果) | 较高(4) | 256 words(≈1KB) | ADC DMA 完成事件 / 100ms 触发 |
| StorageTask | APP 持久化 IO 唯一执行上下文:FatFs/TF + GD25Q40E Persistence Service,处理文件/KV/告警/审计请求 | 中(3) | 512 words(≈2KB) | 事件触发(队列) |
| AlarmTask | 阈值判断、告警状态机;只产生告警事件与持久化请求,不直接访问 Flash/TF | 中(3) | 256 words(≈1KB) | 事件触发(采样结果队列) |
| ControlTask | 系统协调器:决定状态迁移并异步调度服务;不亲自执行 FatFs、Flash GC、协议编码或显示 IO | 中(3) | 256 words(≈1KB) | 事件触发(命令/按键/完成消息) |
| DisplayTask | 执行 Display/Indicator Service 刷新;运行 ebtn 扫描并只上报按键事件,不直接修改业务状态 | 低(2) | 256 words(≈1KB) | 周期性(10ms 按键扫描/500ms 显示) |
| HealthTask | 按任务健康契约检查 deadline/alive/busy/豁免状态,满足条件后喂狗并报告睡眠就绪 | 高(5) | 128 words(≈0.5KB) | 周期性(1s) |

**静态创建约束:** 本项目所有 APP 任务使用 `xTaskCreateStatic()` 创建,每个任务必须显式提供 `StaticTask_t` 控制块和 `StackType_t[]` 栈数组;上表栈深度单位是 `StackType_t`(4 字节 word),非字节。禁止在 APP 中使用 `xTaskCreate()`。

**静态对象约束:** 队列使用 `xQueueCreateStatic()`;事件组使用 `xEventGroupCreateStatic()`;互斥锁/信号量使用对应的 `...Static()` API;软件定时器使用 `xTimerCreateStatic()`。启用软件定时器时,还必须通过 `vApplicationGetIdleTaskMemory()` 和 `vApplicationGetTimerTaskMemory()` 提供 IdleTask、Timer Service Task 的静态内存。FreeRTOS 配置固定为 `configSUPPORT_STATIC_ALLOCATION=1`、`configSUPPORT_DYNAMIC_ALLOCATION=0`,不编译/不链接 `heap_4.c`。

**优先级说明:** FreeRTOS 数字越大优先级越高。ProtocolTask 与 HealthTask 同高优先级(协议实时性 + 喂狗不延迟);DisplayTask 最低(可被抢占)。

### 2、任务间通信全景

```
USART IDLE/DMA ──► ProtocolTask ──请求──► ControlTask ──控制──► SampleTask
                         ▲   │                    │
                         │   └──主动事件◄── AlarmTask ◄── SampleTask
                         │                        │
                         └──异步应答─────────────┘

按键/CLI ──► ControlTask ──持久化/flush 请求──► StorageTask
                    ▲                              │
                    └────完成消息(request_id)──────┘

AlarmTask ──告警落盘请求──► StorageTask ──独占──► FatFs/TF + GD25Q40E
HealthTask ──sleep_ready──► ControlTask ◄── DisplayTask 按键事件
StorageTask ──busy/progress──► HealthTask
SampleTask ──最新值快照──► DisplayTask(只读)
```

| 通信对象 | 机制 | 大小/长度 |
|---|---|---|
| USART IDLE → ProtocolTask | 任务通知 | 1 次通知/数据批次(IDLE 不保证帧边界,帧解析由 RingBuffer 状态机完成) |
| ADC DMA → SampleTask | 二值信号量 | — |
| SampleTask → AlarmTask | 消息队列(采样结果) | 8 项 × 16B |
| SampleTask → StorageTask | 消息队列(存储请求) | 16 项 × 32B |
| ProtocolTask → ControlTask | 请求队列(命令分发,含 request_id/来源/协议序列号) | 8 项 × 24B |
| ControlTask → ProtocolTask | 完成队列(执行结果/异步应答,回显 request_id/协议序列号) | 8 项 × 24B |
| ebtn 按键/CLI → ControlTask | 消息队列(导航/指令事件) | 8 项 × 8B |
| ControlTask → StorageTask | Persistence 请求队列(config 读取/保存/KV/审计/flush) | 8 项 × 24B |
| StorageTask → ControlTask | 完成队列(配置读取/写盘/flush 结果) | 8 项 × 16B |
| ControlTask → SampleTask | 消息队列(周期修改) | 2 项 × 4B |
| AlarmTask → ProtocolTask | 主动告警事件队列 | 8 项 × 24B |
| AlarmTask → StorageTask | 告警 TF/Flash 持久化请求 | 8 项 × 32B |
| HealthTask → ControlTask | 任务通知 + 状态码(sleep_ready/health_fault) | 单槽通知 |
| StorageTask → HealthTask | 原子健康槽(busy 标志 + progress 计数 + operation_deadline) | 1 项共享槽 |
| 系统状态(升级/睡眠/告警) | 事件组 | 8 位 |
| 共享区(通道值/状态) | 互斥锁/临界区 | — |

### 2.1、任务消息协议(强制)

- 每个跨任务请求包含:`request_id(32 位单调递增) + origin + operation + protocol_sequence + deadline_tick + payload`
- 完成消息必须回显 `request_id` 与 `protocol_sequence`;ProtocolTask 只负责协议编码/发送,不得直接执行业务或持久化操作
- ControlTask 使用固定大小 pending 表维护异步命令状态,收到完成消息后再触发协议应答;**禁止持锁等待、禁止无界等待、禁止跨任务函数回调**
- 超时后从 pending 表移除请求并返回超时错误;迟到完成消息只记审计并丢弃,不得复用到新的请求
- 队列满时按消息等级处理:控制/告警/升级请求返回 BUSY,普通采样记录允许计数后丢弃;不得永久阻塞生产者
- 消息 payload 优先值拷贝或静态对象池索引;禁止传递任务栈指针,任何指针都必须有明确所有权和生命周期

### 3、资源所有权清单(强制)

| 资源 | 所有者 | 访问规则 |
|---|---|---|
| FatFs / TF 卡 | StorageTask | 唯一所有者,其他任务只发 Persistence 请求 |
| 外部 Flash(GD25Q40E) | APP:StorageTask;Bootloader:升级状态机 | APP 侧 Persistence Service 独占参数/告警访问;Bootloader 仅在升级阶段独占访问元数据槽/升级包/备份镜像,其他任务不得直接擦除/GC/持 SPI 锁 |
| SPI 总线(GD25Q40E) | 当前执行上下文内部驱动 | APP 与 Bootloader 不并行访问;切换执行上下文前完成外设收尾,未来多设备时由 BSP 总线驱动内部串行化 |
| I2C 总线(OLED) | DisplayTask | 单一所有者;其他任务提交显示状态,不直接访问 I2C |
| USART1(RS485) | ProtocolTask 发送/ISR 接收 | 发送互斥锁 |
| USART0(调试 CLI) | ControlTask(ISR 仅收数据入队) | 发送互斥锁 |
| RTC | 系统服务 | 临界区(读时间短) |

### 3.1、HealthTask 健康契约(强制)

| 任务类型 | 健康判据 |
|---|---|
| 周期任务(Sample/Display) | 在各自周期 deadline + 容差窗口内更新完成序号,不能只机械递增心跳 |
| 事件任务(Protocol/Control/Alarm) | 阻塞等待队列属于健康状态;每次唤醒/处理完成更新 alive counter,并监控队列长期堆积 |
| 长 IO(Storage) | 使用 **busy-but-alive** 契约:操作开始设置 busy + operation_deadline,分块 IO 更新 progress;progress 停滞或超过对应操作 deadline 才判故障 |
| 合法豁免 | 睡眠、Bootloader 升级、系统受控复位阶段使用明确 event bit 豁免对应任务,不得通过伪造心跳喂狗 |
| HealthTask 自身 | 由硬件 IWDG 约束;HealthTask 不满足其他任务健康条件时故意停止喂狗 |

### 4、看门狗策略

**Bootloader FWDGT 接管策略(强制):**

| 场景 | 行为 |
|---|---|
| APP → Bootloader | APP 使能的 FWDGT 无法在运行期停止(标准库无 stop 接口);Bootloader 启动后调用 `fwdgt_config()` 重新配置并立即重载(该函数已启动 FWDGT,不重复调用 `fwdgt_enable()`) |
| Bootloader 长等待 | 60s 等待/大块 Flash 擦写/CRC 计算期间,**在循环中喂狗**(`fwdgt_counter_reload()`) |
| 擦除/搬运 | 分块执行,每块间喂狗,避免单次擦除超时 |
| 跳转 APP 前 | 保持 FWDGT 运行(APP 启动早期接管);APP 侧先喂狗再建立 HealthTask 健康链 |

| 项 | 设计 |
|---|---|
| 看门狗类型 | 独立看门狗 IWDG(硬件,不受主频影响) |
| 喂狗者 | **正常调度阶段仅 HealthTask**;调度器启动前允许 APP 启动代码执行一次受控喂狗,随后立即移交 HealthTask |
| 喂狗条件 | **先检查全部任务心跳,全部正常才喂狗**(任一任务卡死 → 故意不喂 → 触发复位) |
| 超时 | **5s(初版)**(HealthTask 1s 周期;TF 重挂载/SPI 擦除可能 >1s,余量不足;板上测最坏执行时间后再缩短) |
| 崩溃检测 | 任务心跳超时 → 不喂狗 → IWDG 复位 → Bootloader 启动确认计数(见 十二-5) |
| 复位原因 | **仅在 TRIAL_PENDING** 时:IWDG/HardFault 复位计启动失败;软件升级复位不计;普通断电/外部复位不清除但也不增加。其他状态的 IWDG/HardFault 仅记普通崩溃审计 |
| 调试注意 | 调试断点暂停会触发 IWDG 复位;调试时可用"调试模式关看门狗"宏 |

### 5、内存预算(预估)

| 区域 | 预估 | 说明 |
|---|---|---|
| **GD32F470 内部 Flash** | **512KB** | 存放 Bootloader、App 和内部元数据/参数区;这是程序与只读数据空间,不是 FreeRTOS heap |
| **普通 SRAM** | **192KB** | 任务运行数据、普通全局变量和 DMA 缓冲区;DMA 缓冲区必须放这里,不能放 TCM |
| **TCMRAM** | **64KB** | 高速 TCM 区;总 RAM = 192KB + 64KB |
| **FreeRTOS 静态对象总预算** | **约 16KB（初始预算）** | 包含七个业务任务栈约 7.5KB、任务控制块、IdleTask/Timer Service Task、静态队列、事件组、互斥锁、信号量和软件定时器;这是 RAM 规划预算,不是 `heap_4` 动态池 |
| 环形缓冲区(静态) | 2KB | RS485 接收(≥2048B,容纳最大帧 1040B) |
| FatFs 工作区(静态) | ~2KB | 文件系统内部 |
| 业务全局数据(静态) | ~4KB | 协议/配置/状态 |
| **当前软件预算小计** | **~24KB / 256KB** | 仅为当前规划的 heap/静态对象粗略预算,不是链接器最终 `RAM Used`;最终以 `.map`、任务栈水位和运行时余量为准 |

**静态内存与芯片容量的关系:** 上述 16KB 是从 256KB RAM 中规划给 FreeRTOS 静态对象的预算,不表示芯片只有 16KB RAM,也不表示从 512KB Flash 中拿出 16KB。静态对象会直接进入 `.bss`/指定 RAM 段,最终以 `.map` 文件和栈水位检查为准;如果队列、Timer Service Task 或任务栈实际增加,应调整静态数组和总 RAM 预算。

**当前七任务栈预算:** `256 + 256 + 512 + 256 + 256 + 256 + 128 = 1920 words`;在 `StackType_t` 为 4 字节时约为 `7680B`。这 7.5KB 只是七个业务任务的栈,不包含 TCB、IdleTask、Timer Service Task、队列和其他内核对象。GCC 链接脚本中的 `__heap_size = 1K` 属于裸机/newlib 的链接器堆预留;本项目 FreeRTOS 关闭动态分配后,不使用 `heap_4`。

**`heap_4` 边界:** `heap_4.c` 是 FreeRTOS 的一种动态内存管理实现,服务于 `pvPortMalloc()`/`vPortFree()` 以及 `xTaskCreate()`、`xQueueCreate()`、`xTimerCreate()` 等动态 API。它支持释放后相邻空闲块合并,但仍属于动态分配机制。本项目全部使用静态 API,因此不需要 `heap_4.c`;若第三方中间件调用 `pvPortMalloc()`,必须改为固定缓冲区或静态对象,否则就重新引入了动态内存。

---

*本文档为项目任务书,技术选型见《02_TECH_STACK.md》。开发时逐条实现,验收时逐条对照。*

---

## 十七、统一分层与 BSP 设备抽象设计(借鉴 PX4 板级配置思想)

> 本章定义唯一有效的软件分层、Boot/App 公共契约、BSP 边界与初始化规则。后续目录命名和依赖审查均以本章为准。

### 1、设计目标

| 目标 | 说明 |
|---|---|
| 唯一依赖模型 | `Tasks/Application → Domain Services → Protocol/Middleware → BSP/Device Drivers → GD32 SPL/CMSIS`,禁止反向依赖 |
| Common 公共契约 | Bootloader 与 APP 共用 Flash 布局/升级格式/序列化/CRC/复位契约,禁止复制定义 |
| 配置与代码分离 | **同 GD32F470 系列且外设能力一致的换板**主要修改 `board_config.h + board_dma_map.h`;跨芯片仍需新增后端 |
| 可测试 | 协议、升级序列化、状态机、服务逻辑可在 PC 上编译;硬件寄存器访问隔离在 BSP |
| 稳定接口 | 业务调用 Service/中间件接口,BSP 只提供纯硬件能力,避免 FatFs/UI/状态机语义下沉 |

### 2、目录结构

```
IndustrialEmbedded/
├── Common/                     ← Boot/App 共同编译,纯 C、无 RTOS/FatFs/UI/GD32 寄存器依赖
│   ├── common_flash_layout.h   ← Boot/Meta/App/Backup/Staging 地址与容量
│   ├── common_fw_format.c/h    ← V1 固件头/Manifest 逐字段编解码
│   ├── common_upgrade_meta.c/h ← 68B 元数据固定序列化/CRC/commit 规则
│   ├── common_crc_profile.c/h  ← CRC16/CRC32 参数与测试向量
│   ├── common_reset_contract.h ← 规范化复位原因/启动确认协议
│   └── common_upgrade_error.h  ← 升级错误码
├── BSP/                        ← 纯硬件与板级配置
│   ├── Boards/gd32f470ve_v1/
│   │   ├── board_config.h      ← 端口/引脚/时钟/AF/极性/设备实例
│   │   └── board_dma_map.h     ← DMA 静态分配与冲突检查
│   ├── bsp_gpio.c/h
│   ├── bsp_uart.c/h            ← USART/DMA/DE 原始收发
│   ├── bsp_adc.c/h             ← ADC/DMA 原始采样
│   ├── bsp_dac.c/h
│   ├── bsp_oled_hw.c/h         ← OLED 写命令/显存,不含页面状态
│   ├── bsp_spi_flash_raw.c/h   ← GD25Q40E read/program/erase
│   ├── bsp_sdio_block.c/h      ← TF 块设备 read/write/status
│   ├── bsp_rtc_raw.c/h
│   ├── bsp_pmu_hw.c/h          ← 仅硬件低功耗进入/恢复
│   └── bsp_timebase.c/h        ← SysTick/硬件定时器中断入口
├── Middleware/                 ← 通用机制/适配器
│   ├── Protocol/               ← 自定义协议 + Modbus RTU 编解码
│   ├── FatFsAdapter/           ← diskio + mount/unmount
│   ├── FlashKV/                ← 双扇区 KV/GC,调用 raw Flash
│   ├── RingBuffer/
│   └── Ebtn/
├── Services/                   ← 领域服务,不拥有独立线程
│   ├── Persistence/            ← 文件/KV/告警/审计接口,由 StorageTask 执行
│   ├── Display/                ← 页面内容与 OLED 显示策略
│   ├── Indicator/              ← LED 状态/进度/闪烁状态机
│   ├── PowerManager/           ← 睡眠静默与多任务协调
│   └── TimeService/            ← UTC/RTC/文件时间
├── Tasks/                      ← 七任务入口与消息循环
├── Bootloader/                 ← 裸机升级状态机,只依赖 Common+BSP+所需中间件子集
├── App/                        ← APP 组合根、业务状态与配置模型
├── Libraries/                  ← GD32 SPL(不动)
└── Driver/                     ← CMSIS(不动)
```

### 3、Common 公共契约层(强制)

- Bootloader 与 APP 必须从同一个 Common 目录引用地址、枚举、错误码、CRC profile 与序列化代码,禁止复制粘贴定义
- `firmware_header_t/image_manifest_t/upgrade_meta_t` 可以作为 RAM 中逻辑对象,但 Flash/文件存储必须调用 `common_*_encode/decode()` 逐字段处理,**禁止 memcpy 结构体作为持久化 ABI**
- `common_reset_contract.h` 只定义 `RESET_REASON_IWDG/HARDFAULT/SOFTWARE/POWER_ON/EXTERNAL` 等规范化枚举;GD32 RCU/RTC 寄存器到该枚举的映射仍在 Boot/BSP 实现
- Common 只能依赖 `<stdint.h>/<stdbool.h>/<stddef.h>`,不得包含 FreeRTOS、FatFs、OLED、GD32 外设头或任务句柄
- Common 变更必须同时通过 AC5/GCC 编译和 Python 测试向量;固件格式变更必须提升 `package_version/meta_version`,不得静默改变布局

### 4、板级配置表(board_config.h / board_dma_map.h)

- `board_config.h` 集中每个外设的**端口/引脚/时钟/AF/电平极性/默认实例**,运行期可配置的波特率和业务参数不放入板级宏
- `board_dma_map.h` 集中 DMA 控制器/通道/子外设映射,每项带资源编号与编译期冲突检查;新增外设必须先通过资源表审查
- 同系列且外设能力一致的换板主要修改这两个文件和板目录;更换 MCU 系列必须新增时钟、中断、DMA、Flash、低功耗后端,不能宣称只改宏即可移植

### 5、BSP / Middleware / Service 接口边界

| 层 | 允许职责 | 禁止事项/示例 |
|---|---|---|
| BSP/Device Drivers | GPIO、UART/DMA、ADC/DAC、Flash 原始读写、SDIO 块读写、RTC 原始时间、硬件睡眠入口 | 不 mount FatFs、不做 Flash KV GC、不生成页面文本、不协调任务 |
| Protocol/Middleware | 自定义协议/Modbus 编解码、FatFsAdapter、Flash KV、RingBuffer、ebtn | 不决定采样/告警/睡眠业务状态,不直接发送跨任务业务命令 |
| Domain Services | Persistence、Display、Indicator、PowerManager、TimeService 等可复用业务能力 | 不拥有线程;由任务调用,不得直接依赖其他任务入口 |
| Tasks/Application | 消息循环、异步编排、超时、状态迁移 | 不直接访问 GD32 SPL,不跨层持有 SPI/I2C 锁 |

**固定硬件接口原则:** LED/ADC/RTC/UART 等固定路径使用静态类型接口与编译期设备 ID,不使用字符串 `open("uart1_rs485")`。ops 函数表只允许出现在确有多后端的服务边界(例如 Persistence PC mock/MCU backend),不得为每个简单 BSP 外设建立运行时虚表。

### 6、初始化机制(关键路径显式,可选模块受限注册)

**Bootloader 全部显式初始化(强制):** 时钟/最小 GPIO → FWDGT 接管 → Common 元数据读取 → FMC/RS485 → 按需要初始化 OLED/SDIO/FatFs → 状态机。Bootloader 禁止 section 自动注册,保证启动顺序可审计且不会因链接裁剪丢失关键模块。

**APP 核心路径显式初始化(强制):** 时钟与内存 → Common/升级确认接口 → 必需 BSP → Middleware/Services → 队列/事件组/对象池 → 七任务 → 启动调度器。显式初始化函数集中在 APP composition root,新增核心依赖时允许修改初始化清单,不追求“main 永不修改”。

**可选自动注册(默认关闭):** 仅允许 APP 的非关键可选设备模块使用,不得注册 `app_init`、时钟、Flash、FWDGT、升级元数据、RTOS 对象或持久化服务。

```c
#if APP_ENABLE_OPTIONAL_INIT_EXPORT
#define APP_OPTIONAL_INIT_EXPORT(fn, level) /* 工具链适配后的 section 注册 */
APP_OPTIONAL_INIT_EXPORT(optional_demo_sensor_init, 30);
#endif
```

- AC5 scatter file 与 GCC linker script 必须分别定义边界符号并使用 `KEEP`,防止 `--gc-sections`/LTO 回收注册项
- level 使用数值并由链接脚本明确排序,禁止依赖字符串段名的偶然字典序
- 若项目没有至少两个真实可插拔 APP 模块,首版不实现自动注册,仅保留设计说明

### 7、简历价值

> "设计 Common+BSP+Middleware+Service+Task 单向分层架构:Boot/App 共享固定序列化契约,板级引脚与 DMA 资源集中配置,StorageTask 单一持久化所有权;协议/状态机可在 PC 测试,支持 GD32F470 同系列换板与后端扩展。"

### 8、PX4 外设封装模式分析(借鉴参考)

**PX4 的封装三件套(不适合直接搬,但思想可借鉴):**

| 机制 | PX4 做法 | 我们是否采用 |
|---|---|---|
| 设备文件 | `/dev/ttySx`、open/read/write/ioctl | ❌ 太重,无设备文件层;固定外设使用枚举 ID + 静态类型接口 |
| uORB 消息总线 | 发布/订阅,业务发消息驱动收 | ❌ 我们有 FreeRTOS 队列/事件组,不重复造 |
| 板级宏 | board_config.h 打包引脚配置 | ✅ 采用(board_config.h/board_dma_map.h) |

**各外设的 PX4 封装方式与我们的对应设计:**

| 外设 | PX4 封装 | 我们的借鉴 |
|---|---|---|
| **LED** | LedController 状态机:业务发语义(ARMED/OVERLOAD),驱动按模式执行 | BSP 只提供 `bsp_gpio_write(BSP_GPIO_LED_SYS, level)`;`IndicatorService` 接收 NORMAL/ALARM/UPGRADING 等语义并执行闪烁/进度状态机 |
| **KEY** | 输入事件流(RC 输入抽象),业务收事件不碰引脚 | BSP 提供原始电平读取;ebtn 在 Middleware 层完成消抖/长按识别;DisplayTask 将按键事件发送给 ControlTask,不直接修改业务状态 |
| **UART** | 设备文件 + 工作队列驱动 | 固定端口使用 `bsp_uart_init(BSP_UART_RS485)`、`bsp_uart_rx_dma_start(...)` 等枚举 ID 静态接口,不使用字符串 open/动态设备注册 |
| **定时器/HRT** | 高分辨率定时器:hrt_callout 回调链表(deadline/period),统一时间服务 | APP 周期行为使用 FreeRTOS 软件定时器或 `vTaskDelayUntil`;BSP 仅保留硬件时基/SysTick 中断入口;Bootloader 使用独立、显式初始化的裸机时基 |
| **PWM/定时器输出** | ioctl 控制(PWM_SERVO_SET/ARM),混控器管理 | 当前 LED 升级指示不使用 PWM;若未来其他功能确需 PWM,由对应 Service 驱动硬件 PWM,禁止在 BSP 内建立第二套通用软件调度器 |
| **输入捕获** | capture_callback_t 回调(通道+边沿+时间戳) | 预留:如需测频率/脉宽,`bsp_input_capture` 回调式接口 |
| **ADC** | 语义通道宏(ADC_SCALED_V5_CHANNEL) | `BSP_ADC_CH_POT` 语义宏 + 100ms 常驻 |
| **DAC** | 少见,无典型封装 | `bsp_dac_set_voltage(BSP_DAC_CH0, v)` 语义接口 |
| **SPI 总线** | 总线抽象 + 设备注册表(每个传感器一个驱动) | BSP 提供总线传输原语;APP 侧 GD25Q40E 由 StorageTask 的 PersistenceService 路径访问,Bootloader 仅在升级阶段访问,不向业务公开 `bsp_spi_lock/unlock` |
| **OLED** | 传感器驱动框架 | BSP 只负责 I2C/屏控制器原始传输;页面布局、状态文本和刷新节流属于 DisplayService |
| **SPI Flash** | 设备驱动 | `bsp_flash_read/program/erase`(原始硬件) → FlashKV(Middleware) → PersistenceService(Service),由 StorageTask 单一执行 |
| **SDIO/TF** | 块设备驱动 | `bsp_sdio_block_read/write`(块设备) → FatFsAdapter(Middleware)完成 mount/unmount/文件操作 |
| **RTC** | NuttX 时间服务统一抽象 | BSP 提供 RTC 原始读写;TimeService 负责时间有效性、Unix 时间戳和业务格式转换 |
| **PMU** | power 模块 + 电源状态抽象 | BSP 只执行最终硬件睡眠/WFI;PowerManager 负责停止采样、等待 flush/健康检查、关闭外设和恢复任务 |

**接口边界对照:**

| 调用者看到的接口 | 实际归属 | 边界说明 |
|---|---|---|
| `indicator_set_state(INDICATOR_ALARM)` | IndicatorService | 业务传语义,Service 计算状态/进度,BSP 只写 GPIO |
| `display_post_model(&model)` | DisplayService | 业务发布显示模型,DisplayTask 独占 OLED IO |
| `persistence_kv_set_async(request_id, ...)` | PersistenceService | 转为 StorageTask 请求;调用者不持 SPI 锁、不执行擦除/GC |
| `fatfs_adapter_mount()` | FatFsAdapter | 仅 StorageTask/Bootloader 离线升级流程调用,不属于 BSP |
| `power_manager_request_sleep(request_id)` | PowerManager | 异步协调各任务并等待完成消息,最后才调用 BSP 硬件睡眠入口 |
| `bsp_uart_rx_dma_start(BSP_UART_RS485, ...)` | BSP/Device Driver | 编译期固定端口,无字符串查找和动态句柄生命周期 |
| `vTaskDelayUntil()` / FreeRTOS Timer | APP 执行机制 | APP 不通过 BSP 注册通用周期回调;Bootloader 另用裸机时基 |

**结论:** 本项目只借鉴 PX4 的板级配置、语义化服务接口、状态机和资源所有权思想,不搬 uORB、设备文件、动态驱动注册等重机制。固定硬件采用静态类型 BSP 接口;跨任务业务能力由 Service + FreeRTOS 消息提供;Bootloader 与 APP 通过 Common 的稳定序列化契约共享升级定义;持久化统一由 StorageTask 单所有者执行。这样既保留工程表达力,又避免在 GD32 单 MCU 项目中过度设计。

---
