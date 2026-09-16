# GD32F470 Industrial Data Acquisition Terminal

[中文版](README.md)

> A layered industrial data acquisition terminal built around GD32F470VET6 and FreeRTOS, covering acquisition, processing, display, storage, communication, alarms, low power, and firmware updates.

## Project Overview

This project organizes a GD32F470VET6 industrial control board as a layered embedded system instead of placing unrelated peripheral demos directly in main. Hardware capabilities are progressively encapsulated into BSP adapters, shared contracts, middleware, application services, and real-time tasks. Bootloader and App are separate runtime images with different responsibilities.

The complete system contains:

- ADC voltage acquisition, DMA events, three-sample averaging, ratio conversion, and DAC loopback;
- USART0 CLI, a custom USART1 RS485 protocol, and Modbus RTU;
- OLED, LED, key, RTC, and watchdog services;
- TF/FatFs storage for samples, alarms, audit records, and configuration;
- GD25Q40E external SPI NOR storage for business KV data, alarms, and upgrade metadata;
- Bootloader online IAP, TF offline update, image validation, backup, trial confirmation, rollback, and progress indication;
- A system-level sleep, wake-up, resource-quiescence, and recovery path coordinated by tasks.

The main engineering focus is layered abstraction, interface encapsulation, ownership, asynchronous messaging, storage reliability, fault recovery, and maintainability.

## Overall Software Architecture

~~~mermaid
flowchart TB
    CHIP["GD32F470VET6<br/>MCU peripherals and interrupts"]
    VENDOR["Driver / Libraries<br/>CMSIS + GD32F4xx peripheral library"]
    BSP["BSP<br/>board resources and device adapters"]
    CONTRACT["Common<br/>addresses, data formats, CRC, reset contracts"]
    MIDDLEWARE["Middleware<br/>FreeRTOS / FatFs / Protocol / Modbus / FlashKV"]
    APP["App<br/>configuration, protocol services, composition root"]
    TASKS["Tasks<br/>Protocol / Sample / Storage / Alarm / Control / Display / Health"]
    BOOT["Bootloader<br/>startup, online/offline update, install, confirm, rollback"]
    STORAGE["Storage media<br/>internal Flash / GD25Q40E / TF"]
    IO["System interfaces<br/>ADC / DAC / USART / RS485 / OLED / RTC"]
    HOST["PC tools<br/>packaging, serial access, upgrade host"]
    POWER["System power management<br/>sleep quiescence, RTC wake-up, recovery"]

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

### Layer Relationships

| Layer | Directories | Responsibility | Dependency rule |
|---|---|---|---|
| MCU architecture | Driver/CMSIS, Libraries | Startup, CMSIS, GD32F4xx peripheral library, linker scripts | No project business logic |
| Board adaptation | BSP | Pins, clocks, DMA, peripheral timing, device I/O, hardware state | No business state or task queues |
| Shared contract | Common | Flash partitions, manifests, CRC, upgrade metadata, reset reasons, serialization | Shared by Bootloader and App |
| Reusable foundation | Middleware | RTOS, file system, protocols, ring buffers, CLI, Flash KV, key events | Reusable mechanisms, no business workflow |
| Application layer | App | Application entry, configuration, protocol services, Modbus, upgrade request/confirmation | Uses services and task interfaces |
| Runtime/task layer | Tasks | Acquisition, storage, alarms, communication, control, display, health | Owns concurrency and resources |
| Startup/update layer | Bootloader | Startup dispatch, online/offline update, install, confirm, rollback | Separate bare-metal image |
| Delivery layer | MDK, tools, test, Docs | Build, packaging, host interaction, PC checks, design records | Outside the runtime business path |

The dependency direction is from hardware to abstraction and from mechanisms to business:

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

Upper layers do not depend on hardware implementation details. Business code does not access registers directly, protocol code does not erase Flash directly, ISRs do not execute long-running business logic, and storage/display access is controlled by explicit task ownership.

## File and Code Architecture

### Driver and Libraries: MCU Capabilities

