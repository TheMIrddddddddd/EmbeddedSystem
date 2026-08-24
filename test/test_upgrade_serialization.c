/* PC tests for firmware header serialization. */

#include "unity.h"
#include "upgrade_serialization.h"

#include <string.h>

static firmware_header_t make_valid_header(void)
{
    firmware_header_t header;

    memset(&header, 0, sizeof(header));
    header.magic = FIRMWARE_HEADER_MAGIC;
    header.package_version = FIRMWARE_HEADER_PACKAGE_VERSION;
    header.header_size = FIRMWARE_HEADER_SIZE;
    header.firmware_version = 0x01000400UL;
    header.image_size = 4096U;
    header.target_address = FIRMWARE_HEADER_TARGET_ADDRESS;
    header.image_crc32 = 0x12345678UL;
    header.flags = FIRMWARE_HEADER_FLAG_FORCE_UPGRADE;

    return header;
}

void test_firmware_header_encode_decode_roundtrip(void)
{
    firmware_header_t expected = make_valid_header();
    firmware_header_t actual;
    uint8_t buffer[FIRMWARE_HEADER_SIZE];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OK,
                      firmware_header_encode(&expected,
                                             buffer,
                                             sizeof(buffer)));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OK,
                      firmware_header_decode(buffer,
                                             sizeof(buffer),
                                             &actual));

    TEST_ASSERT_EQUAL_UINT32(expected.magic, actual.magic);
    TEST_ASSERT_EQUAL_UINT16(expected.package_version,
                             actual.package_version);
    TEST_ASSERT_EQUAL_UINT16(expected.header_size, actual.header_size);
    TEST_ASSERT_EQUAL_UINT32(expected.firmware_version,
                             actual.firmware_version);
    TEST_ASSERT_EQUAL_UINT32(expected.image_size, actual.image_size);
    TEST_ASSERT_EQUAL_UINT32(expected.target_address,
                             actual.target_address);
    TEST_ASSERT_EQUAL_UINT32(expected.image_crc32, actual.image_crc32);
    TEST_ASSERT_EQUAL_UINT32(expected.flags, actual.flags);
}

void test_firmware_header_uses_little_endian_fields(void)
{
    firmware_header_t header = make_valid_header();
    uint8_t buffer[FIRMWARE_HEADER_SIZE];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OK,
                      firmware_header_encode(&header,
                                             buffer,
                                             sizeof(buffer)));

    TEST_ASSERT_EQUAL_UINT8(0x3CU, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0xC3U, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0xA5U, buffer[2]);
    TEST_ASSERT_EQUAL_UINT8(0x5AU, buffer[3]);
    TEST_ASSERT_EQUAL_UINT8(0x01U, buffer[4]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, buffer[5]);
    TEST_ASSERT_EQUAL_UINT8(0x20U, buffer[6]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, buffer[7]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, buffer[8]);
    TEST_ASSERT_EQUAL_UINT8(0x04U, buffer[9]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, buffer[10]);
    TEST_ASSERT_EQUAL_UINT8(0x01U, buffer[11]);
}

void test_firmware_header_rejects_invalid_arguments(void)
{
    firmware_header_t header = make_valid_header();
    firmware_header_t decoded;
    uint8_t buffer[FIRMWARE_HEADER_SIZE];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_ARGUMENT,
                      firmware_header_encode(NULL,
                                             buffer,
                                             sizeof(buffer)));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_ARGUMENT,
                      firmware_header_encode(&header,
                                             NULL,
                                             sizeof(buffer)));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_ARGUMENT,
                      firmware_header_decode(NULL,
                                             sizeof(buffer),
                                             &decoded));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_ARGUMENT,
                      firmware_header_decode(buffer,
                                             sizeof(buffer),
                                             NULL));
}

void test_firmware_header_rejects_small_buffer(void)
{
    firmware_header_t header = make_valid_header();
    uint8_t buffer[FIRMWARE_HEADER_SIZE - 1U];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OUTPUT_TOO_SMALL,
                      firmware_header_encode(&header,
                                             buffer,
                                             sizeof(buffer)));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OUTPUT_TOO_SMALL,
                      firmware_header_decode(buffer,
                                             sizeof(buffer),
                                             &header));
}

