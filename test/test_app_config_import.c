#include <string.h>
#include "unity.h"
#include "app_config_import.h"
void test_critical_enter(void) {}
void test_critical_exit(void) {}
static void base_config(app_config_t *c) { TEST_ASSERT_EQUAL_INT(1, app_config_defaults(c)); c->rs485_baudrate=57600U; c->local_sample_enabled=1U; c->hide_mode=1U; }
static const char valid_file[] = "device_id=0002\nsample_period=10\nprotocol_mode=0\nalarm_mode=1\nch0_ratio=2.5\nch1_ratio=3.5\nch0_limit=250\nch1_limit=499.5\n";
/* 验证完整导入、31 字节序列化、decode 回读及快照字段继承。 */
static void test_round_trip(void)
{ app_config_t b,c; uint8_t e[31]; uint16_t n=0,l=0; base_config(&b); c=b; TEST_ASSERT_EQUAL_INT(1,app_config_import_prepare(valid_file,sizeof(valid_file)-1U,&b,&c,e,sizeof(e),&n,&l)); TEST_ASSERT_EQUAL_UINT16(31U,n); TEST_ASSERT_EQUAL_UINT16(2U,c.device_id); TEST_ASSERT_EQUAL_FLOAT(2.5f,c.ratio[0]); TEST_ASSERT_EQUAL_FLOAT(499.5f,c.limit[1]); TEST_ASSERT_EQUAL_UINT32(57600U,c.rs485_baudrate); TEST_ASSERT_EQUAL_UINT8(1U,c.local_sample_enabled); }
/* 验证解析失败时三个输出均不改变。 */
static void test_failure_atomic(void)
{ const char f[]="device_id=0002\nsample_period=10\nprotocol_mode=0\n"; app_config_t b,c,old; uint8_t e[31],old_e[31]; uint16_t n=19,l=0,i; base_config(&b); c=b; old=c; for(i=0;i<31;i++) e[i]=(uint8_t)i; memcpy(old_e,e,31); TEST_ASSERT_EQUAL_INT(0,app_config_import_prepare(f,sizeof(f)-1U,&b,&c,e,31,&n,&l)); TEST_ASSERT_EQUAL_MEMORY(&old,&c,sizeof(c)); TEST_ASSERT_EQUAL_MEMORY(old_e,e,31); TEST_ASSERT_EQUAL_UINT16(19U,n); }
/* 验证容量和空指针参数拒绝。 */
static void test_arguments(void)
{ app_config_t b,c; uint8_t e[31]; uint16_t n=7,l=9; base_config(&b); c=b; TEST_ASSERT_EQUAL_INT(0,app_config_import_prepare(valid_file,sizeof(valid_file)-1U,&b,&c,e,30,&n,&l)); TEST_ASSERT_EQUAL_UINT16(7U,n); TEST_ASSERT_EQUAL_UINT16(9U,l); }
void setUp(void) {} void tearDown(void) {}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_round_trip); RUN_TEST(test_failure_atomic); RUN_TEST(test_arguments); return UNITY_END(); }