- Driver/CMSIS contains Cortex-M4 headers, GD32 system initialization, startup files, and GCC/Keil linker scripts;
- Libraries/Include and Libraries/Source contain the GD32F4xx vendor peripheral library;
- this layer answers how the chip boots and how peripherals are accessed, not what the device business state means.

### BSP: The Single Entry Point for Board Differences

BSP files with the board_ prefix encapsulate concrete hardware details:

- board_gpio and board_key: LEDs and keys;
- board_adc and board_dac: ADC/DAC setup, acquisition, and loopback;
- board_usart: USART, DMA, IDLE/RBNE, and RS485 direction control;
- board_i2c and board_oled: I2C0 and SSD1306;
- board_spi_flash: GD25Q40E raw access and sector erase;
- board_sdio: TF block I/O, DMA events, and card detection;
- board_rtc and board_timebase: RTC and time base;
- board_internal_flash: internal-Flash page operations.

Upper layers call functional board_ APIs and do not repeat GPIO alternate-function setup, DMA-channel details, RS485 direction timing, or SSD1306 command sequences. When moving to a similar board, board-level changes stay primarily inside BSP and board_config instead of spreading into business tasks.

### Common: Stable Boot/App Contracts

Common centralizes information that both runtime images must understand:

- common_flash_layout.h: Boot, App, Backup, Staging, and manifest addresses;
- upgrade_serialization.h: firmware header, manifest, metadata, states, and install substages;
- common_crc: CRC16 and CRC32;
- common_reset_contract: software, watchdog, and fault reset reasons;
- common_crash_marker: HardFault/IWDG-related crash markers.

Common owns neither business tasks nor concrete UART, TF, or Flash devices. It defines cross-module and cross-image data contracts and boundaries so Bootloader and App do not duplicate incompatible formats.

### Middleware: Replaceable Mechanisms

- Middleware/FreeRTOS: scheduling, queues, event groups, mutexes, software timers, and static memory;
- Middleware/Protocol: custom frames, stream parsing, and CRC resynchronization;
- Modbus RTU: functions 03/04/06/10 and standard exception responses;
- Middleware/FatFs: file-system core;
- Middleware/FlashKV: fixed records, CRC, dual-sector rotation, and garbage collection;
- Middleware/Ringbuffer: serial receive and stream buffering;
- Middleware/CLI: command parsing;
- Middleware/Ebtn: key-event mechanism.

These modules provide mechanisms but do not decide when to sample, what triggers an alarm, or which business field belongs in a file. Business semantics are composed by App and Tasks.

### App and Tasks: Business/Concurrency Separation

App provides the application composition root, configuration model, protocol services, Modbus services, and upgrade request/confirmation. Tasks provide concurrency and resource ownership:

- SampleTask acquires, filters, converts, and publishes snapshots;
- AlarmTask consumes sample results and generates alarm events;
- StorageTask performs actual file/KV persistence;
- ProtocolTask receives/transmits frames and dispatches protocols;
- ControlTask coordinates asynchronous business requests;
- DisplayTask owns OLED and key scanning;
- HealthTask owns health state and watchdog decisions.

Tasks communicate with static queues, event groups, mutexes, notifications, and value-copied request objects instead of cross-task callbacks or shared raw pointers.

### Bootloader: An Independent State-Machine Boundary

The Bootloader reuses Common partition/serialization contracts and BSP hardware interfaces while owning its own startup, upgrade protocol, metadata, installation, and rollback state machine. The App only requests an update and confirms the health of the new image; it does not erase its own execution region.

This separates normal application operation from self-update execution into two explicit runtime contexts.

## FreeRTOS Runtime Model

The App uses statically allocated FreeRTOS resources, disables dynamic allocation, and does not link heap_4. Static queues, event groups, mutexes, software timers, and task notifications carry requests and completion results.

