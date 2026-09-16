# GD32F470 Industrial Data Acquisition Terminal

[中文版](README.md)

> An industrial data acquisition terminal built around GD32F470VET6, FreeRTOS, RS485, Modbus RTU, TF/FatFs storage, and a recoverable Bootloader upgrade path.

## Project Overview

This project organizes a GD32F470VET6 industrial control board as a layered embedded system instead of placing unrelated peripheral demos directly in main. The design starts at the hardware layer and moves through BSP adapters, shared data contracts, middleware, application services, and FreeRTOS tasks to form complete acquisition, processing, display, storage, communication, alarm, and firmware-upgrade paths.

The system provides:

- ADC voltage acquisition, three-sample averaging, ratio conversion, and DAC loopback;
- USART0 CLI, a custom USART1 RS485 protocol, and Modbus RTU;
- OLED, LED, key-input, and RTC services;
- TF card/FatFs storage for samples, alarms, audit records, and configuration;
- GD25Q40E external SPI NOR storage for parameters, alarms, and upgrade metadata;
- Bootloader online IAP, TF offline upgrade, image validation, trial confirmation, rollback, and OLED progress indication;
- A complete architectural plan for future low-power operation.

The main engineering focus is layering, abstraction, ownership, asynchronous task coordination, reliable persistence, and recoverable firmware updates.

## Current Scope

The selected M0-M6 functionality is implemented and the repository is in its final delivery stage. Because of the internship/job-search schedule, new development and testing are paused after M6. M7 low-power operation and final system-wide acceptance are retained as future extensions.

The current version can be described as follows:

> The runtime application, communication, storage, alarm, and Bootloader upgrade paths are implemented with build and board-level evidence. Low-power management, long-duration stability, and selected random power-loss cases are outside the current completion evidence.

The selected M6 scope includes online/offline upgrades, GD25Q40E dual-slot metadata, App trial confirmation, rollback, Boot-to-App handoff, FWDGT safety waiting, error responses, and OLED upgrade indication. Random power-loss or rename-power-loss cases N-05, N-07, and N-08 were intentionally not executed and must not be reported as passed.

## Layered System Architecture

~~~mermaid
flowchart TB
    HW["GD32F470VET6<br/>GPIO / ADC / DAC / USART / SPI / SDIO / I2C / RTC"]
    VENDOR["Driver + Libraries<br/>CMSIS + GD32F4xx peripheral library"]
    BSP["BSP<br/>board pins, clocks, DMA, peripheral adapters"]
    COMMON["Common<br/>Flash layout, CRC, upgrade serialization, reset contracts"]
    MW["Middleware<br/>FreeRTOS / FatFs / Protocol / Modbus / FlashKV / RingBuffer / CLI"]
    SERVICES["App services<br/>configuration, sampling, alarms, display, persistence, protocol"]
    TASKS["FreeRTOS tasks<br/>Protocol / Sample / Storage / Alarm / Control / Display / Health"]
    BOOT["Bootloader<br/>startup dispatch, online/offline update, metadata, install, trial, rollback"]
    HOST["PC host tools<br/>packaging, RS485/Modbus regression, online upgrade"]

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

The dependency direction is intentionally layered:

1. **Hardware and vendor-library layer** provides MCU registers, startup code, and chip-level capabilities.
2. **BSP layer** encapsulates board pins, clocks, DMA, direction control, and device timing behind functional APIs.
3. **Common layer** defines the shared addresses, data formats, CRCs, metadata, and reset contracts understood by Bootloader and App.
4. **Middleware layer** provides RTOS, file-system, protocol, ring-buffer, and Flash-KV capabilities independent of business semantics.
5. **App services and tasks** implement the application behavior through task, queue, and service interfaces.
6. **Bootloader layer** is a separate bare-metal image that reuses Common/BSP contracts and drivers but never runs concurrently with the App.
7. **Tools, tests, and documentation** support building, packaging, host communication, PC-side checks, and design records.

## File and Directory Architecture

