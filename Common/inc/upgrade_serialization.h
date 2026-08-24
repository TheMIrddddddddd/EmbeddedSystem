#ifndef UPGRADE_SERIALIZATION_H
#define UPGRADE_SERIALIZATION_H

#include <stddef.h>
#include <stdint.h>

#define FIRMWARE_HEADER_MAGIC                   0x5AA5C33CUL
#define FIRMWARE_HEADER_PACKAGE_VERSION         1U
#define FIRMWARE_HEADER_SIZE                    32U     

#define FIRMWARE_HEADER_TARGET_ADDRESS          0x08012000UL
#define FIRMWARE_HEADER_MAX_IMAGE_SIZE          ((128UL * 1024UL) - 64UL)

#define FIRMWARE_HEADER_FLAG_ALLOW_DOWNGRADE    (1UL << 0)
#define FIRMWARE_HEADER_FLAG_FORCE_UPGRADE      (1UL << 1)
#define FIRMWARE_HEADER_VALID_FLAGS             \
        (FIRMWARE_HEADER_FLAG_ALLOW_DOWNGRADE | FIRMWARE_HEADER_FLAG_FORCE_UPGRADE)

#define IMAGE_MANIFEST_MAGIC                    0x4D4E4653UL
#define IMAGE_MANIFEST_SIZE                     20U

#define UPGRADE_META_MAGIC                      0x554D4454UL
#define UPGRADE_META_VERSION                    1U
#define UPGRADE_META_SIZE                       68U
#define UPGRADE_META_COMMIT_MARKER              0xA5C3C3A5UL

typedef enum
{
    FW_STATE_IDLE = 0,
    FW_STATE_RECEIVING,
    FW_STATE_STAGED_VALID,
    FW_STATE_INSTALLING,
    FW_STATE_TRIAL_PENDING,
    FW_STATE_CONFIRMED,
    FW_STATE_ROLLBACK_REQUIRED,
    FW_STATE_ROLLED_BACK
} firmware_state_t;

typedef enum
{
    INSTALL_BACKUP_START = 0,
    INSTALL_BACKUP_VALID,
    INSTALL_APP_ERASING,
    INSTALL_APP_PROGRAMMING,
    INSTALL_APP_VALID
} install_stage_t;

typedef enum
{
    UPGRADE_SOURCE_NONE = 0,
    UPGRADE_SOURCE_ONLINE,
    UPGRADE_SOURCE_TF_OFFLINE
} upgrade_source_t;

typedef enum
{
    FW_FORMAT_STATUS_OK = 0,
    FW_FORMAT_STATUS_INVALID_ARGUMENT,
    FW_FORMAT_STATUS_OUTPUT_TOO_SMALL,
    FW_FORMAT_STATUS_INVALID_FIELD,
    FW_FORMAT_STATUS_CRC_ERROR
} fw_format_status_t;

typedef struct 
{
    uint32_t magic;
    uint16_t package_version;
    uint16_t header_size;
    uint32_t firmware_version;
    uint32_t image_size;
    uint32_t target_address;
    uint32_t image_crc32;
    uint32_t flags;
    uint32_t header_crc32;
} firmware_header_t;

typedef struct
{
    uint32_t magic;
    uint32_t image_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t manifest_crc32;
} image_manifest_t;

typedef struct
{
    uint32_t magic;
    uint32_t meta_version;
    uint32_t generation;

    uint8_t state;
    uint8_t install_stage;
    uint8_t failure_count;
    uint8_t upgrade_source;

    uint32_t active_size;
    uint32_t active_crc32;
    uint32_t active_version;

    uint32_t backup_size;
    uint32_t backup_crc32;
    uint32_t backup_version;

    uint32_t pending_size;
    uint32_t pending_crc32;
    uint32_t pending_version;

    uint32_t failed_package_crc32;
    uint32_t failed_package_version;

    uint32_t crc32;
    uint32_t commit_marker;
} upgrade_meta_t;

fw_format_status_t firmware_header_encode(
    const firmware_header_t *header,
    uint8_t *buffer,
    size_t buffer_size);

fw_format_status_t firmware_header_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    firmware_header_t *header);

fw_format_status_t image_manifest_encode(
    const image_manifest_t *manifest,
    uint8_t *buffer,
    size_t buffer_size);

fw_format_status_t image_manifest_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    image_manifest_t *manifest);

fw_format_status_t upgrade_meta_encode(
    const upgrade_meta_t *meta,
    uint8_t *buffer,
    size_t buffer_size);

fw_format_status_t upgrade_meta_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    upgrade_meta_t *meta);

#endif /* UPGRADE_SERIALIZATION_H */ 
