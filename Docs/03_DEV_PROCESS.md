# 工业数据采集终端 —— 开发流程(执行级展开)

> 文档版本:V1.0 | 日期:2026-08-09
> 定位:本文是《01_PROJECT_OVERVIEW.md》第十五章 M0~M7 里程碑的**执行级展开**——把每个里程碑翻译成「动作清单 → 产出文件 → 依赖 → 卡点 → 验收关卡」。
> 使用方式:开发时照表执行,验收时对照《01》第十四章(A-01~Q-02)与第十四-2(稳定性)逐条验证;技术原理见《02_TECH_STACK.md》。
> 当前工程状态:单工程裸机闪灯 demo(0x08000000 全 Flash,Keil/EIDE 双工具链可编译),尚未拆分 Boot/App,业务代码为零。

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
| 5 | Flash 边界表 | Boot 64KB / slot A 4KB / slot B 4KB / App / Backup / Staging 各 128KB,全部 4KB 对齐 | 《01》十二-1(宏已给出) |

**产出文件:**

```
BSP/Boards/gd32f470ve_v1/board_config.h    ← 引脚宏打包(参考 h743mini board_config.h 风格)
BSP/Boards/gd32f470ve_v1/board_dma_map.h   ← DMA/定时器通道静态表 + 冲突检查
```

**写法参考(借用 PX4 h743mini,只借"怎么写",不借 OS 机制):**

| 参考文件 | 借鉴点 |
|---|---|
| `boards/gjl/h743mini/src/board_config.h` | 引脚宏打包:`(方向\|推挽\|速度\|初始电平\|端口\|引脚)` 一个宏定义全量信息 |
| `boards/gjl/h743mini/src/timer_config.cpp` | `constexpr` 通道静态表:每通道 = {定时器,通道,GPIO},编译期定死 |
| `boards/gjl/h743mini/src/spi.cpp` | 总线/设备/CS 静态表 + `validateSPIConfig()` 编译期校验 |

**卡点(阻塞项):** 引脚分配需要**板子原理图**。文档已锁定的部分:LED1~6 = PD8~PD13(连续引脚)、DAC = PA4、CH1 采集 = PC1、SDIO = PC8~PC12 + PD2;未定的:CH0 电位器引脚、USART0/USART1 引脚与 DE 脚、SPI Flash 引脚、I2C OLED 引脚、6 个按键引脚。

**验收关卡:** 资源表完成且**无冲突**(DMA 通道互斥、EXTI 线号互斥、NVIC 优先级分层、Flash 边界 4KB 页对齐、`fmc_page_erase()` 适用)。

---

## 四、M1:工程地基(Boot/App 双工程)

**目标:** 双工程 + 分区链接 + 向量跳转,打通"上电 Boot → 跳 App"最小链路。

**动作清单(严格按序):**

1. **建目录骨架**(《01》十七-2 目录树):
   ```
   Common/  BSP/Boards/gd32f470ve_v1/  Middleware/  Services/  Tasks/  App/  Bootloader/
   Libraries/(SPL,不动)  Driver/(CMSIS,不动)  User/(逐步废弃)
   ```
2. **先建 Common 最小子集**:`common_flash_layout.h`(十二-1 的 9 个地址宏 + MANIFEST 宏)——两个工程的链接脚本都引用它;
3. **拆出 Boot 工程**(Keil 主力):0x08000000 裸机,最小 LED + 5s 等待 + 跳转 App(跳转前按《01》十二-8 的 10 步序列:关中断/关 SysTick/反初始化/NVIC 清中断/校验 MSP 与复位向量/VTOR/MSP/跳转);
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

**卡点:** Bootloader 64KB 分区内发布映像 `Code + RO-data + RW-data load` **必须 ≤ 60KB**(《01》十二-1 门槛);若后期 SDIO/FatFs/OLED 撑爆,回到 M0 重新划区,不允许砍校验功能硬塞。

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
| ebtn | Middleware/Ebtn | 去抖/单击/长按/KEEPALIVE 事件 | 《01》十一-2 |
| 升级序列化 | Common | firmware_header/manifest/upgrade_meta 逐字段编解码 | 《01》十二-2/10 |

**产出文件:** 各组件 `.c/.h` + PC 测试工程(`test/` 目录,不占 MCU 工程)。

**验收关卡:** 组件级测试全过;**此阶段完全不依赖板子**,可与 M1 并行推进。

---

## 六、M3:FreeRTOS APP 骨架

**目标:** 架构完整、业务为空、能稳定跑的 App。

**动作清单:**

1. 移植 FreeRTOS:heap_4 16KB 池,SysTick 交还给 FreeRTOS 作 tick(《01》十六-5 内存预算);
2. 按《01》十六-1 建**七任务空壳**(Protocol/Sample/Storage/Alarm/Control/Display/Health,优先级 5/4/3/3/3/2/5,栈 256/256/512/256/256/256/128 words);
3. 按《01》十六-2 建全部队列/事件组/软定时器(容量照表:8×24B 请求队列等);
4. IWDG(5s)+ HealthTask 喂狗链:全部任务心跳正常才喂狗(《01》十六-4);
5. FatFs 在 StorageTask 上下文 mount,验证"TF 卡单一所有权"模型;
6. DisplayTask 以 10ms 按键扫描 / 500ms 显示周期跑空显示模型(《01》十六-1)。