| Task | Main responsibility | Resource ownership |
|---|---|---|
| ProtocolTask | Serial reception, frame parsing, protocol dispatch, responses | USART1 and protocol queues |
| SampleTask | ADC/DMA acquisition, filtering, ratio conversion | ADC/DAC and sample snapshot |
| StorageTask | TF/FatFs, configuration, samples, alarms, audit, Flash KV | App-side TF/FatFs/SPI Flash |
| AlarmTask | Thresholds, consecutive counts, hysteresis, ACTIVE/RECOVERED | Alarm state and alarm requests |
| ControlTask | CLI/key/protocol commands, async requests, configuration | System coordination and pending requests |
| DisplayTask | OLED refresh, key scanning, display service | App-side I2C/OLED |
| HealthTask | Heartbeats, busy/progress/deadline, watchdog | Health state and IWDG |

Typical data flow:

~~~text
ADC/DMA
   ↓
SampleTask
   ├──→ DisplayTask: latest-value display
   └──→ AlarmTask: threshold evaluation
                  └──→ StorageTask: alarm persistence

USART / RS485
   ↓
ISR + RingBuffer
   ↓
ProtocolTask
   ↓ request/result queues
ControlTask
   ├──→ protocol and configuration services
   ├──→ SampleTask: acquisition control
   └──→ StorageTask: persistence requests

HealthTask
   └──→ watchdog, health state, and low-power coordination
~~~

Key constraints:

- ISRs only receive bytes, record timing, set events, or notify tasks;
- ProtocolTask does not directly modify business configuration or storage;
- ControlTask and AlarmTask do not call FatFs or Flash-KV garbage collection directly;
- StorageTask is the only App context for FatFs and persistence I/O;
- DisplayTask is the only App owner of OLED/I2C;
- cross-task requests carry request_id, origin, protocol sequence, and deadline;
- long I/O exposes busy/progress/deadline state to HealthTask.

## Hardware Abstraction and Interfaces

| Hardware function | MCU resources | Software path |
|---|---|---|
| System clock | 25 MHz HXTAL → 240 MHz | Driver/CMSIS → BSP clock initialization |
| RTC | 32.768 kHz LSE | board_rtc → time service |
| ADC CH0/CH1 | PC0 / PC1 | board_adc → SampleTask |
| DAC | PA4 | board_dac, PA4 looped back to PC1 |
| Debug CLI | USART0 PA9/PA10 | board_usart → CLI → ControlTask |
| RS485/RS232 | USART1 PA2/PA3, PA1 direction control | board_usart → ProtocolTask |
| External serial | USART2 PB10/PB11 | board_usart |
| OLED | I2C0 PB8/PB9 | board_i2c/board_oled → DisplayTask |
| SPI NOR | SPI1 PB13-PB15, CS=PB12 | board_spi_flash → FlashKV/Bootloader |
| TF card | SDIO PC8-PC12, PD2, detect=PE2 | board_sdio → FatFs → StorageTask |
| LEDs/keys | LEDs PD8-PD13; keys PE15, PE13, PE11, PE9, PE7, PB0 | board_gpio/board_key |

## Storage and Flash Layout

### Internal Flash

The GD32F470 internal Flash is 512 KiB and covers 0x08000000-0x0807FFFF.

| Region | Start | End | Size | Purpose |
|---|---:|---:|---:|---|
| Bootloader | 0x08000000 | 0x0800FFFF | 64 KiB | Startup and upgrade |
| Legacy metadata reserve | 0x08010000 | 0x08011FFF | 8 KiB | Retired layout placeholder |
| App | 0x08012000 | 0x08031FFF | 128 KiB | FreeRTOS application |
| Backup | 0x08032000 | 0x08051FFF | 128 KiB | Previous App and rollback |
| Staging | 0x08052000 | 0x08071FFF | 128 KiB | New firmware staging |
| Tail reserve | 0x08072000 | 0x0807FFFF | 56 KiB | Reserved |

App, Backup, and Staging reserve their final 64 bytes for a manifest. The App manifest is at 0x08031FC0. Upgrade metadata is external so programming the App cannot erase the internal metadata sector.

### GD25Q40E

GD25Q40E provides 4 Mbit, or 512 KiB, with 4 KiB sectors.

