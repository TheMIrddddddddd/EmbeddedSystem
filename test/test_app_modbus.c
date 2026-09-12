#include "unity.h"
#include "app_modbus.h"
#include "app_config.h"
#include "sample_task.h"
#include <string.h>

extern int app_config_sample_period_key_set(uint16_t key_id);

static sample_snapshot_t snapshot;
static float ratios[2];
static unsigned depth, snapshot_reads;
static int snapshot_ok;
void test_critical_enter(void) { ++depth; }
void test_critical_exit(void) { TEST_ASSERT_TRUE(depth > 0); --depth; }
int storage_task_audit_event_submit(uint8_t event, uint8_t channel,
                                    float value0, float value1,
                                    uint32_t argument)
{
    (void)event; (void)channel; (void)value0; (void)value1; (void)argument;
    return 1;
}
int sample_task_snapshot_get(sample_snapshot_t *out)
{
    ++snapshot_reads;
    if (!snapshot_ok) return 0;
    *out = snapshot;
    return 1;
}
int sample_task_ratio_set(uint8_t ch, float value)
{
    TEST_ASSERT_TRUE(depth > 0); /* Batch publication holds outer critical section. */
    if (ch >= 2 || !(value >= 0 && value <= 100)) return 0;
    ratios[ch] = value;
    return 1;
}
void setUp(void)
{
    depth = snapshot_reads = 0;
    snapshot_ok = 1;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.sequence = 1;
    snapshot.value_ch0 = 1.0f;
    snapshot.value_ch1 = 2.0f;
    ratios[0] = ratios[1] = 1.0f;
    app_config_init();
}
void tearDown(void) { TEST_ASSERT_EQUAL_UINT32(0, depth); }

