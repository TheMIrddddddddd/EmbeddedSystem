#include "upgrade_serialization.h"
#include "common_crc.h"

static void write_u16_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static uint16_t read_u16_le(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] |
           ((uint16_t)buffer[1] << 8U);
}

static void write_u32_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFUL);
    buffer[1] = (uint8_t)((value >> 8U) & 0xFFUL);
    buffer[2] = (uint8_t)((value >> 16U) & 0xFFUL);
    buffer[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}

static uint32_t read_u32_le(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] |
           ((uint32_t)buffer[1] << 8U) |
           ((uint32_t)buffer[2] << 16U) |
           ((uint32_t)buffer[3] << 24U);
}

static fw_format_status_t firmware_header_validate(const firmware_header_t *header)
{
    if (header == NULL)
    {
        return FW_FORMAT_STATUS_INVALID_ARGUMENT;
    }

    if (header->magic != FIRMWARE_HEADER_MAGIC)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    if (header->package_version != FIRMWARE_HEADER_PACKAGE_VERSION)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    if ((header->header_size != FIRMWARE_HEADER_SIZE))
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }
    
    if ((header->image_size == 0U) || 
        (header->image_size > FIRMWARE_HEADER_MAX_IMAGE_SIZE))
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }
    
    if (header->target_address != FIRMWARE_HEADER_TARGET_ADDRESS)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    if ((header->flags & ~FIRMWARE_HEADER_VALID_FLAGS) != 0U)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    return FW_FORMAT_STATUS_OK;
}

static fw_format_status_t image_manifest_validate(
    const image_manifest_t *manifest)
{
    if (manifest == NULL)
    {
        return FW_FORMAT_STATUS_INVALID_ARGUMENT;
    }

    if (manifest->magic != IMAGE_MANIFEST_MAGIC)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    if ((manifest->image_size == 0U) ||
        (manifest->image_size > FIRMWARE_HEADER_MAX_IMAGE_SIZE))
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    return FW_FORMAT_STATUS_OK;
}

static fw_format_status_t upgrade_meta_validate(
    const upgrade_meta_t *meta)
{
    if (meta == NULL)
    {
        return FW_FORMAT_STATUS_INVALID_ARGUMENT;
    }

    if (meta->magic != UPGRADE_META_MAGIC)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    if (meta->meta_version != UPGRADE_META_VERSION)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    if (meta->state > FW_STATE_ROLLED_BACK)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    if (meta->install_stage > INSTALL_APP_VALID)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    if (meta->upgrade_source > UPGRADE_SOURCE_TF_OFFLINE)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    if (meta->commit_marker != UPGRADE_META_COMMIT_MARKER)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    return FW_FORMAT_STATUS_OK;
}

