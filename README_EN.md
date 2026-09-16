# GD32F470 Industrial Data Acquisition Terminal

[中文版](README.md)

> An industrial data acquisition terminal built around GD32F470VET6, FreeRTOS, RS485, Modbus RTU, TF/FatFs storage, and a recoverable Bootloader upgrade path.

## Project Positioning

This project targets an industrial data acquisition and maintenance scenario. It integrates the following system capabilities on a GD32F470VET6 control board:

- ADC voltage acquisition, filtering, ratio conversion, and DAC loopback;
- USART0 CLI, a custom USART1 RS485 protocol, and Modbus RTU;
- OLED, LED, key input, and RTC services;
- TF card/FatFs storage for samples, alarms, audit records, and configuration;
- GD25Q40E external SPI NOR storage for parameters and alarms;
- Bootloader online IAP, TF offline upgrade, dual-slot metadata, trial confirmation, and rollback;
- A complete architectural plan for future low-power operation.

The project demonstrates task ownership, asynchronous requests, reliable storage, communication boundaries, and recoverable firmware upgrades rather than isolated peripheral demos.

## Current Status and Closure Scope

**Current status: the selected M0-M6 scope is complete and the project is in its final stage.**

Because of the internship/job-search schedule, development and testing are paused after M6. M7 is retained as future work and must not be described as implemented.

| Stage | Current conclusion | Main scope |
|---|---|---|
| M0 | Complete | Hardware resources, clock, DMA, Flash boundaries, and interface ownership |
| M1 | Complete | Boot/App projects, partitioned linking, vector table, and Boot-to-App jump |
| M2 | Complete | CRC, RingBuffer, CLI, protocol frames, Flash KV, and upgrade serialization |
| M3 | Core complete | Static FreeRTOS tasks, drivers, StorageTask, and health monitoring |
| M4 | Core complete | ADC/DAC, sampling conversion, CLI, custom protocol, Modbus, and reporting |
| M5 | Core complete | TF/FatFs, configuration persistence, alarms, audit, hotplug recovery, and TF state |
| M6 | Complete for the selected scope | Online/offline upgrade, dual-slot metadata, confirmation/rollback, OLED progress |
| M7 | Deferred | Low-power sleep/wake and final system-wide acceptance |

M6 items N-05, N-07, and N-08, which require random power-loss or rename-power-loss testing, were intentionally not executed for the current project scope. They must not be reported as passed. The 24-hour run, 100-invalid-frame stress test, and complete power-loss matrix are also outside the current closure evidence.

## System Architecture

~~~mermaid
flowchart LR
    HOST["PC / USB-RS485"]
    BOOT["Bootloader<br/>Online IAP + TF offline upgrade<br/>Dual-slot metadata + rollback"]
    APP["Application<br/>Static FreeRTOS tasks"]
    META["GD25Q40E<br/>Business KV + upgrade metadata"]
    FLASH["Internal Flash<br/>App / Backup / Staging"]
    TF["TF card<br/>SDIO + FatFs"]
    ADC["ADC / DAC<br/>Acquisition loopback"]
    OLED["OLED / LED / keys"]
    RTC["RTC / watchdog"]

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

The firmware consists of two independent but connected images:

1. The Bootloader occupies the beginning of internal Flash and owns startup dispatch, upgrade protocol, image validation, backup/copy operations, atomic metadata commits, trial handling, and rollback.
2. The App occupies a separate internal Flash region and runs FreeRTOS together with the acquisition, communication, storage, alarm, display, and health services.

## Application Tasks and Ownership

The App uses statically allocated FreeRTOS objects, disables dynamic allocation, and does not link heap_4. Queues, event groups, mutexes, timers, and task notifications carry requests and completion results.

| Task | Main responsibility | Typical resources |
|---|---|---|
| ProtocolTask | USART reception, frame handling, CRC, protocol dispatch, responses | USART1, RS485, request/result queues |
| SampleTask | ADC/DMA acquisition, three-sample mean, ratio conversion, latest snapshot | ADC1, DAC loopback, sample queue |
| StorageTask | TF/FatFs, configuration, samples, alarms, audit, external persistence | TF, FatFs, GD25Q40E |
| AlarmTask | Threshold detection, consecutive counts, hysteresis, ACTIVE/RECOVERED state machine | Sample results, alarm requests |
| ControlTask | Command coordination, configuration application, async requests, result matching | CLI, keys, business requests |
| DisplayTask | OLED refresh, key scanning, display service | I2C0, SSD1306, keys |
| HealthTask | Heartbeats, deadline/busy state, stack watermarks, watchdog decisions | IWDG, health state |