| Directory | Layer role | Encapsulated responsibility |
|---|---|---|
| Driver/CMSIS | MCU startup and architecture | Cortex-M4, GD32 system initialization, startup files, linker scripts |
| Libraries | Vendor peripheral library | GD32F4xx GPIO, DMA, ADC, DAC, USART, SPI, SDIO, I2C, RTC, FMC, and related APIs |
| BSP | Board adaptation | board configuration, GPIO, keys, LEDs, ADC, DAC, USART/RS485, RTC, OLED, SPI Flash, SDIO, internal Flash |
| Common | Shared image contract | common_flash_layout, CRC, upgrade serialization, reset reasons, crash markers |
| Middleware/FreeRTOS | RTOS kernel | Static tasks, queues, event groups, mutexes, software timers, and scheduling |
| Middleware/FatFs | File-system layer | FatFs core and TF block-device adaptation |
| Middleware/Protocol | Protocol foundation | Custom framing, stream parsing, and Modbus RTU encoding/decoding |
| Middleware/FlashKV | Persistence foundation | External Flash records, CRC, dual-sector rotation, and garbage collection |
| Middleware/Ringbuffer, CLI, Ebtn | Generic components | Ring buffers, command parsing, and key-event mechanisms |
| Tasks | Application task layer | Sampling, storage, alarm, protocol, control, display, and health tasks |
| App | Application composition root | FreeRTOS configuration, application entry, CLI, protocol services, configuration, upgrade confirmation |
| Bootloader | Startup and update layer | Request, staging, DATA/END/INSTALL, offline update, metadata, trial, rollback, jump, indicator |
| MDK | Project and build layer | Keil AC5 projects, linker configuration, workspace, and build outputs |
| test | Host and component verification | Unity/C tests, Python host tests, protocol, storage, and serialization checks |
| tools | Delivery tools | App manifest, TF offline package, and online-upgrade host tools |
| Docs | Design and process layer | Overview, partition contracts, development process, configuration contract, acceptance records |
| .cursor/rules | AI collaboration layer | Full-project reading, architecture explanation, and evidence boundaries |

The directory boundaries are part of the design. Business tasks do not access registers directly; ISRs do not parse protocols or perform persistence; ControlTask does not bypass StorageTask to access TF/Flash; and Bootloader does not share upgrade resources concurrently with the App.

## Runtime Tasks and Ownership

The App uses statically allocated FreeRTOS objects, disables dynamic allocation, and does not link heap_4. Static queues, event groups, mutexes, software timers, and task notifications carry requests and completion results.

| Task | Inputs | Outputs/calls | Ownership boundary |
|---|---|---|---|
| ProtocolTask | USART1 events and RingBuffer data | Request queues, response frames, event frames | Framing, dispatch, encoding, and transmission; no persistence business |
| SampleTask | ADC/DMA events and sampling configuration | Latest snapshot and sample-result queue | Acquisition, filtering, and engineering-unit conversion |
| StorageTask | Configuration, sample, alarm, and audit requests | File and KV completion results | The only App task that executes FatFs, TF, and external persistence |
| AlarmTask | Sample results | Alarm events and persistence requests | Thresholds, consecutive counts, hysteresis, ACTIVE/RECOVERED state |
| ControlTask | CLI, keys, protocol requests, completion messages | Business calls, configuration application, protocol results | System coordinator and async request lifecycle owner |
| DisplayTask | Display state and key-scan period | OLED refresh and key events | App-side owner of OLED/I2C |
| HealthTask | Heartbeats and busy/progress/deadline state | Watchdog decision, health state, sleep-ready | Health contract and IWDG feeding conditions |

### Typical Data Flow

~~~text
ADC/DMA ──→ SampleTask ──→ latest snapshot ──→ DisplayTask
                         └──→ AlarmTask ──→ alarm persistence request

USART/RS485 ──→ ISR/RingBuffer ──→ ProtocolTask
                                      └──→ request queue ──→ ControlTask
                                                              ├──→ SampleTask
                                                              ├──→ StorageTask
                                                              └──→ result queue ──→ ProtocolTask