fw_format_status_t firmware_header_encode(
    const firmware_header_t *header,
    uint8_t *buffer,
    size_t buffer_size)
{
    uint32_t header_crc;

    if ((header == NULL) || (buffer == NULL))
    {
        return FW_FORMAT_STATUS_INVALID_ARGUMENT;
    }

    if (buffer_size < FIRMWARE_HEADER_SIZE)
    {
        return FW_FORMAT_STATUS_OUTPUT_TOO_SMALL;
    }

    if (firmware_header_validate(header) != FW_FORMAT_STATUS_OK)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    write_u32_le(&buffer[0], header->magic);
    write_u16_le(&buffer[4], header->package_version);
    write_u16_le(&buffer[6], header->header_size);
    write_u32_le(&buffer[8], header->firmware_version);
    write_u32_le(&buffer[12], header->image_size);
    write_u32_le(&buffer[16], header->target_address);
    write_u32_le(&buffer[20], header->image_crc32);
    write_u32_le(&buffer[24], header->flags);

    /*
     * CRC 范围是前 28 字节：
     * magic 到 flags，不包含 header_crc32 自身。
     */

    header_crc = common_crc32_calc(buffer, 28U);
    write_u32_le(&buffer[28], header_crc);

    return FW_FORMAT_STATUS_OK;
}
fw_format_status_t firmware_header_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    firmware_header_t *header)
{
    uint32_t stored_crc;
    uint32_t calculated_crc;
    fw_format_status_t status;

    if ((buffer == NULL) || (header == NULL))
    {
        return FW_FORMAT_STATUS_INVALID_ARGUMENT;
    }

    if (buffer_size < FIRMWARE_HEADER_SIZE)
    {
        return FW_FORMAT_STATUS_OUTPUT_TOO_SMALL;
    }

    header->magic = read_u32_le(&buffer[0]);
    header->package_version = read_u16_le(&buffer[4]);
    header->header_size = read_u16_le(&buffer[6]);
    header->firmware_version = read_u32_le(&buffer[8]);
    header->image_size = read_u32_le(&buffer[12]);
    header->target_address = read_u32_le(&buffer[16]);
    header->image_crc32 = read_u32_le(&buffer[20]);
    header->flags = read_u32_le(&buffer[24]);
    header->header_crc32 = read_u32_le(&buffer[28]);

    status = firmware_header_validate(header);

    if (status != FW_FORMAT_STATUS_OK)
    {
        return status;
    }

    stored_crc = header->header_crc32;
    calculated_crc = common_crc32_calc(buffer, 28U);

    if (stored_crc != calculated_crc)
    {
        return FW_FORMAT_STATUS_CRC_ERROR;
    }

    return FW_FORMAT_STATUS_OK;
    
}

fw_format_status_t image_manifest_encode(
    const image_manifest_t *manifest,
    uint8_t *buffer,
    size_t buffer_size)
{
    uint32_t manifest_crc;

    if ((manifest == NULL) || (buffer == NULL))
    {
        return FW_FORMAT_STATUS_INVALID_ARGUMENT;
    }

    if (buffer_size < IMAGE_MANIFEST_SIZE)
    {
        return FW_FORMAT_STATUS_OUTPUT_TOO_SMALL;
    }

    if (image_manifest_validate(manifest) != FW_FORMAT_STATUS_OK)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    write_u32_le(&buffer[0], manifest->magic);
    write_u32_le(&buffer[4], manifest->image_version);
    write_u32_le(&buffer[8], manifest->image_size);
    write_u32_le(&buffer[12], manifest->image_crc32);

    /*
     * CRC 范围是前 16 字节，
     * 不包含 manifest_crc32 自身。
     */
    manifest_crc = common_crc32_calc(buffer, 16U);
    write_u32_le(&buffer[16], manifest_crc);

    return FW_FORMAT_STATUS_OK;
}

fw_format_status_t image_manifest_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    image_manifest_t *manifest)
{
    uint32_t stored_crc;
    uint32_t calculated_crc;
    fw_format_status_t status;

    if ((buffer == NULL) || (manifest == NULL))
    {
        return FW_FORMAT_STATUS_INVALID_ARGUMENT;
    }

    if (buffer_size < IMAGE_MANIFEST_SIZE)
    {
        return FW_FORMAT_STATUS_OUTPUT_TOO_SMALL;
    }

    manifest->magic = read_u32_le(&buffer[0]);
    manifest->image_version = read_u32_le(&buffer[4]);
    manifest->image_size = read_u32_le(&buffer[8]);
    manifest->image_crc32 = read_u32_le(&buffer[12]);
    manifest->manifest_crc32 = read_u32_le(&buffer[16]);

    status = image_manifest_validate(manifest);

    if (status != FW_FORMAT_STATUS_OK)
    {
        return status;
    }

    stored_crc = manifest->manifest_crc32;
    calculated_crc = common_crc32_calc(buffer, 16U);

    if (stored_crc != calculated_crc)
    {
        return FW_FORMAT_STATUS_CRC_ERROR;
    }

    return FW_FORMAT_STATUS_OK;
}