Important ownership rules:

- ISRs only receive bytes, record timing, and post events. They do not parse protocols, print, or erase/program Flash.
- StorageTask is the only App context allowed to execute FatFs and persistence operations.
- ControlTask and AlarmTask do not call FatFs, run Flash KV GC, or hold the SPI lock directly.
- DisplayTask is the App-side owner of the OLED/I2C display.
- ProtocolTask owns framing and protocol encoding; ControlTask executes business logic.
- Bootloader and App do not run concurrently. Bootloader exclusively accesses upgrade-related external storage during upgrade processing.

Typical data flow:

~~~text
USART / keys / CLI
        ↓
ProtocolTask / ControlTask
        ↓
SampleTask ──→ AlarmTask ──→ StorageTask ──→ TF / GD25Q40E
        │                         ↑
        └────────→ DisplayTask    │
                                  │
HealthTask ── health/sleep-ready ┘
~~~

## Hardware and Interfaces

| Function | MCU resources | Board usage |
|---|---|---|
| System clock | 25 MHz HXTAL → 240 MHz | GD32F470 system clock |
| RTC | 32.768 kHz LSE | Calendar, Unix time, future wake-up |
| ADC CH0/CH1 | PC0 / PC1 | Two analog inputs |
| DAC | PA4 | DAC output looped back to PC1 |
| USART0 | PA9 / PA10 | H7, onboard CH340, CLI |
| USART1 | PA2 / PA3, PA1 direction control | H6, RS232/RS485 |
| USART2 | PB10 / PB11 | CN1 external serial port |
| OLED | I2C0 PB8 / PB9 | SSD1306 128×32 |
| SPI NOR | SPI1 PB13-PB15, CS=PB12 | GD25Q40E |
| TF card | SDIO PC8-PC12, PD2, detect=PE2 | SDIO block device and FatFs |
| LEDs | PD8-PD13 | System, protocol, alarm, and upgrade indicators |
| Keys | PE15, PE13, PE11, PE9, PE7, PB0 | Key scanning and input events |

## Flash and Storage Layout

### GD32F470 Internal Flash

The internal Flash is 512 KiB, covering 0x08000000-0x0807FFFF.

| Region | Start | End | Size | Purpose |
|---|---:|---:|---:|---|
| Bootloader | 0x08000000 | 0x0800FFFF | 64 KiB | Startup and upgrade |
| Legacy metadata reserve | 0x08010000 | 0x08011FFF | 8 KiB | Retired; layout placeholder only |
| App | 0x08012000 | 0x08031FFF | 128 KiB | FreeRTOS application |
| Backup | 0x08032000 | 0x08051FFF | 128 KiB | Previous App and rollback |
| Staging | 0x08052000 | 0x08071FFF | 128 KiB | New firmware staging |
| Tail reserve | 0x08072000 | 0x0807FFFF | 56 KiB | Currently unused |

App, Backup, and Staging reserve their final 64 bytes for a manifest. The App manifest is at 0x08031FC0. The old internal-Flash metadata slots were retired because programming the App can erase their shared sector.

### GD25Q40E External SPI NOR

GD25Q40E provides 4 Mbit, or 512 KiB, with 4 KiB sectors.

| Region | Offset | Purpose |
|---|---:|---|
| Business KV slots A/B | sector 0/1 | Ratios, thresholds, device ID, baud rate, protocol mode |
| Upgrade metadata slots A/B | sector 2/3 | Upgrade state, active/pending/backup, version, CRC, source |
| Diagnostic sector | 0x7F000 | Diagnostic data |

Upgrade metadata uses a fixed 72-byte serialization with CRC, generation, and a final commit marker to provide alternating slots and atomic selection.

## Communication Interfaces

### USART0 CLI

USART0 provides local configuration, diagnostics, and mode control. CLI parsing and execution run in ControlTask; the ISR only receives and queues input. Capabilities include:

