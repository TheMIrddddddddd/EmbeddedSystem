# M6-0A 分区与内部 Flash 页擦除冻结契约

状态：M6-0A-3 分区规则已冻结，Common 宏替换方案待手工应用；2026-09-13。

本文件记录 M6 首版的内部 Flash、GD25Q40E 外部 Flash、镜像 manifest 和擦除边界契约。它是设计冻结文件，不代表 Bootloader 升级状态机、外部元数据双槽或内部 Flash 擦除驱动已经实现。

## 1. 冻结范围

- GD32F470 当前工程使用的内部 Flash 窗口为 `0x08000000～0x0807FFFF`，总容量 512 KiB。
- 物理布局采用 `4 × 16 KiB + 1 × 64 KiB + 3 × 128 KiB`。
- App、Backup、Staging 继续使用现有的三个 128 KiB 逻辑镜像区。
- M6 首版只在内部 Flash 中保存 Bootloader、App、Backup 和 Staging；GD25Q40E 只新增升级元数据双槽。
- GD25Q40E sector 0/1 保持 M5 业务 KV，sector 2/3 固定为 M6 升级元数据 Slot A/B。
- 旧内部 Meta 区 `0x08010000～0x08011FFF` 仅作为退役保留区，不再作为运行时元数据后端。

## 2. 页擦除语义

GD32F4xx 用户手册对 `FMC_PECFG.PE_ADDR` 的定义是 4 KiB 对齐的页地址。`fmc_page_erase(page_addr)` 没有长度参数，一次调用擦除一个完整的 4 KiB 页；它不是擦除 4 个字节，也不是从任意地址向后擦除 4 KiB。

合法参数必须是页首地址，例如：

```c
fmc_page_erase(0x08012000UL); /* 擦除 0x08012000～0x08012FFF */
```

以下地址属于 manifest 地址，不是页首，不能作为页擦除参数：

```c
0x08031FC0UL
0x08051FC0UL
0x08071FC0UL
```

对应的页首分别是 `0x08031000`、`0x08051000` 和 `0x08071000`。当前 SPL 实现不会替调用者检查 4 KiB 对齐、地址范围或逻辑分区权限，M6 的内部 Flash 封装必须补齐这些边界检查。

依据：