void test_firmware_header_rejects_invalid_fields(void)
{
    firmware_header_t header = make_valid_header();
    uint8_t buffer[FIRMWARE_HEADER_SIZE];

    header.magic ^= 1UL;
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_FIELD,
                      firmware_header_encode(&header,
                                             buffer,
                                             sizeof(buffer)));

    header = make_valid_header();
    header.image_size = FIRMWARE_HEADER_MAX_IMAGE_SIZE + 1UL;
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_FIELD,
                      firmware_header_encode(&header,
                                             buffer,
                                             sizeof(buffer)));

    header = make_valid_header();
    header.flags = 0x04UL;
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_FIELD,
                      firmware_header_encode(&header,
                                             buffer,
                                             sizeof(buffer)));
}

void test_firmware_header_rejects_crc_error(void)
{
    firmware_header_t header = make_valid_header();
    firmware_header_t decoded;
    uint8_t buffer[FIRMWARE_HEADER_SIZE];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OK,
                      firmware_header_encode(&header,
                                             buffer,
                                             sizeof(buffer)));
    buffer[20] ^= 0x01U;

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_CRC_ERROR,
                      firmware_header_decode(buffer,
                                             sizeof(buffer),
                                             &decoded));
}

static image_manifest_t make_valid_manifest(void)
{
    image_manifest_t manifest;

    memset(&manifest, 0, sizeof(manifest));
    manifest.magic = IMAGE_MANIFEST_MAGIC;
    manifest.image_version = 0x01000400UL;
    manifest.image_size = 4096U;
    manifest.image_crc32 = 0x89ABCDEFUL;

    return manifest;
}

void test_image_manifest_encode_decode_roundtrip(void)
{
    image_manifest_t expected = make_valid_manifest();
    image_manifest_t actual;
    uint8_t buffer[IMAGE_MANIFEST_SIZE];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OK,
                      image_manifest_encode(&expected,
                                             buffer,
                                             sizeof(buffer)));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OK,
                      image_manifest_decode(buffer,
                                             sizeof(buffer),
                                             &actual));

    TEST_ASSERT_EQUAL_UINT32(expected.magic, actual.magic);
    TEST_ASSERT_EQUAL_UINT32(expected.image_version,
                             actual.image_version);
    TEST_ASSERT_EQUAL_UINT32(expected.image_size,
                             actual.image_size);
    TEST_ASSERT_EQUAL_UINT32(expected.image_crc32,
                             actual.image_crc32);
}

void test_image_manifest_uses_little_endian_fields(void)
{
    image_manifest_t manifest = make_valid_manifest();
    uint8_t buffer[IMAGE_MANIFEST_SIZE];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OK,
                      image_manifest_encode(&manifest,
                                             buffer,
                                             sizeof(buffer)));

    TEST_ASSERT_EQUAL_UINT8(0x53U, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0x46U, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0x4EU, buffer[2]);
    TEST_ASSERT_EQUAL_UINT8(0x4DU, buffer[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, buffer[4]);
    TEST_ASSERT_EQUAL_UINT8(0x04U, buffer[5]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, buffer[6]);
    TEST_ASSERT_EQUAL_UINT8(0x01U, buffer[7]);
}

void test_image_manifest_rejects_invalid_arguments(void)
{
    image_manifest_t manifest = make_valid_manifest();
    image_manifest_t decoded;
    uint8_t buffer[IMAGE_MANIFEST_SIZE];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_ARGUMENT,
                      image_manifest_encode(NULL,
                                            buffer,
                                            sizeof(buffer)));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_ARGUMENT,
                      image_manifest_encode(&manifest,
                                            NULL,
                                            sizeof(buffer)));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_ARGUMENT,
                      image_manifest_decode(NULL,
                                            sizeof(buffer),
                                            &decoded));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_ARGUMENT,
                      image_manifest_decode(buffer,
                                            sizeof(buffer),
                                            NULL));
}

void test_image_manifest_rejects_small_buffer(void)
{
    image_manifest_t manifest = make_valid_manifest();
    uint8_t buffer[IMAGE_MANIFEST_SIZE - 1U];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OUTPUT_TOO_SMALL,
                      image_manifest_encode(&manifest,
                                            buffer,
                                            sizeof(buffer)));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OUTPUT_TOO_SMALL,
                      image_manifest_decode(buffer,
                                            sizeof(buffer),
                                            &manifest));
}

