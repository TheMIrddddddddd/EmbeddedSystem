#ifndef BOOT_UPGRADE_BEGIN_H
#define BOOT_UPGRADE_BEGIN_H

#include <stdint.h>

#include "boot_upgrade_meta.h"

boot_upgrade_meta_write_status_t boot_upgrade_begin_accept(
    const uint8_t *header_raw,
    uint32_t header_length
);

#endif /* BOOT_UPGRADE_BEGIN_H */