**产出文件:** `Tasks/*`、`App/`(composition root + 配置模型)、FreeRTOS 移植层、队列与事件组定义。

**验收关卡(照搬《01》M3):** 连续 **24h 无看门狗复位、无任务栈溢出**。

**风险点:** 栈大小与 heap 预算(十六-5)、喂狗豁免规则(十六-3.1)、DisplayTask 不得直接改业务状态。

---

## 七、M4:采集与通信

**目标:** 第一个业务里程碑——模拟量闭环 + 双串口双协议。

**动作清单:**

1. ADC/DMA 100ms 常驻采集 + 3 次均值滤波,写入共享区(采集引擎常驻,《01》十三-5);
2. DAC 输出 PA4 → 跳线 PC1 回读,打通输出-采集闭环(《02》3.4);
3. USART0 CLI 全部指令(test / rtc config / rtc now / conf / ratio / limit / config save|read / protocol / id / baud / start / stop / hide / unhide / help),CLI 解析在 ControlTask 上下文,ISR 只收数据入队(《01》四-5);
4. USART1 RS485 自定义二进制帧协议(帧格式/CRC16/应答超时/序列号,《01》七章);
5. Modbus RTU 从站(03/04/06/10 功能码,寄存器映射《01》八章),`protocol_mode` 切换;
6. **Python 回归测试脚本同步进场**:串口发帧/收帧/断言,覆盖正常帧 + 错误帧 + 异常帧。

**验收关卡:** A/B/C 类验收项通过;协议测试脚本可重复执行并全绿。

**风险点:** USART IDLE 中断不保证帧边界,半帧/多帧必须由 RingBuffer 解析器消化(《02》3.1 注意事项)。

---

## 八、M5:TF 卡与告警

**目标:** 可靠落盘 + 配置原子导入 + 告警状态机。

**动作清单:**

1. 三类文件存储:sample/alarm 每文件 10 条滚动、命名规则、audit 上电次数自增(boot_00000N.log),关键记录 `f_sync()`(《01》六章);
2. `config.ini` 原子导入:读全文件 → 临时结构解析校验 → 一次性替换 + Flash 原子写,任一行失败整组不生效(《01》三-1);
3. `config save/read` 走 Flash KV(GD25Q16 sector 0/1 双扇区轮换,《02》4.5 分区表);
4. 告警状态机:连续 3 次超限 → ACTIVE(只触发一次)→ 滞回 0.05V → RECOVERED;LED3 / CSV / Flash 最近 10 条 / RS485 上报按模式联动(《01》九章);
5. 拔卡降级/重挂载:检测拔卡停写不停采,重插恢复;写失败重试 3 次(100/300/900ms)后降级(《01》六-4)。

**验收关卡:** P 类验收项(P-01~P-03、Q-01~Q-02)+ 十三-5 使能位联动规则。

**风险点:** 所有 TF/GD25Q16 访问必须只在 StorageTask 上下文,ControlTask/AlarmTask 只发请求(《01》三-4 存储分域强制项)。

---

## 九、M6:Bootloader(全程最硬核)

**目标:** 在线 IAP + TF 离线升级 + 掉电恢复 + 启动确认回滚,全部按《01》十二章状态机实现。

**动作清单(严格按文档顺序):**

1. 五阶段在线升级:0x0500 ENTER_BOOT(仅 APP)/ 0x0501 BEGIN / 0x0502 DATA(先写后 ACK)/ 0x0503 END / 0x0504 INSTALL,命令职责表与状态机命令限制照《01》十二-4;
2. TF 离线升级:统一暂存流程(staging_prepare → 剥头复制 → 校验 → 生成 manifest → STAGED_VALID → 共用 INSTALL)+ 失败包 `.failed` 隔离 + 成功包 `.applied` 幂等改名(《01》十二-6/9);
3. 双槽元数据:68B 固定序列化、双槽轮换、commit_marker 原子提交、无有效槽时按 App/Backup manifest 恢复(《01》十二-10);
4. 启动确认:TRIAL_PENDING → APP 满足五条件写 CONFIRMED;IWDG/HardFault 失败计数 ≥3 回滚;crash_marker 统一消费(《01》十二-5/9);
5. OLED 升级进度(0~90% 接收 / 90~100% 校验搬运)+ LED 波浪呼吸(裸机 1ms 时基,《01》十一-4/5/6);
6. 跳转 App 10 步序列与 FWDGT 接管(《01》十二-8、十六-4)。

**产出文件:** `Bootloader/` 完整状态机、Common 的升级元数据/固件格式编解码、Python 打包工具(固件头 + 映像 + manifest 出厂四件套)。

**验收关卡:** N-01~N-10 逐条通过,重点是 N-05(备份/擦写随机断电)与 N-08(回滚/改名断电)的**随机断电测试**。

**风险点(投入最大处):**
- 安装子阶段掉电恢复矩阵(BACKUP_START→STAGED_VALID、BACKUP_VALID/APP_ERASING/APP_PROGRAMMING→回滚、APP_VALID→TRIAL_PENDING,《01》十二-4);
- 参数区必须 `fmc_page_erase()` 4KB 页擦除,严禁换算地址用扇区擦除(《01》十二-11);
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