ControlTask / AlarmTask ──persistence request──→ StorageTask
                                                 ├──→ FatFs / TF
                                                 └──→ FlashKV / GD25Q40E

HealthTask ──health, busy, deadline──→ watchdog and system coordination
~~~

### Ownership Rules

- ISRs only receive bytes, record timing, set events, or notify tasks. They do not parse protocols, print, or erase/program Flash.
- ProtocolTask owns communication framing and encoding; ControlTask executes business logic.
- StorageTask exclusively owns App-side FatFs and GD25Q40E business persistence.
- AlarmTask produces alarm events and persistence requests but does not write storage directly.
- DisplayTask exclusively owns the App-side OLED/I2C display.
- Cross-task messages carry request_id, origin, protocol sequence, deadline, and value-copied payloads rather than pointers to reusable RX buffers.
- Bootloader exclusively owns upgrade resources during startup/update processing and turns off its display before jumping to the App.

## Hardware Abstraction and Interface Mapping

| Hardware function | MCU resources | Software abstraction |
|---|---|---|
| System clock | 25 MHz HXTAL → 240 MHz | CMSIS/GD32 system initialization |
| RTC | 32.768 kHz LSE | BSP RTC primitives and time service |
| ADC CH0/CH1 | PC0 / PC1 | board_adc → SampleTask |
| DAC | PA4 | board_dac, PA4 looped back to PC1 |
| Debug CLI | USART0 PA9/PA10 | board_usart → CLI → ControlTask |
| RS485/RS232 | USART1 PA2/PA3, PA1 direction control | board_usart → ProtocolTask |
| External serial | USART2 PB10/PB11 | board_usart |
| OLED | I2C0 PB8/PB9 | board_i2c/board_oled → DisplayTask |
| SPI NOR | SPI1 PB13-PB15, CS=PB12 | board_spi_flash → FlashKV/Bootloader metadata |
| TF card | SDIO PC8-PC12, PD2, detect=PE2 | board_sdio → FatFs → StorageTask |
| LEDs/keys | LEDs PD8-PD13; keys PE15, PE13, PE11, PE9, PE7, PB0 | board_gpio/board_key → DisplayTask and status services |

The BSP isolates board details so upper layers depend on functional interfaces rather than GPIO registers, DMA channels, RS485 direction timing, or SSD1306 command sequences.

## Storage and Flash Layout

### GD32F470 Internal Flash

The internal Flash is 512 KiB and covers 0x08000000-0x0807FFFF.

| Region | Start | End | Size | Purpose |
|---|---:|---:|---:|---|
| Bootloader | 0x08000000 | 0x0800FFFF | 64 KiB | Startup and upgrade |
| Legacy metadata reserve | 0x08010000 | 0x08011FFF | 8 KiB | Retired; layout placeholder only |
| App | 0x08012000 | 0x08031FFF | 128 KiB | FreeRTOS application |
| Backup | 0x08032000 | 0x08051FFF | 128 KiB | Previous App and rollback |
| Staging | 0x08052000 | 0x08071FFF | 128 KiB | New firmware staging |
| Tail reserve | 0x08072000 | 0x0807FFFF | 56 KiB | Currently unused |

App, Backup, and Staging reserve their final 64 bytes for a manifest. The App manifest is at 0x08031FC0. The old internal-Flash metadata scheme was retired because programming the App can erase its shared sector.

### GD25Q40E External Flash

GD25Q40E provides 4 Mbit, or 512 KiB, with 4 KiB sectors.

| Region | Offset | Owner | Purpose |
|---|---:|---|---|
| Business KV slots A/B | sector 0/1 | StorageTask | Configuration and business parameters |
| Upgrade metadata slots A/B | sector 2/3 | Bootloader update state machine | active/pending/backup, state, version, CRC, source |
| Diagnostic sector | 0x7F000 | Diagnostic tools | Diagnostic data |

Upgrade metadata uses a fixed 72-byte serialization with CRC, generation, and a final commit marker. Alternating slots preserve one valid record if the other slot fails during a write.

## Communication Architecture

### USART0 CLI