void test_image_manifest_rejects_invalid_fields(void)
{
    image_manifest_t manifest = make_valid_manifest();
    uint8_t buffer[IMAGE_MANIFEST_SIZE];

    manifest.magic ^= 1UL;
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_FIELD,
                      image_manifest_encode(&manifest,
                                            buffer,
                                            sizeof(buffer)));

    manifest = make_valid_manifest();
    manifest.image_size = 0U;
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_FIELD,
                      image_manifest_encode(&manifest,
                                            buffer,
                                            sizeof(buffer)));

    manifest = make_valid_manifest();
    manifest.image_size = FIRMWARE_HEADER_MAX_IMAGE_SIZE + 1UL;
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_FIELD,
                      image_manifest_encode(&manifest,
                                            buffer,
                                            sizeof(buffer)));
}

void test_image_manifest_rejects_crc_error(void)
{
    image_manifest_t manifest = make_valid_manifest();
    image_manifest_t decoded;
    uint8_t buffer[IMAGE_MANIFEST_SIZE];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OK,
                      image_manifest_encode(&manifest,
                                             buffer,
                                             sizeof(buffer)));
    buffer[12] ^= 0x01U;

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_CRC_ERROR,
                      image_manifest_decode(buffer,
                                            sizeof(buffer),
                                            &decoded));
}

static upgrade_meta_t make_valid_upgrade_meta(void)
{
    upgrade_meta_t meta;

    memset(&meta, 0, sizeof(meta));
    meta.magic = UPGRADE_META_MAGIC;
    meta.meta_version = UPGRADE_META_VERSION;
    meta.generation = 7U;
    meta.state = FW_STATE_TRIAL_PENDING;
    meta.install_stage = INSTALL_APP_VALID;
    meta.failure_count = 2U;
    meta.upgrade_source = UPGRADE_SOURCE_ONLINE;
    meta.active_size = 1000U;
    meta.active_crc32 = 0x11111111UL;
    meta.active_version = 0x01000000UL;
    meta.backup_size = 2000U;
    meta.backup_crc32 = 0x22222222UL;
    meta.backup_version = 0x00090000UL;
    meta.pending_size = 3000U;
    meta.pending_crc32 = 0x33333333UL;
    meta.pending_version = 0x01010000UL;
    meta.failed_package_crc32 = 0x44444444UL;
    meta.failed_package_version = 0x00080000UL;
    meta.commit_marker = UPGRADE_META_COMMIT_MARKER;

    return meta;
}

void test_upgrade_meta_encode_decode_roundtrip(void)
{
    upgrade_meta_t expected = make_valid_upgrade_meta();
    upgrade_meta_t actual;
    uint8_t buffer[UPGRADE_META_SIZE];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OK,
                      upgrade_meta_encode(&expected,
                                           buffer,
                                           sizeof(buffer)));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OK,
                      upgrade_meta_decode(buffer,
                                           sizeof(buffer),
                                           &actual));

    TEST_ASSERT_EQUAL_UINT32(expected.magic, actual.magic);
    TEST_ASSERT_EQUAL_UINT32(expected.meta_version, actual.meta_version);
    TEST_ASSERT_EQUAL_UINT32(expected.generation, actual.generation);
    TEST_ASSERT_EQUAL_UINT8(expected.state, actual.state);
    TEST_ASSERT_EQUAL_UINT8(expected.install_stage, actual.install_stage);
    TEST_ASSERT_EQUAL_UINT8(expected.failure_count, actual.failure_count);
    TEST_ASSERT_EQUAL_UINT8(expected.upgrade_source, actual.upgrade_source);
    TEST_ASSERT_EQUAL_UINT32(expected.active_size, actual.active_size);
    TEST_ASSERT_EQUAL_UINT32(expected.active_crc32, actual.active_crc32);
    TEST_ASSERT_EQUAL_UINT32(expected.pending_version,
                             actual.pending_version);
    TEST_ASSERT_EQUAL_UINT32(expected.failed_package_crc32,
                             actual.failed_package_crc32);
    TEST_ASSERT_EQUAL_UINT32(expected.commit_marker, actual.commit_marker);
}

