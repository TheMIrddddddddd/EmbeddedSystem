#ifndef STORAGE_MOUNT_POLICY_H
#define STORAGE_MOUNT_POLICY_H

#include <stdint.h>

uint8_t storage_mount_policy_retry(uint8_t card_present,
                                   uint8_t mounted,
                                   uint8_t attempted);

#endif