static modbus_request_t req(uint8_t function, uint16_t start, uint16_t count)
{
    modbus_request_t r = {0};
    r.address = 1; r.function = function; r.start_address = start; r.quantity = count;
    return r;
}
static app_modbus_result_t run(modbus_request_t *r)
{
    app_modbus_result_t out;
    memset(&out, 0xA5, sizeof(out));
    app_modbus_execute(r, &out);
    return out;
}
static void unchanged(void)
{
    app_config_t c;
    app_config_get(&c);
    TEST_ASSERT_EQUAL_FLOAT(1, c.ratio[0]);
    TEST_ASSERT_EQUAL_FLOAT(1, c.ratio[1]);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, c.limit[0]);
    TEST_ASSERT_EQUAL_FLOAT(10.5f, c.limit[1]);
    TEST_ASSERT_EQUAL_FLOAT(1, ratios[0]);
    TEST_ASSERT_EQUAL_FLOAT(1, ratios[1]);
    TEST_ASSERT_EQUAL_UINT16(1, c.device_id);
    TEST_ASSERT_EQUAL_UINT32(115200, c.rs485_baudrate);
}
static void test_holding_all(void)
{
    const uint8_t expected[] = {16,0x3F,0x80,0,0,0x3F,0x80,0,0,0x40,0x20,0,0,0x41,0x28,0,0};
    modbus_request_t r = req(3,0,8);
    app_modbus_result_t out = run(&r);
    TEST_ASSERT_EQUAL_UINT8(0,out.exception);
    TEST_ASSERT_EQUAL_UINT8(17,out.data_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected,out.data,17);
}
static void test_single_float_word(void)
{
    modbus_request_t r = req(3,1,1);
    app_modbus_result_t out = run(&r);
    const uint8_t expected[] = {2,0,0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected,out.data,3);
}
static void test_communication_read(void)
{
    modbus_request_t r = req(3,0x10,2);
    app_modbus_result_t out = run(&r);
    const uint8_t expected[] = {4,0,1,0,4};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected,out.data,5);
}
static void test_input_snapshot(void)
{
    modbus_request_t r = req(4,0,4);
    app_modbus_result_t out = run(&r);
    const uint8_t expected[] = {8,0x3F,0x80,0,0,0x40,0,0,0};
    TEST_ASSERT_EQUAL_UINT8(0,out.exception);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected,out.data,9);
    TEST_ASSERT_EQUAL_UINT32(1,snapshot_reads);
}
static void test_read_ranges(void)
{
    const uint16_t starts[] = {8,7,0xFFFF,0x12};
    const uint16_t counts[] = {1,2,2,1};
    unsigned i;
    for(i=0;i<4;i++) {
        modbus_request_t r=req(3,starts[i],counts[i]);
        app_modbus_result_t out=run(&r);
        TEST_ASSERT_EQUAL_UINT8(2,out.exception);
        TEST_ASSERT_EQUAL_UINT8(0,out.data_length);
    }
    { modbus_request_t r=req(4,3,2); app_modbus_result_t out=run(&r);
      TEST_ASSERT_EQUAL_UINT8(2,out.exception); }
}
static void test_read_busy_and_quantity(void)
{
    modbus_request_t r=req(4,0,2);
    app_modbus_result_t out;
    snapshot.sequence=0; out=run(&r); TEST_ASSERT_EQUAL_UINT8(6,out.exception);
    snapshot.sequence=1; snapshot_ok=0; out=run(&r); TEST_ASSERT_EQUAL_UINT8(6,out.exception);
    r.quantity=0; out=run(&r); TEST_ASSERT_EQUAL_UINT8(3,out.exception);
    r.quantity=126; out=run(&r); TEST_ASSERT_EQUAL_UINT8(3,out.exception);
}
static void test_write_all_parameters(void)
{
    const uint8_t values[]={0x40,0,0,0, 0x40,0x40,0,0, 0x40,0x80,0,0, 0x40,0xA0,0,0};
    const uint8_t ack[]={0,0,0,8};
    modbus_request_t r=req(16,0,8);
    app_modbus_result_t out;
    app_config_t c;
    r.write_data=values; r.write_byte_count=16; out=run(&r);
    TEST_ASSERT_EQUAL_UINT8(0,out.exception);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ack,out.data,4);
    app_config_get(&c);
    TEST_ASSERT_EQUAL_FLOAT(2,c.ratio[0]); TEST_ASSERT_EQUAL_FLOAT(3,c.ratio[1]);
    TEST_ASSERT_EQUAL_FLOAT(4,c.limit[0]); TEST_ASSERT_EQUAL_FLOAT(5,c.limit[1]);
    TEST_ASSERT_EQUAL_FLOAT(2,ratios[0]); TEST_ASSERT_EQUAL_FLOAT(3,ratios[1]);
}
static void test_write_invalid_batch_atomic(void)
{
    /* Last value 501 invalid: first three parameters must remain unchanged. */
    const uint8_t values[]={0x40,0,0,0, 0x40,0x40,0,0, 0x40,0x80,0,0, 0x43,0xFA,0x80,0};
    modbus_request_t r=req(16,0,8);
    app_modbus_result_t out;
    r.write_data=values; r.write_byte_count=16; out=run(&r);
    TEST_ASSERT_EQUAL_UINT8(3,out.exception);
    unchanged();
}
static void test_nan_inf_and_ratio_bounds(void)
{
    const uint8_t values[][4]={{0x7F,0xC0,0,0},{0x7F,0x80,0,0},{0xFF,0x80,0,0},{0x42,0xCA,0,0},{0xBF,0x80,0,0}};
    unsigned i;
    for(i=0;i<5;i++) {
        modbus_request_t r=req(16,0,2); app_modbus_result_t out;
        r.write_data=values[i]; r.write_byte_count=4; out=run(&r);
        TEST_ASSERT_EQUAL_UINT8(3,out.exception); unchanged();
    }
}
static void test_half_word_and_holes(void)
{
    uint8_t data[16]={0};
    const uint16_t start[]={0,1,0,6,0xFFFF};
    const uint16_t count[]={1,2,1,4,2};
    unsigned i;
    for(i=0;i<5;i++) {
        modbus_request_t r=req(i==0?6:16,start[i],count[i]); app_modbus_result_t out;
        r.write_data=data; r.write_byte_count=(uint8_t)(count[i]*2); out=run(&r);
        TEST_ASSERT_EQUAL_UINT8(2,out.exception); unchanged();
    }
}
static void test_deferred_communication(void)
{
    modbus_request_t r=req(6,0x10,1);
    app_modbus_result_t out;
    r.value=8; out=run(&r);
    TEST_ASSERT_EQUAL_UINT8(1,out.apply_flags);
    TEST_ASSERT_EQUAL_UINT16(8,out.next_device_id);
    TEST_ASSERT_EQUAL_UINT8(1,out.address);
    unchanged();
    r.start_address=0x11; r.value=0; out=run(&r);
    TEST_ASSERT_EQUAL_UINT8(2,out.apply_flags);
    TEST_ASSERT_EQUAL_UINT32(9600,out.next_baudrate);
    unchanged();
}
static void test_communication_batch_and_failure(void)
{
    uint8_t data[]={0,8,0,2};
    modbus_request_t r=req(16,0x10,2);
    app_modbus_result_t out;
    r.write_data=data; r.write_byte_count=4; out=run(&r);
    TEST_ASSERT_EQUAL_UINT8(3,out.apply_flags);
    TEST_ASSERT_EQUAL_UINT16(8,out.next_device_id);
    TEST_ASSERT_EQUAL_UINT32(38400,out.next_baudrate); unchanged();
    data[3]=5; out=run(&r);
    TEST_ASSERT_EQUAL_UINT8(3,out.exception);
    TEST_ASSERT_EQUAL_UINT8(0,out.apply_flags);
    TEST_ASSERT_EQUAL_UINT16(0,out.next_device_id);
    TEST_ASSERT_EQUAL_UINT32(0,out.next_baudrate); unchanged();
}
static void test_broadcast_policy(void)
{
    const uint8_t data[]={0x40,0,0,0};
    modbus_request_t r=req(16,0,2);
    app_modbus_result_t out;
    app_config_t c;
    r.address=0; r.write_data=data; r.write_byte_count=4; out=run(&r);
    TEST_ASSERT_EQUAL_UINT8(0,out.reply_required);
    app_config_get(&c); TEST_ASSERT_EQUAL_FLOAT(2,c.ratio[0]);
    r.function=6; r.start_address=0x10; r.quantity=1; r.value=8; out=run(&r);
    TEST_ASSERT_EQUAL_UINT8(0,out.reply_required);
    TEST_ASSERT_EQUAL_UINT8(0,out.apply_flags);
    app_config_get(&c); TEST_ASSERT_EQUAL_UINT16(1,c.device_id);
    r.function=3; out=run(&r); TEST_ASSERT_EQUAL_UINT8(0,out.reply_required);
}
static void test_routing_and_null(void)
{
    modbus_request_t r=req(3,0,2);
    app_modbus_result_t out;
    r.address=2; out=run(&r); TEST_ASSERT_EQUAL_UINT8(0,out.reply_required);
    r.address=1; r.exception=3; out=run(&r); TEST_ASSERT_EQUAL_UINT8(3,out.exception);
    r.exception=0; r.function=0x55; out=run(&r); TEST_ASSERT_EQUAL_UINT8(1,out.exception);
    app_modbus_execute(&r,NULL);
    app_modbus_execute(NULL,&out); TEST_ASSERT_EQUAL_UINT8(0,out.reply_required);
    app_config_device_id_set(248); out=run(&r); TEST_ASSERT_EQUAL_UINT8(0,out.reply_required);
}
static void test_modbus_mode_id_guard(void)
{
    app_config_t c;

    TEST_ASSERT_EQUAL_INT(1, app_config_protocol_mode_set(1));
    TEST_ASSERT_EQUAL_INT(0, app_config_device_id_set(248));
    app_config_get(&c);
    TEST_ASSERT_EQUAL_UINT16(1, c.device_id);

    TEST_ASSERT_EQUAL_INT(1, app_config_protocol_mode_set(0));
    TEST_ASSERT_EQUAL_INT(1, app_config_device_id_set(248));
    TEST_ASSERT_EQUAL_INT(0, app_config_protocol_mode_set(1));
}