USART0 is the local configuration and diagnostic interface. The ISR only receives and queues input; CLI parsing and execution run in ControlTask. The CLI covers device ID, baud rate, sampling period, ratios, thresholds, protocol mode, RTC, sampling control, display state, configuration persistence, and TF status.

### Custom RS485 Protocol

USART1 uses PA1 for RS485 direction control. The protocol layer handles frame-header search, partial frames, concatenated frames, noise prefixes, CRC resynchronization, invalid lengths, duplicate-frame caching, and asynchronous request/result matching.

### Modbus RTU

Modbus uses USART1 in 8E1 mode and supports functions 03, 04, 06, and 10, standard exception responses, CRC16-Modbus, silent broadcast writes, and custom/Modbus mode switching. Modbus requests pass through ProtocolTask, the request queue, and ControlTask without bypassing application ownership.

## Bootloader Update Architecture

The Bootloader is a separate bare-metal image that uses the Common address/serialization contracts and the BSP internal-Flash, SPI-NOR, SDIO, USART, and OLED interfaces.

### Online Update

~~~text
App running
  → 0x0500 ENTER_BOOT
  → App persists the request and replies READY
  → software reset into Bootloader
  → 0x0501 BEGIN
  → 0x0502 DATA: write Staging, read back, then ACK
  → 0x0503 END: validate length, CRC, manifest, vector table
  → 0x0504 INSTALL: Backup → erase App → program App → verify
  → TRIAL_PENDING
  → App health confirmation CONFIRMED
  → atomic active=pending commit
  → new App runs
~~~

0x0505 ABORT clears the online-update context atomically. The state and installation substage are persisted before destructive operations, and FWDGT is serviced during long waits and chunked erase/program operations.

### TF Offline Update

~~~text
Read firmware/app.bin
  → prepare staging
  → copy payload and read back
  → validate length/CRC/vector table
  → generate manifest
  → STAGED_VALID
  → shared INSTALL path
  → TRIAL_PENDING / CONFIRMED
  → idempotently rename app.bin to .applied
~~~

Failed packages are isolated with .failed. If offline cleanup is incomplete, the Bootloader still starts a valid App but blocks new online/offline updates so the cleanup context cannot be overwritten.

### OLED Upgrade Indicator

The Bootloader has an independent SSD1306 indicator:

- Staging and BEGIN: 0%;
- DATA write and read-back: 0%-90%;
- END validation and metadata commit: approximately 90%-95%;
- INSTALL copy, erase, and verification: 96%-100%;
- Clear and turn off the OLED before jumping to the App;
- Fall back to LED indication if OLED initialization or refresh fails.

## Build and Programming

Keil AC5 is the official release build chain. The EIDE Boot target still shares the App source tree and is not the official release chain.

PowerShell example:

~~~powershell
$UV4 = "D:\keil5\UV4\UV4.exe"
& $UV4 -b "MDK\IndustrialEmbedded-Boot.uvprojx"
& $UV4 -b "MDK\IndustrialEmbedded-App.uvprojx"
~~~

Replace the UV4 path with the local Keil installation path.

| Artifact | Path | Programming address |
|---|---|---:|
| Bootloader BIN | MDK/ObjectsBoot/IndustrialEmbedded-Boot.bin | 0x08000000 |
| App BIN | MDK/ObjectsApp/IndustrialEmbedded-App.bin | 0x08012000 |
| App manifest HEX | MDK/ObjectsApp/IndustrialEmbedded-App-with-manifest.hex | Address records are embedded in HEX |
| TF offline package | MDK/ObjectsApp/IndustrialEmbedded-App-vXX-offline.bin | TF firmware directory |

The final M6 build record reports 0 errors and 0 warnings for both Boot and App. The Boot BIN is 0xC1AC bytes and the App BIN is 0x168C4 bytes.

## Evidence and Limitations

Recorded evidence includes:

- The Bootloader on the board matched the final local Boot build byte-for-byte;
- The App booted normally and answered over RS485;
- The version-23 online update completed successfully;
- DATA, END, INSTALL, and multiple post-update boots were traced;
- The OLED framebuffer progressed to 100% and stayed full during the post-install boots;
- Online/TF isolation and incomplete-offline-cleanup recovery were verified.