fw_format_status_t upgrade_meta_encode(
    const upgrade_meta_t *meta,
    uint8_t *buffer,
    size_t buffer_size)
{
    uint32_t meta_crc;

    if ((meta == NULL) || (buffer == NULL))
    {
        return FW_FORMAT_STATUS_INVALID_ARGUMENT;
    }

    if (buffer_size < UPGRADE_META_SIZE)
    {
        return FW_FORMAT_STATUS_OUTPUT_TOO_SMALL;
    }

    if (upgrade_meta_validate(meta) != FW_FORMAT_STATUS_OK)
    {
        return FW_FORMAT_STATUS_INVALID_FIELD;
    }

    write_u32_le(&buffer[0], meta->magic);
    write_u32_le(&buffer[4], meta->meta_version);
    write_u32_le(&buffer[8], meta->generation);

    buffer[12] = meta->state;
    buffer[13] = meta->install_stage;
    buffer[14] = meta->failure_count;
    buffer[15] = meta->upgrade_source;

    write_u32_le(&buffer[16], meta->active_size);
    write_u32_le(&buffer[20], meta->active_crc32);
    write_u32_le(&buffer[24], meta->active_version);

    write_u32_le(&buffer[28], meta->backup_size);
    write_u32_le(&buffer[32], meta->backup_crc32);
    write_u32_le(&buffer[36], meta->backup_version);

    write_u32_le(&buffer[40], meta->pending_size);
    write_u32_le(&buffer[44], meta->pending_crc32);
    write_u32_le(&buffer[48], meta->pending_version);

    write_u32_le(&buffer[52], meta->failed_package_crc32);
    write_u32_le(&buffer[56], meta->failed_package_version);

    /*
     * CRC 范围为前 60 字节。
     */
    meta_crc = common_crc32_calc(buffer, 60U);
    write_u32_le(&buffer[60], meta_crc);

    /*
     * commit_marker 是最后 4 字节。
     */
    write_u32_le(&buffer[64], meta->commit_marker);

    return FW_FORMAT_STATUS_OK;
}

fw_format_status_t upgrade_meta_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    upgrade_meta_t *meta)
{
    uint32_t stored_crc;
    uint32_t calculated_crc;
    fw_format_status_t status;

    if ((buffer == NULL) || (meta == NULL))
    {
        return FW_FORMAT_STATUS_INVALID_ARGUMENT;
    }

    if (buffer_size < UPGRADE_META_SIZE)
    {
        return FW_FORMAT_STATUS_OUTPUT_TOO_SMALL;
    }

    meta->magic = read_u32_le(&buffer[0]);
    meta->meta_version = read_u32_le(&buffer[4]);
    meta->generation = read_u32_le(&buffer[8]);

    meta->state = buffer[12];
    meta->install_stage = buffer[13];
    meta->failure_count = buffer[14];
    meta->upgrade_source = buffer[15];

    meta->active_size = read_u32_le(&buffer[16]);
    meta->active_crc32 = read_u32_le(&buffer[20]);
    meta->active_version = read_u32_le(&buffer[24]);

    meta->backup_size = read_u32_le(&buffer[28]);
    meta->backup_crc32 = read_u32_le(&buffer[32]);
    meta->backup_version = read_u32_le(&buffer[36]);

    meta->pending_size = read_u32_le(&buffer[40]);
    meta->pending_crc32 = read_u32_le(&buffer[44]);
    meta->pending_version = read_u32_le(&buffer[48]);

    meta->failed_package_crc32 = read_u32_le(&buffer[52]);
    meta->failed_package_version = read_u32_le(&buffer[56]);

    meta->crc32 = read_u32_le(&buffer[60]);
    meta->commit_marker = read_u32_le(&buffer[64]);

    status = upgrade_meta_validate(meta);

    if (status != FW_FORMAT_STATUS_OK)
    {
        return status;
    }

    stored_crc = meta->crc32;
    calculated_crc = common_crc32_calc(buffer, 60U);

    if (stored_crc != calculated_crc)
    {
        return FW_FORMAT_STATUS_CRC_ERROR;
    }

    return FW_FORMAT_STATUS_OK;
}

