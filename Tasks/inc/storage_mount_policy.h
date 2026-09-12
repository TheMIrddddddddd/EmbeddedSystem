#ifndef STORAGE_MOUNT_POLICY_H
#define STORAGE_MOUNT_POLICY_H

#include <stdint.h>

#define STORAGE_FATFS_FULL_THRESHOLD_PERCENT 5U

uint8_t storage_mount_policy_retry(uint8_t card_present,
                                   uint8_t mounted,
                                   uint8_t attempted);

uint32_t storage_mount_policy_write_retry_delay_ms(uint8_t retry_index);
uint8_t storage_mount_policy_write_retry_exhausted(uint8_t retry_count);
uint8_t storage_mount_policy_space_full(uint32_t free_clusters,
                                        uint32_t total_clusters);

#endif