| Region | Offset | Owner | Purpose |
|---|---:|---|---|
| Business KV slots A/B | sector 0/1 | StorageTask | Configuration and business persistence |
| Upgrade metadata slots A/B | sector 2/3 | Bootloader update state machine | active/pending/backup, state, version, CRC, source |
| Diagnostic sector | 0x7F000 | Diagnostic tools | Diagnostic data |

Metadata uses a fixed 72-byte serialization with CRC, generation, and a final commit marker. Alternating slots preserve a valid record if the other slot fails during a write.

## Communication Architecture

### USART0 CLI

USART0 is the local configuration and diagnostic interface. The ISR only receives and queues input; CLI parsing and execution run in ControlTask. It covers device ID, baud rate, sampling period, ratios, thresholds, protocol mode, RTC, sampling control, display state, configuration persistence, and TF status.

### Custom RS485 Protocol

USART1 uses PA1 for RS485 direction control. The protocol layer encapsulates frame-header search, partial frames, concatenated frames, noise prefixes, CRC resynchronization, invalid lengths, duplicate-frame caching, and asynchronous request/result matching.

### Modbus RTU

Modbus uses USART1 in 8E1 mode and supports functions 03, 04, 06, and 10, standard exception responses, CRC16-Modbus, silent broadcast writes, and custom/Modbus mode switching. Requests pass through ProtocolTask, request queues, and ControlTask without bypassing application ownership.

## Bootloader Update Architecture

The Bootloader is a separate bare-metal image that uses Common address/serialization contracts and BSP internal-Flash, SPI-NOR, SDIO, USART, and OLED interfaces.

### Online Update

~~~text
App
  → 0x0500 ENTER_BOOT
  → persist the request and reply READY
  → software reset into Bootloader
  → 0x0501 BEGIN
  → 0x0502 DATA: write Staging, read back, then ACK
  → 0x0503 END: validate length, CRC, manifest, vector table
  → 0x0504 INSTALL: Backup → erase App → program App → verify
  → TRIAL_PENDING
  → App health confirmation CONFIRMED
  → atomic active=pending commit
~~~

0x0505 ABORT clears the online-update context. State and installation substages are persisted before destructive operations, and FWDGT is serviced during long waits and chunked erase/program operations.

### TF Offline Update

~~~text
firmware/app.bin
  → prepare staging
  → copy payload and read back
  → validate length/CRC/vector table
  → generate manifest
  → STAGED_VALID
  → shared INSTALL path
  → TRIAL_PENDING / CONFIRMED
  → idempotently rename app.bin to .applied
~~~

Failed packages are isolated with .failed. Offline cleanup, metadata state, and image state jointly determine whether the next boot continues update processing.

### OLED Upgrade Indicator

The Bootloader uses an independent SSD1306 indicator:

- Staging and BEGIN: 0%;
- DATA write and read-back: 0%-90%;
- END validation and metadata commit: approximately 90%-95%;
- INSTALL copy, erase, and verification: 96%-100%;
- clear and turn off the OLED before jumping to the App;
- fall back to LED indication if OLED initialization or refresh fails.

## Low-Power and System-Level Closure

Low-power operation is part of the complete system architecture. It is a cross-task transaction that lets every owner finish its work before hardware is disabled.

~~~mermaid
flowchart TD
    CMD["RS485 0x03AA"]
    P["ProtocolTask<br/>receive and parse"]
    C["ControlTask<br/>create sleep request"]
    PM["PowerManager<br/>system coordinator"]
    S["StorageTask<br/>stop requests, flush, close files"]
    A["SampleTask<br/>stop ADC/DMA"]
    R["ProtocolTask / RS485<br/>wait for TX complete"]
    H["HealthTask<br/>health check, sleep-ready, IWDG window"]
    HW["BSP RTC / EXTI / PMU<br/>configure wake sources"]
    WFI["WFI deep sleep"]
    RESTORE["Restore clocks, peripherals, tasks, timers"]
    TICK["Re-align vTaskDelayUntil time bases"]
    EVENT["0x0381 wake event"]

    CMD --> P --> C --> PM
    PM --> S
    PM --> A
    PM --> R
    PM --> H
    PM --> HW --> WFI
    WFI --> RESTORE --> TICK --> EVENT
    EVENT --> P
