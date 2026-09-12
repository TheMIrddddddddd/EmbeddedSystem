#include <math.h>

#include "unity.h"
#include "alarm_logic.h"

/* Checks that a channel context contains the expected state and counter. */
static void assert_channel(const alarm_logic_channel_t *channel,
                           alarm_logic_state_t state,
                           uint8_t consecutive_over_limit)
{
    TEST_ASSERT_EQUAL_INT(state, channel->state);
    TEST_ASSERT_EQUAL_UINT8(consecutive_over_limit,
                            channel->consecutive_over_limit);
}

/* Initializes a channel before each state-machine example. */
void setUp(void)
{
}

/* Releases no resources because the state machine owns no dynamic data. */
void tearDown(void)
{
}

/* Verifies initialization starts in NORMAL with no pending over-limit count. */
static void test_init_starts_normal(void)
{
    alarm_logic_channel_t channel =
    {
        ALARM_LOGIC_STATE_ACTIVE,
        2U
    };

    TEST_ASSERT_EQUAL_INT(1, alarm_logic_init(&channel));
    assert_channel(&channel, ALARM_LOGIC_STATE_NORMAL, 0U);
}

/* Verifies only three consecutive strict over-limit samples trigger an alarm. */
static void test_three_consecutive_over_limit_samples_trigger_once(void)
{
    alarm_logic_channel_t channel;
    alarm_logic_event_t event = ALARM_LOGIC_EVENT_NONE;

    TEST_ASSERT_EQUAL_INT(1, alarm_logic_init(&channel));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_NO_EVENT,
                          alarm_logic_update(&channel, 2.51f, 2.50f, &event));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_EVENT_NONE, event);
    assert_channel(&channel, ALARM_LOGIC_STATE_NORMAL, 1U);

    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_NO_EVENT,
                          alarm_logic_update(&channel, 2.60f, 2.50f, &event));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_EVENT_NONE, event);
    assert_channel(&channel, ALARM_LOGIC_STATE_NORMAL, 2U);

    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_EVENT,
                          alarm_logic_update(&channel, 2.70f, 2.50f, &event));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_EVENT_TRIGGERED, event);
    assert_channel(&channel, ALARM_LOGIC_STATE_ACTIVE,
                   ALARM_LOGIC_TRIGGER_COUNT);

    event = ALARM_LOGIC_EVENT_TRIGGERED;
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_NO_EVENT,
                          alarm_logic_update(&channel, 2.80f, 2.50f, &event));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_EVENT_TRIGGERED, event);
    assert_channel(&channel, ALARM_LOGIC_STATE_ACTIVE,
                   ALARM_LOGIC_TRIGGER_COUNT);
}

/* Verifies an in-limit sample clears the pending consecutive count. */
static void test_in_limit_sample_breaks_consecutive_trigger_sequence(void)
{
    alarm_logic_channel_t channel;
    alarm_logic_event_t event = ALARM_LOGIC_EVENT_NONE;

    TEST_ASSERT_EQUAL_INT(1, alarm_logic_init(&channel));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_NO_EVENT,
                          alarm_logic_update(&channel, 2.51f, 2.50f, &event));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_NO_EVENT,
                          alarm_logic_update(&channel, 2.50f, 2.50f, &event));
    assert_channel(&channel, ALARM_LOGIC_STATE_NORMAL, 0U);

    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_NO_EVENT,
                          alarm_logic_update(&channel, 2.51f, 2.50f, &event));
    assert_channel(&channel, ALARM_LOGIC_STATE_NORMAL, 1U);
}

/* Verifies recovery requires a strict value below the hysteresis boundary. */
static void test_active_recovers_only_below_limit_minus_hysteresis(void)
{
    alarm_logic_channel_t channel;
    alarm_logic_event_t event = ALARM_LOGIC_EVENT_NONE;

    TEST_ASSERT_EQUAL_INT(1, alarm_logic_init(&channel));
    (void)alarm_logic_update(&channel, 2.51f, 2.50f, &event);
    (void)alarm_logic_update(&channel, 2.60f, 2.50f, &event);
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_EVENT,
                          alarm_logic_update(&channel, 2.70f, 2.50f, &event));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_EVENT_TRIGGERED, event);

    event = ALARM_LOGIC_EVENT_TRIGGERED;
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_NO_EVENT,
                          alarm_logic_update(&channel, 2.45f, 2.50f, &event));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_EVENT_TRIGGERED, event);
    assert_channel(&channel, ALARM_LOGIC_STATE_ACTIVE,
                   ALARM_LOGIC_TRIGGER_COUNT);

    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_NO_EVENT,
                          alarm_logic_update(&channel, 2.45f, 2.50f, &event));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_EVENT_TRIGGERED, event);
    assert_channel(&channel, ALARM_LOGIC_STATE_ACTIVE,
                   ALARM_LOGIC_TRIGGER_COUNT);

    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_EVENT,
                          alarm_logic_update(&channel, 2.44f, 2.50f, &event));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_EVENT_RECOVERED, event);
    assert_channel(&channel, ALARM_LOGIC_STATE_RECOVERED, 0U);
}

