#ifndef APP_CONFIG_IMPORT_H
#define APP_CONFIG_IMPORT_H
#include <stdint.h>
#include "app_config.h"
#define APP_CONFIG_IMPORT_BUFFER_SIZE APP_CONFIG_SERIALIZED_SIZE
/* 纯内存导入：解析、序列化并 decode 回读校验；成功才写输出。 */
int app_config_import_prepare(const char *file, uint16_t file_length,
                              const app_config_t *base, app_config_t *candidate,
                              uint8_t *encoded, uint16_t capacity,
                              uint16_t *encoded_length, uint16_t *error_line);
#endif