~~~

The integration with the rest of the system is:

- ProtocolTask identifies 0x03AA; ControlTask owns the asynchronous request lifecycle;
- PowerManager coordinates tasks instead of letting ControlTask disable peripherals directly;
- StorageTask stops new persistence requests, completes the current write and FatFs flush, then closes files;
- SampleTask stops ADC/DMA so peripherals do not continue generating events during sleep;
- ProtocolTask waits for RS485 transmission to finish so the bus is not left with a partial frame;
- HealthTask checks task health and sets an IWDG window covering sleep and wake-up initialization;
- BSP RTC/EXTI/PMU interfaces provide timed wake-up, key wake-up, and WFI entry;
- wake-up restores clocks, peripherals, scheduler state, software timers, and task timing bases;
- ProtocolTask sends 0x0381 after recovery to notify the host that the device is awake.

## Build, Packaging, and Programming

Keil AC5 is the official release build chain. EIDE configuration remains available for development, while the Boot release build is defined by the Keil project.

PowerShell example:

~~~powershell
$UV4 = "D:\keil5\UV4\UV4.exe"
& $UV4 -b "MDK\IndustrialEmbedded-Boot.uvprojx"
& $UV4 -b "MDK\IndustrialEmbedded-App.uvprojx"
~~~

Main artifacts:

| Artifact | Path | Programming address |
|---|---|---:|
| Bootloader BIN | MDK/ObjectsBoot/IndustrialEmbedded-Boot.bin | 0x08000000 |
| App BIN | MDK/ObjectsApp/IndustrialEmbedded-App.bin | 0x08012000 |
| App manifest HEX | MDK/ObjectsApp/IndustrialEmbedded-App-with-manifest.hex | Address records embedded in HEX |
| TF offline package | MDK/ObjectsApp/IndustrialEmbedded-App-vXX-offline.bin | TF firmware directory |

Delivery tools:

- tools/pack_app_manifest.py generates the App manifest;
- tools/pack_offline_firmware.py generates the TF offline package;
- tools/m6_upgrade_host.py drives the online update over serial.

## Repository Structure

~~~text
Driver/CMSIS/       Cortex-M4, GD32 startup, and linker scripts
Libraries/          GD32F4xx vendor peripheral library
BSP/                Board hardware adapters and device drivers
Common/             Shared addresses, CRC, serialization, and reset contracts
Middleware/         FreeRTOS, FatFs, Protocol, Modbus, FlashKV, CLI, RingBuffer
Tasks/              Seven FreeRTOS application tasks
App/                Application entry, configuration, protocol, update request/confirmation
Bootloader/         Startup, online/offline update, metadata, install, trial, rollback
MDK/                Keil projects, linker configuration, and build outputs
test/               C/Unity/Python host and component tests
tools/              Firmware packaging and online-update tools
Docs/               Project overview, partition, process, and acceptance records
~~~

## Design Characteristics

- **Layered abstraction:** hardware, board adaptation, shared contracts, middleware, business, and tasks have explicit boundaries;
- **Interface encapsulation:** upper layers use board_ APIs, services, and queues rather than register details;
- **Resource ownership:** StorageTask owns FatFs/persistence, DisplayTask owns OLED, and ProtocolTask owns communication;
- **Asynchronous decoupling:** requests carry request_id, origin, sequence, and deadline, while results are matched explicitly;
- **Fault safety:** CRC, dual-slot metadata, manifests, Backup, Staging, trial confirmation, rollback, and watchdog handling form the recovery chain;
- **Runtime-image isolation:** Bootloader and App use separate link regions and execution models;
- **Maintainable delivery:** build, packaging, host update, design contracts, and acceptance records are separated into dedicated directories.

## Related Documents

- [Project overview](Docs/01_PROJECT_OVERVIEW.md)
- [Development process and design records](Docs/03_DEV_PROCESS.md)
- [M6 partition freeze contract](Docs/M6_0A_PARTITION_FREEZE.md)
- [M5 configuration contract draft](Docs/M5_CONFIG_INI_CONTRACT.md)
- [中文 README](README.md)
