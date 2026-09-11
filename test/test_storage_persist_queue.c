#include <string.h>

#include "unity.h"
#include "storage_persist_queue.h"

struct test_queue
{
    size_t length;
    size_t item_size;
    uint8_t *storage;
    size_t count;
    size_t read_index;
    size_t write_index;
};

static struct test_queue s_queues[2];
static size_t s_queue_count;
static uint8_t s_initialized;

QueueHandle_t xQueueCreateStatic(size_t length,
                                  size_t item_size,
                                  uint8_t *storage,
                                  StaticQueue_t *queue)
{
    struct test_queue *out;

    (void)queue;

    if (s_queue_count >= 2U)
    {
        return NULL;
    }

    out = &s_queues[s_queue_count++];
    out->length = length;
    out->item_size = item_size;
    out->storage = storage;
    out->count = 0U;
    out->read_index = 0U;
    out->write_index = 0U;
    return out;
}

BaseType_t xQueueSend(QueueHandle_t queue,
                      const void *item,
                      TickType_t wait_ticks)
{
    (void)wait_ticks;

    if ((queue == NULL) ||
        (item == NULL) ||
        (queue->count >= queue->length))
    {
        return errQUEUE_FULL;
    }

    (void)memcpy(&queue->storage[queue->write_index * queue->item_size],
                 item,
                 queue->item_size);
    queue->write_index = (queue->write_index + 1U) % queue->length;
    queue->count++;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue,
                         void *item,
                         TickType_t wait_ticks)
{
    (void)wait_ticks;

    if ((queue == NULL) ||
        (item == NULL) ||
        (queue->count == 0U))
    {
        return pdFALSE;
    }

    (void)memcpy(item,
                 &queue->storage[queue->read_index * queue->item_size],
                 queue->item_size);
    queue->read_index = (queue->read_index + 1U) % queue->length;
    queue->count--;
    return pdTRUE;
}

static void storage_persist_queue_ensure_initialized(void)
{
    if (s_initialized == 0U)
    {
        TEST_ASSERT_EQUAL_INT(1, storage_persist_queue_init());
        s_initialized = 1U;
    }
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_queue_initializes_once(void)
{
    TEST_ASSERT_EQUAL_INT(1, storage_persist_queue_init());
    s_initialized = 1U;
}

static void test_request_queue_copies_inline_payload(void)
{
    storage_task_persist_request_t request;
    storage_task_persist_request_t received;
    uint16_t index;

    storage_persist_queue_ensure_initialized();

    (void)memset(&request, 0, sizeof(request));
    request.request_id = 42U;
    request.deadline_tick = 1000U;
    request.operation = STORAGE_TASK_PERSIST_CONFIG_SAVE;
    request.origin = STORAGE_TASK_PERSIST_ORIGIN_CONTROL;
    request.payload_length = 31U;

    for (index = 0U; index < request.payload_length; index++)
    {
        request.payload[index] = (uint8_t)(index + 1U);
    }

    TEST_ASSERT_EQUAL_INT(pdTRUE,
                          storage_persist_request_send(&request));

    (void)memset(&received, 0xA5, sizeof(received));
    TEST_ASSERT_EQUAL_INT(pdTRUE,
                          storage_persist_request_receive(&received, 0U));
    TEST_ASSERT_EQUAL_UINT32(request.request_id, received.request_id);
    TEST_ASSERT_EQUAL_UINT32(request.deadline_tick, received.deadline_tick);
    TEST_ASSERT_EQUAL_UINT8(request.operation, received.operation);
    TEST_ASSERT_EQUAL_UINT8(request.origin, received.origin);
    TEST_ASSERT_EQUAL_UINT16(request.payload_length,
                             received.payload_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(request.payload,
                                  received.payload,
                                  request.payload_length);
}

static void test_result_queue_copies_inline_payload(void)
{
    storage_task_persist_result_t result;
    storage_task_persist_result_t received;

    storage_persist_queue_ensure_initialized();

    (void)memset(&result, 0, sizeof(result));
    result.request_id = 43U;
    result.operation = STORAGE_TASK_PERSIST_CONFIG_READ;
    result.status = STORAGE_TASK_PERSIST_STATUS_OK;
    result.payload_length = 3U;
    result.payload[0] = 0x11U;
    result.payload[1] = 0x22U;
    result.payload[2] = 0x33U;

    TEST_ASSERT_EQUAL_INT(pdTRUE,
                          storage_persist_result_send(&result));

    (void)memset(&received, 0xA5, sizeof(received));
    TEST_ASSERT_EQUAL_INT(pdTRUE,
                          storage_persist_result_receive(&received, 0U));
    TEST_ASSERT_EQUAL_UINT32(result.request_id, received.request_id);
    TEST_ASSERT_EQUAL_UINT8(result.operation, received.operation);
    TEST_ASSERT_EQUAL_UINT8(result.status, received.status);
    TEST_ASSERT_EQUAL_UINT16(result.payload_length,
                             received.payload_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(result.payload,
                                  received.payload,
                                  result.payload_length);
}

static void test_request_queue_reports_full_without_blocking(void)
{
    storage_task_persist_request_t request;
    storage_task_persist_request_t received;
    uint8_t index;

    storage_persist_queue_ensure_initialized();

    (void)memset(&request, 0, sizeof(request));
    request.operation = STORAGE_TASK_PERSIST_CONFIG_READ;

    for (index = 0U; index < STORAGE_PERSIST_QUEUE_LENGTH; index++)
    {
        request.request_id = (uint32_t)index;
        TEST_ASSERT_EQUAL_INT(pdTRUE,
                              storage_persist_request_send(&request));
    }

    request.request_id = 99U;
    TEST_ASSERT_EQUAL_INT(errQUEUE_FULL,
                          storage_persist_request_send(&request));

    for (index = 0U; index < STORAGE_PERSIST_QUEUE_LENGTH; index++)
    {
        TEST_ASSERT_EQUAL_INT(pdTRUE,
                              storage_persist_request_receive(&received, 0U));
    }
    TEST_ASSERT_EQUAL_INT(pdFALSE,
                          storage_persist_request_receive(&received, 0U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_queue_initializes_once);
    RUN_TEST(test_request_queue_copies_inline_payload);
    RUN_TEST(test_result_queue_copies_inline_payload);
    RUN_TEST(test_request_queue_reports_full_without_blocking);
    return UNITY_END();
}