- Device ID, baud rate, sampling period, ratios, and thresholds;
- Protocol mode switching;
- RTC configuration and queries;
- Sampling start/stop, display hide/unhide, and diagnostics;
- Configuration save/read and TF status queries.

### Custom RS485 Protocol

USART1 uses PA1 for RS485 direction control. The stream parser handles frame-header search, partial frames, concatenated frames, noise prefixes, CRC resynchronization, duplicate-frame caching, and asynchronous request/result matching.

Covered business areas include device information, reset, configuration, ADC/DAC, thresholds, ratios, TF status, automatic reporting, alarm events, heartbeat, invalid-frame handling, busy responses, and broadcast silence.

### Modbus RTU

Modbus uses USART1 in 8E1 mode and supports functions 03, 04, 06, and 10, standard exception responses, CRC16-Modbus, silent broadcast writes, and custom/Modbus mode switching. Modbus business execution remains in ControlTask and cannot bypass task ownership.

## Bootloader Upgrade Path

### Online Upgrade

The App and host cooperate as follows:

1. The host sends 0x0500 ENTER_BOOT.
2. The App persists the upgrade request and replies READY.
3. The App resets into the Bootloader.
4. The Bootloader waits for 0x0501 BEGIN.
5. 0x0502 DATA chunks are written to Staging, read back, verified, and acknowledged.
6. 0x0503 END validates length, CRC, manifest, and the vector table.
7. 0x0504 INSTALL performs Backup, App erase, App programming, and whole-image verification.
8. The new App enters TRIAL_PENDING.
9. The App writes CONFIRMED after its health conditions are satisfied.
10. The Bootloader commits active=pending, or rolls back according to the state machine.

0x0505 ABORT atomically clears the online upgrade context. Ordinary commands are not accepted during installation; state and the installation substage are persisted before destructive operations.

### TF Offline Upgrade

The Bootloader checks TF firmware/app.bin and performs:

~~~text
Read firmware header
  → prepare staging
  → copy payload and read back
  → validate length/CRC/vector table
  → generate manifest
  → STAGED_VALID
  → shared INSTALL path
  → CONFIRMED
  → rename app.bin idempotently to .applied
~~~

Failed packages are isolated with .failed. If offline cleanup is incomplete, the Bootloader still starts a valid App but blocks new online/offline upgrades until the original TF file is correctly cleaned up.

### OLED Upgrade Indicator

The Bootloader has an independent SSD1306 indicator:

- BEGIN/staging preparation: 0%;
- DATA reception: 0%-90%;
- END validation and metadata commit: approximately 90%-95%;
- INSTALL copy, erase, and verification: 96%-100%;
- Clear and turn off the OLED before jumping to the App;
- Fall back to LED indication if OLED initialization or refresh fails.

## Build and Programming

Keil AC5 is the official release build chain. The EIDE Boot target still shares the App source tree and is not the official release chain for this project.

PowerShell build example:

~~~powershell
$UV4 = "D:\keil5\UV4\UV4.exe"
& $UV4 -b "MDK\IndustrialEmbedded-Boot.uvprojx"
& $UV4 -b "MDK\IndustrialEmbedded-App.uvprojx"
~~~

Replace the UV4 path with the local Keil installation path.

Main artifacts:

| Artifact | Path | Programming address |
|---|---|---:|
| Bootloader BIN | MDK/ObjectsBoot/IndustrialEmbedded-Boot.bin | 0x08000000 |
| App BIN | MDK/ObjectsApp/IndustrialEmbedded-App.bin | 0x08012000 |
| App manifest HEX | MDK/ObjectsApp/IndustrialEmbedded-App-with-manifest.hex | Encoded in HEX records |
| TF offline package | MDK/ObjectsApp/IndustrialEmbedded-App-vXX-offline.bin | TF firmware directory |

The final M6 record reports 0 errors and 0 warnings for both Boot and App. The Boot BIN is 0xC1AC bytes and the App BIN is 0x168C4 bytes. The exact version, manifest, and CRC belong to the corresponding packaged App image.

## Evidence and Limitations

Recorded M6 evidence includes:

- The Bootloader on the board matched the final local Boot build byte-for-byte;
- The App booted normally and answered over RS485;
- The version-23 online upgrade completed successfully;
- DATA, END, INSTALL, and the post-upgrade boot sequence were traced;
- The OLED framebuffer progressed to 100% and stayed full during the post-install boots;
- N-09 online/TF isolation and N-10 incomplete-offline-cleanup recovery were closed.