The following are not current completion evidence:

- M7 low-power sleep/wake;
- 24-hour continuous operation;
- A 100-invalid-frame stress run;
- M6 N-05, N-07, and N-08 random power-loss/rename-power-loss cases;
- Twenty random power losses during sample/alarm writes;
- Physical TF-full injection;
- Logic-analyzer measurement of sub-millisecond Modbus timing.

## How the Unimplemented Low-Power Feature Fits the Full Architecture

Low-power operation is not an isolated sleep function in App. It is a cross-task, cross-peripheral system transaction:

~~~mermaid
flowchart TD
    CMD["RS485 0x03AA"]
    P["ProtocolTask"]
    C["ControlTask"]
    PM["PowerManager<br/>planned component"]
    S["StorageTask<br/>flush + close files"]
    A["SampleTask<br/>stop ADC/DMA"]
    R["RS485<br/>wait for TX complete"]
    H["HealthTask<br/>health check + watchdog window"]
    W["RTC / EXTI<br/>10 s or key wake-up"]
    SLEEP["WFI deep sleep"]
    RESTORE["Restore clocks, peripherals, tasks, timers"]
    EVENT["0x0381 wake event"]

    CMD --> P --> C --> PM
    PM --> S
    PM --> A
    PM --> R
    PM --> H
    PM --> W --> SLEEP
    SLEEP --> RESTORE --> EVENT
    EVENT --> P
~~~

Mapped to the current file architecture:

- ProtocolTask identifies 0x03AA; ControlTask creates and tracks the sleep request;
- a planned PowerManager coordinates StorageTask, SampleTask, ProtocolTask, and HealthTask;
- StorageTask must flush FatFs data and close active files first;
- SampleTask must stop ADC/DMA, while ProtocolTask waits for RS485 transmission to finish;
- HealthTask checks all task health and adjusts the IWDG window to cover sleep and wake-up initialization;
- BSP RTC/EXTI and PMU interfaces provide the physical wake source and WFI entry;
- after wake-up, PowerManager restores peripherals, timers, and task time bases before ProtocolTask sends 0x0381;
- these project-level entry points and the board-level loop are not implemented in the current version.

## AI Reading and Project Explanation Convention

Any AI reading this repository must understand the complete architecture before answering a local question. It must not simply say “M7 is missing”; it must explain how low power would connect to the existing tasks, protocol, storage, acquisition, RTC, watchdog, and hardware sleep entry.

Every status answer must distinguish:

- Implemented;
- Built;
- Board-tested;
- Supported only by design or source evidence;
- Unverified;
- Deferred or intentionally skipped by project decision.

The persistent rule is in [.cursor/rules/industrialembedded-project-context.mdc](.cursor/rules/industrialembedded-project-context.mdc). The development process and design records are in [Docs/03_DEV_PROCESS.md](Docs/03_DEV_PROCESS.md).

## Resume-Oriented Project Summary

Developed an industrial data acquisition terminal based on GD32F470VET6 and FreeRTOS. Implemented ADC/DAC acquisition loopback, digital filtering, custom RS485 protocol, Modbus RTU, TF/FatFs storage, alarm handling, and persistent configuration. Used layered BSP, middleware, shared contracts, and task services to implement Bootloader online IAP, TF offline update, GD25Q40E dual-slot metadata, trial confirmation/rollback, and OLED upgrade indication, with corresponding build and board-level update evidence.

Low-power sleep/wake, long-duration stability, and selected random power-loss tests are future extensions and are not claimed as completed in the current version.

## Related Documents

- [Project overview](Docs/01_PROJECT_OVERVIEW.md)
- [Development process and design records](Docs/03_DEV_PROCESS.md)
- [M6 partition freeze contract](Docs/M6_0A_PARTITION_FREEZE.md)
- [M5 configuration contract draft](Docs/M5_CONFIG_INI_CONTRACT.md)
- [中文 README](README.md)
