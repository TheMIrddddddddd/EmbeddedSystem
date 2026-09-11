#include <string.h>

#include "unity.h"
#include "task_queues.h"

struct test_queue
{
    size_t length;
    size_t item_size;
    uint8_t *storage;
    size_t count;
    size_t read_index;
    size_t write_index;
};

static struct test_queue s_queues[3];
static size_t s_queue_count;

void setUp(void)
{
}

void tearDown(void)
{
}

QueueHandle_t xQueueCreateStatic(size_t length,
                                 size_t item_size,
                                 uint8_t *storage,
                                 StaticQueue_t *queue)
{
    struct test_queue *out;

    (void)queue;
    if (s_queue_count >= 3U)
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
    if ((queue == NULL) || (item == NULL) || (queue->count >= queue->length))
    {
        return 0;
    }

    memcpy(&queue->storage[queue->write_index * queue->item_size],
           item, queue->item_size);
    queue->write_index = (queue->write_index + 1U) % queue->length;
    queue->count++;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue,
                         void *item,
                         TickType_t wait_ticks)
{
    (void)wait_ticks;
    if ((queue == NULL) || (item == NULL) || (queue->count == 0U))
    {
        return pdFALSE;
    }

    memcpy(item, &queue->storage[queue->read_index * queue->item_size],
           queue->item_size);
    queue->read_index = (queue->read_index + 1U) % queue->length;
    queue->count--;
    return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t queue)
{
    if (queue == NULL)
    {
        return pdFALSE;
    }

    queue->count = 0U;
    queue->read_index = 0U;
    queue->write_index = 0U;
    return pdTRUE;
}

static void test_queue_reset_drops_both_protocol_directions(void)
{
    protocol_request_t request = {0};
    protocol_result_t result = {0};
    protocol_request_t received_request;
    protocol_result_t received_result;

    TEST_ASSERT_EQUAL_INT(1, task_queues_init());
    TEST_ASSERT_EQUAL_INT(pdTRUE, protocol_request_send(&request));
    TEST_ASSERT_EQUAL_INT(pdTRUE, protocol_result_send(&result));

    protocol_queues_reset();

    TEST_ASSERT_EQUAL_INT(pdFALSE,
                          protocol_request_receive(&received_request, 0U));
    TEST_ASSERT_EQUAL_INT(pdFALSE,
                          protocol_result_receive(&received_result, 0U));

    TEST_ASSERT_EQUAL_INT(pdTRUE, protocol_request_send(&request));
    TEST_ASSERT_EQUAL_INT(pdTRUE, protocol_result_send(&result));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_queue_reset_drops_both_protocol_directions);
    return UNITY_END();
}