The following are not current completion evidence:

- M7 low-power sleep/wake;
- 24-hour continuous operation and final stability acceptance;
- M6 N-05, N-07, and N-08 random power-loss/rename-power-loss cases;
- Twenty random power losses during sample/alarm writes;
- Physical TF-full injection;
- Logic-analyzer measurement of sub-millisecond Modbus timing.

## How the Unimplemented M7 Fits the Complete Architecture

M7 is not an isolated sleep function. It is a system-level transaction that coordinates the existing tasks and ownership boundaries.

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
    WFI["WFI deep sleep"]
    RES["Restore clocks, peripherals, tasks, timers"]
    EVT["0x0381 wake event"]

    CMD --> P --> C --> PM
    PM --> S
    PM --> A
    PM --> R
    PM --> H
    PM --> W --> WFI
    WFI --> RES --> EVT
    EVT --> P
~~~

Therefore, “M7 not implemented” specifically means:

- There is no completed project-level PowerManager entry point;
- 0x03AA remains a reserved M7 command in the App protocol;
- StorageTask, SampleTask, ProtocolTask, HealthTask, and RTC/EXTI are not yet connected into one complete sleep transaction;
- Wake-up timer restoration, vTaskDelayUntil re-alignment, and business enable-state restoration have not been closed or verified.

## Repository Layout

~~~text
Common/       Shared CRC, Flash layout, upgrade serialization, and reset contracts
BSP/          GPIO, ADC, DAC, USART, RS485, RTC, OLED, SPI, and SDIO drivers
Bootloader/   Startup dispatch, online/offline upgrade, metadata, install, trial, rollback
App/          FreeRTOS configuration, application entry, CLI, protocol, confirmation
Tasks/        Protocol/Sample/Storage/Alarm/Control/Display/Health tasks
Middleware/   FreeRTOS, FatFs, Protocol, and Modbus middleware
test/         PC component tests, protocol tests, and host regression tools
tools/        Manifest, offline-package, and online-upgrade host tools
MDK/          Keil AC5 projects, linker settings, and build outputs
Docs/         Project overview, execution process, partition contracts, and records
.cursor/rules/Project-level AI reading and architecture guidance
~~~

## AI Reading and Project Explanation Convention

Any AI reading this repository must first understand the complete M0-M7 architecture before answering about current progress. For an unimplemented module, it must explain how that module is intended to connect to the existing tasks, protocols, hardware, storage, and acceptance flow.

Every status answer must distinguish:

- Implemented;
- Built;
- Board-tested;
- Supported only by design or source evidence;
- Unverified;
- Deferred or intentionally skipped by project decision.

For example, an AI must not only say “M7 is not done.” It must explain how M7 would connect 0x03AA, ControlTask, PowerManager, StorageTask, ADC/DMA, RS485, RTC/EXTI, the watchdog, WFI, and 0x0381, while clearly stating that the code and validation are still missing.

The persistent rule is in [.cursor/rules/industrialembedded-project-context.mdc](.cursor/rules/industrialembedded-project-context.mdc). The execution source of truth is [Docs/03_DEV_PROCESS.md](Docs/03_DEV_PROCESS.md).

## Resume-Oriented Project Summary

Developed an industrial data acquisition terminal based on GD32F470VET6 and FreeRTOS. Implemented ADC/DAC acquisition loopback, digital filtering, custom RS485 protocol, Modbus RTU, TF/FatFs storage, alarm handling, and persistent configuration. Designed and implemented Bootloader online IAP, TF offline upgrade, GD25Q40E dual-slot metadata, trial confirmation/rollback, and OLED upgrade progress indication, with corresponding build and board-level upgrade evidence.

Low-power sleep/wake and long-duration/random-power-loss testing are future extensions and are not claimed as completed in the current version.

## Related Documents

- [Project overview](Docs/01_PROJECT_OVERVIEW.md)
- [Development process and stage records](Docs/03_DEV_PROCESS.md)
- [M6 partition freeze contract](Docs/M6_0A_PARTITION_FREEZE.md)
- [M5 configuration contract draft](Docs/M5_CONFIG_INI_CONTRACT.md)
- [中文 README](README.md)