/* Verifies RECOVERED is a one-update transition before returning to NORMAL. */
static void test_recovered_state_returns_to_normal_before_rearming(void)
{
    alarm_logic_channel_t channel;
    alarm_logic_event_t event = ALARM_LOGIC_EVENT_NONE;

    TEST_ASSERT_EQUAL_INT(1, alarm_logic_init(&channel));
    (void)alarm_logic_update(&channel, 2.51f, 2.50f, &event);
    (void)alarm_logic_update(&channel, 2.60f, 2.50f, &event);
    (void)alarm_logic_update(&channel, 2.70f, 2.50f, &event);
    (void)alarm_logic_update(&channel, 2.44f, 2.50f, &event);
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATE_RECOVERED, channel.state);

    event = ALARM_LOGIC_EVENT_RECOVERED;
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_NO_EVENT,
                          alarm_logic_update(&channel, 2.51f, 2.50f, &event));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_EVENT_RECOVERED, event);
    assert_channel(&channel, ALARM_LOGIC_STATE_NORMAL, 1U);
}

/* Verifies NaN or infinity cannot mutate state or an existing event output. */
static void test_invalid_nan_and_infinity_are_safe(void)
{
    alarm_logic_channel_t channel;
    alarm_logic_channel_t before;
    alarm_logic_event_t event = ALARM_LOGIC_EVENT_TRIGGERED;

    TEST_ASSERT_EQUAL_INT(1, alarm_logic_init(&channel));
    (void)alarm_logic_update(&channel, 2.51f, 2.50f, &event);
    before = channel;

    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_ERROR,
                          alarm_logic_update(&channel, NAN, 2.50f, &event));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_EVENT_TRIGGERED, event);
    TEST_ASSERT_EQUAL_INT(before.state, channel.state);
    TEST_ASSERT_EQUAL_UINT8(before.consecutive_over_limit,
                            channel.consecutive_over_limit);

    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_ERROR,
                          alarm_logic_update(&channel, 2.51f, INFINITY, &event));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_EVENT_TRIGGERED, event);
    TEST_ASSERT_EQUAL_INT(before.state, channel.state);
    TEST_ASSERT_EQUAL_UINT8(before.consecutive_over_limit,
                            channel.consecutive_over_limit);
}

/* Verifies null arguments are rejected without changing an initialized channel. */
static void test_null_arguments_are_rejected_without_mutation(void)
{
    alarm_logic_channel_t channel;
    alarm_logic_channel_t before;
    alarm_logic_event_t event = ALARM_LOGIC_EVENT_RECOVERED;

    TEST_ASSERT_EQUAL_INT(0, alarm_logic_init(NULL));
    TEST_ASSERT_EQUAL_INT(1, alarm_logic_init(&channel));
    before = channel;

    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_ERROR,
                          alarm_logic_update(NULL, 1.0f, 1.0f, &event));
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_STATUS_ERROR,
                          alarm_logic_update(&channel, 1.0f, 1.0f, NULL));
    TEST_ASSERT_EQUAL_INT(before.state, channel.state);
    TEST_ASSERT_EQUAL_UINT8(before.consecutive_over_limit,
                            channel.consecutive_over_limit);
    TEST_ASSERT_EQUAL_INT(ALARM_LOGIC_EVENT_RECOVERED, event);
}

/* Runs the independent alarm-logic contract tests. */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_starts_normal);
    RUN_TEST(test_three_consecutive_over_limit_samples_trigger_once);
    RUN_TEST(test_in_limit_sample_breaks_consecutive_trigger_sequence);
    RUN_TEST(test_active_recovers_only_below_limit_minus_hysteresis);
    RUN_TEST(test_recovered_state_returns_to_normal_before_rearming);
    RUN_TEST(test_invalid_nan_and_infinity_are_safe);
    RUN_TEST(test_null_arguments_are_rejected_without_mutation);
    return UNITY_END();
}