- `Libraries/Source/gd32f4xx_fmc.c` 中的 `fmc_page_erase()`；
- `Libraries/Include/gd32f4xx_fmc.h` 中的 `FMC_PE_ADDR` 和页擦除声明；
- [GD32F4xx User Manual Rev3.4](https://www.gd32mcu.com/data/documents/userManual/GD32F4xx_User_Manual_Rev3.4.pdf) 的 FMC_PECFG 章节；
- 厂商示例 `GD32F4xx_Firmware_Library_V3.3.3/Examples/USB/USB_Device/dev_firmware_update/inc/inter_flash_if.h`。

## 3. 物理扇区表

| 物理扇区 | 起始地址 | 结束地址 | 大小 | 页首地址范围 |
|---|---:|---:|---:|---|
| Sector 0 | `0x08000000` | `0x08003FFF` | 16 KiB | `0x08000000～0x08003000` |
| Sector 1 | `0x08004000` | `0x08007FFF` | 16 KiB | `0x08004000～0x08007000` |
| Sector 2 | `0x08008000` | `0x0800BFFF` | 16 KiB | `0x08008000～0x0800B000` |
| Sector 3 | `0x0800C000` | `0x0800FFFF` | 16 KiB | `0x0800C000～0x0800F000` |
| Sector 4 | `0x08010000` | `0x0801FFFF` | 64 KiB | `0x08010000～0x0801F000` |
| Sector 5 | `0x08020000` | `0x0803FFFF` | 128 KiB | `0x08020000～0x0803F000` |
| Sector 6 | `0x08040000` | `0x0805FFFF` | 128 KiB | `0x08040000～0x0805F000` |
| Sector 7 | `0x08060000` | `0x0807FFFF` | 128 KiB | `0x08060000～0x0807F000` |

## 4. 逻辑分区与页范围

| 逻辑区域 | 起始地址 | 结束地址 | 页数 | 页首地址范围 | 跨越物理扇区 |
|---|---:|---:|---:|---|---|
| Bootloader | `0x08000000` | `0x0800FFFF` | 16 | `0x08000000～0x0800F000` | Sector 0～3 |
| Legacy Meta 保留区 | `0x08010000` | `0x08011FFF` | 2 | `0x08010000～0x08011000` | Sector 4 |
| App | `0x08012000` | `0x08031FFF` | 32 | `0x08012000～0x08031000` | Sector 4、5 |
| Backup | `0x08032000` | `0x08051FFF` | 32 | `0x08032000～0x08051000` | Sector 5、6 |
| Staging | `0x08052000` | `0x08071FFF` | 32 | `0x08052000～0x08071000` | Sector 6、7 |
| 内部 Flash 尾部保留区 | `0x08072000` | `0x0807FFFF` | 14 | `0x08072000～0x0807F000` | Sector 7 |

三个镜像区的最后一页分别是：

| 分区 | 最后一页 | 镜像数据最后地址 | Manifest 起始地址 |
|---|---|---:|---:|
| App | `0x08031000～0x08031FFF` | `0x08031FBF` | `0x08031FC0` |
| Backup | `0x08051000～0x08051FFF` | `0x08051FBF` | `0x08051FC0` |
| Staging | `0x08071000～0x08071FFF` | `0x08071FBF` | `0x08071FC0` |

## 5. 擦除保护边界

### 5.1 允许的 M6 镜像擦除范围

```text
App:     0x08012000～0x08031FFF，共 32 个 4 KiB 页
Backup:  0x08032000～0x08051FFF，共 32 个 4 KiB 页
Staging: 0x08052000～0x08071FFF，共 32 个 4 KiB 页
```

每次擦除前必须检查：

```text
page_addr % 0x1000 == 0
partition_base <= page_addr
page_addr + 0x1000 <= partition_end + 1
```

### 5.2 禁止使用物理大扇区擦除

| 物理擦除对象 | 实际会损坏的逻辑内容 | M6 首版规则 |
|---|---|---|
| Sector 4 | Legacy Meta + App 前 14 页 | 不得用于 App 擦除 |
| Sector 5 | App 后 18 页 + Backup 前 14 页 | 不得用于 App/Backup 擦除 |
| Sector 6 | Backup 后 18 页 + Staging 前 14 页 | 不得用于 Backup/Staging 擦除 |
| Sector 7 | Staging 后 18 页 + 尾部保留区 | 不得用于 Staging 擦除 |

因此 `fmc_sector_erase()` 不得用于 App、Backup、Staging。M6 安装时由 Bootloader 在 Boot 区执行，Boot 区自身不属于本次安装擦除对象。

### 5.3 Manifest 页规则

分区准备阶段允许先擦除包含 manifest 的最后一页，然后按以下顺序写入：

```text
擦除分区全部页
→ 写入原始镜像
→ 校验镜像 CRC 和向量表
→ 最后写入 manifest
→ 读回 manifest 并校验
```

不能为了单独修改 manifest 直接擦除最后一页，因为该页前面的 `0xFC0` 字节仍属于镜像数据。若未来需要修复已存在镜像的 manifest，必须采用整页读入 RAM、整页擦除、整页恢复的读改写流程；M6 首版默认不提供无保护的单独页擦除接口。

## 6. GD25Q40E 外部地址边界

GD25Q40E 的地址是 SPI 器件内部偏移，不是 MCU 内部 Flash CPU 地址。

| 区域 | SPI 偏移 | 大小 | 所有权 |
|---|---:|---:|---|
| 业务 KV Slot A | `0x000000～0x000FFF` | 4 KiB | M5 StorageTask |
| 业务 KV Slot B | `0x001000～0x001FFF` | 4 KiB | M5 StorageTask |
| 升级 Meta Slot A | `0x002000～0x002FFF` | 4 KiB | M6 Bootloader 状态机 |
| 升级 Meta Slot B | `0x003000～0x003FFF` | 4 KiB | M6 Bootloader 状态机 |
| 后续保留 | `0x004000～0x07EFFF` | — | 待 M6 后续冻结 |
| 当前诊断扇区 | `0x07F000～0x07FFFF` | 4 KiB | `flashdiag`，暂保留 |

M6 首版不把外部 Flash 用作镜像执行区，也不把外部 Flash 用作 Backup 或 Staging。sector 2/3 只承载升级元数据双槽。

## 7. Common 宏替换方案

### 7.1 旧宏的处理

当前 `Common/inc/common_flash_layout.h` 中的旧宏：

```c
#define META_SLOT_A_BASE
#define META_SLOT_B_BASE
#define META_SLOT_SIZE
```

它们应在后续手工修改中替换为明确的退役保留区命名：

| 旧宏 | 新宏 | 说明 |
|---|---|---|
| `META_SLOT_A_BASE` | `INTERNAL_FLASH_LEGACY_META_RESERVED_A_BASE` | 只表示旧内部保留页 A |
| `META_SLOT_B_BASE` | `INTERNAL_FLASH_LEGACY_META_RESERVED_B_BASE` | 只表示旧内部保留页 B |
| `META_SLOT_SIZE` | `INTERNAL_FLASH_LEGACY_META_RESERVED_SLOT_SIZE` | 只表示退役页大小 |

不保留旧宏兼容别名，避免未来代码继续把退役区误当作运行时元数据。

### 7.2 冻结后的 Common 宏工作集

以下是手工修改时采用的目标宏方案，当前只作为契约记录：

```c
#define INTERNAL_FLASH_PAGE_SIZE                    0x00001000UL

#define INTERNAL_FLASH_LEGACY_META_RESERVED_BASE   0x08010000UL
#define INTERNAL_FLASH_LEGACY_META_RESERVED_SIZE   0x00002000UL
#define INTERNAL_FLASH_LEGACY_META_RESERVED_SLOT_SIZE \
        0x00001000UL
#define INTERNAL_FLASH_LEGACY_META_RESERVED_A_BASE \
        0x08010000UL
#define INTERNAL_FLASH_LEGACY_META_RESERVED_B_BASE \
        0x08011000UL

#define GD25Q40E_CAPACITY                          0x00080000UL
#define GD25Q40E_SECTOR_SIZE                       0x00001000UL
#define GD25Q40E_BUSINESS_KV_SLOT_A_OFFSET         0x00000000UL
#define GD25Q40E_BUSINESS_KV_SLOT_B_OFFSET         0x00001000UL
#define GD25Q40E_UPGRADE_META_SLOT_A_OFFSET        0x00002000UL
#define GD25Q40E_UPGRADE_META_SLOT_B_OFFSET        0x00003000UL
#define GD25Q40E_UPGRADE_META_SLOT_SIZE            0x00001000UL
#define GD25Q40E_DIAGNOSTIC_SECTOR_OFFSET          0x0007F000UL
```

现有 `APP_BASE`、`APP_SIZE`、`BACKUP_BASE`、`BACKUP_SIZE`、`STAGING_BASE` 和 `STAGING_SIZE` 保持不变。后续应让 `storage_persistence.h` 和 `upgrade_serialization.h` 引用 Common 的唯一地址定义，消除重复硬编码。

## 8. 当前实现状态与验证边界

已由源码或厂商资料确认：

- 512 KiB 窗口的物理扇区边界；
- App、Backup、Staging 的逻辑地址和 4 KiB 页范围；
- `fmc_page_erase()` 一次操作一页且要求 4 KiB 对齐；
- 大扇区擦除会跨越逻辑分区；
- 旧内部 Meta 位于 Sector 4，不能继续作为运行时 Meta；
- GD25Q40E sector 0/1 的 M5 业务边界和 sector 2/3 的 M6 元数据边界。

尚未实现或验证：

- Common 头文件中的宏替换；
- AC5/GCC 链接器对 manifest 64 B 保留区的强制保护；
- M6 内部 Flash 页擦除封装；
- 实际板级页擦除、读回和相邻页保护测试；
- 外部元数据双槽状态机。

M6-0A-3 的退出条件是：地址表、物理页范围和保护规则经人工确认，随后由开发者手工更新 Common 宏；更新后只做引用扫描和差异检查，不直接进入升级擦写。