static void test_sample_period_key_mapping(void)
{
    app_config_t c;

    TEST_ASSERT_EQUAL_INT(1, app_config_sample_period_key_set(2U));
    app_config_get(&c);
    TEST_ASSERT_EQUAL_UINT8(5U, c.sample_period_s);

    TEST_ASSERT_EQUAL_INT(1, app_config_sample_period_key_set(3U));
    app_config_get(&c);
    TEST_ASSERT_EQUAL_UINT8(10U, c.sample_period_s);

    TEST_ASSERT_EQUAL_INT(1, app_config_sample_period_key_set(4U));
    app_config_get(&c);
    TEST_ASSERT_EQUAL_UINT8(15U, c.sample_period_s);

    TEST_ASSERT_EQUAL_INT(0, app_config_sample_period_key_set(1U));
    TEST_ASSERT_EQUAL_INT(0, app_config_sample_period_key_set(5U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_holding_all);
    RUN_TEST(test_single_float_word);
    RUN_TEST(test_communication_read);
    RUN_TEST(test_input_snapshot);
    RUN_TEST(test_read_ranges);
    RUN_TEST(test_read_busy_and_quantity);
    RUN_TEST(test_write_all_parameters);
    RUN_TEST(test_write_invalid_batch_atomic);
    RUN_TEST(test_nan_inf_and_ratio_bounds);
    RUN_TEST(test_half_word_and_holes);
    RUN_TEST(test_deferred_communication);
    RUN_TEST(test_communication_batch_and_failure);
    RUN_TEST(test_broadcast_policy);
    RUN_TEST(test_routing_and_null);
    RUN_TEST(test_modbus_mode_id_guard);
    RUN_TEST(test_sample_period_key_mapping);
    return UNITY_END();
}