void test_upgrade_meta_uses_fixed_little_endian_layout(void)
{
    upgrade_meta_t meta = make_valid_upgrade_meta();
    uint8_t buffer[UPGRADE_META_SIZE];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OK,
                      upgrade_meta_encode(&meta,
                                          buffer,
                                          sizeof(buffer)));

    TEST_ASSERT_EQUAL_UINT8(0x54U, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0x44U, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0x4DU, buffer[2]);
    TEST_ASSERT_EQUAL_UINT8(0x55U, buffer[3]);
    TEST_ASSERT_EQUAL_UINT8(0x07U, buffer[8]);
    TEST_ASSERT_EQUAL_UINT8(FW_STATE_TRIAL_PENDING, buffer[12]);
    TEST_ASSERT_EQUAL_UINT8(INSTALL_APP_VALID, buffer[13]);
    TEST_ASSERT_EQUAL_UINT8(UPGRADE_SOURCE_ONLINE, buffer[15]);
}

void test_upgrade_meta_rejects_invalid_arguments(void)
{
    upgrade_meta_t meta = make_valid_upgrade_meta();
    upgrade_meta_t decoded;
    uint8_t buffer[UPGRADE_META_SIZE];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_ARGUMENT,
                      upgrade_meta_encode(NULL,
                                          buffer,
                                          sizeof(buffer)));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_ARGUMENT,
                      upgrade_meta_encode(&meta,
                                          NULL,
                                          sizeof(buffer)));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_ARGUMENT,
                      upgrade_meta_decode(NULL,
                                          sizeof(buffer),
                                          &decoded));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_ARGUMENT,
                      upgrade_meta_decode(buffer,
                                          sizeof(buffer),
                                          NULL));
}

void test_upgrade_meta_rejects_small_buffer(void)
{
    upgrade_meta_t meta = make_valid_upgrade_meta();
    uint8_t buffer[UPGRADE_META_SIZE - 1U];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OUTPUT_TOO_SMALL,
                      upgrade_meta_encode(&meta,
                                          buffer,
                                          sizeof(buffer)));
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OUTPUT_TOO_SMALL,
                      upgrade_meta_decode(buffer,
                                          sizeof(buffer),
                                          &meta));
}

void test_upgrade_meta_rejects_invalid_fields(void)
{
    upgrade_meta_t meta = make_valid_upgrade_meta();
    uint8_t buffer[UPGRADE_META_SIZE];

    meta.magic ^= 1UL;
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_FIELD,
                      upgrade_meta_encode(&meta,
                                          buffer,
                                          sizeof(buffer)));

    meta = make_valid_upgrade_meta();
    meta.state = 8U;
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_FIELD,
                      upgrade_meta_encode(&meta,
                                          buffer,
                                          sizeof(buffer)));

    meta = make_valid_upgrade_meta();
    meta.install_stage = 5U;
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_FIELD,
                      upgrade_meta_encode(&meta,
                                          buffer,
                                          sizeof(buffer)));

    meta = make_valid_upgrade_meta();
    meta.upgrade_source = 3U;
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_FIELD,
                      upgrade_meta_encode(&meta,
                                          buffer,
                                          sizeof(buffer)));

    meta = make_valid_upgrade_meta();
    meta.commit_marker = 0U;
    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_FIELD,
                      upgrade_meta_encode(&meta,
                                          buffer,
                                          sizeof(buffer)));
}

void test_upgrade_meta_rejects_crc_error(void)
{
    upgrade_meta_t meta = make_valid_upgrade_meta();
    upgrade_meta_t decoded;
    uint8_t buffer[UPGRADE_META_SIZE];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OK,
                      upgrade_meta_encode(&meta,
                                          buffer,
                                          sizeof(buffer)));
    buffer[40] ^= 0x01U;

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_CRC_ERROR,
                      upgrade_meta_decode(buffer,
                                          sizeof(buffer),
                                          &decoded));
}

void test_upgrade_meta_rejects_invalid_commit_marker(void)
{
    upgrade_meta_t meta = make_valid_upgrade_meta();
    upgrade_meta_t decoded;
    uint8_t buffer[UPGRADE_META_SIZE];

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_OK,
                      upgrade_meta_encode(&meta,
                                          buffer,
                                          sizeof(buffer)));
    buffer[64] ^= 0x01U;

    TEST_ASSERT_EQUAL(FW_FORMAT_STATUS_INVALID_FIELD,
                      upgrade_meta_decode(buffer,
                                          sizeof(buffer),
                                          &decoded));
}
