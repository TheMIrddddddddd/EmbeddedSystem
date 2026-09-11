#include "app_modbus.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app_config.h"
#include "sample_task.h"

#define APP_MODBUS_PARAMETER_END        0x0008U
#define APP_MODBUS_INPUT_END            0x0004U

#define APP_MODBUS_REG_DEVICE_ID        0X0010U
#define APP_MODBUS_REG_BAUDRATE         0X0011U
#define APP_MODBUS_COMM_END             0X0012U

static const uint32_t s_modbus_baudrates[] =
{
    9600U,
    19200U,
    38400U,
    57600U,
    115200U
};

#define APP_MODBUS_BAUD_COUNT (sizeof(s_modbus_baudrates) / sizeof(s_modbus_baudrates[0]))

typedef char app_modbus_float_must_be_32_bits[(sizeof(float) == sizeof(uint32_t)) ? 1 : -1];

static uint16_t app_modbus_load_u16(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] << 8 | (uint16_t)src[1]);
}

static void app_modbus_store_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)value;
}

static float app_modbus_load_float(const uint8_t *src)
{
    uint32_t bits;
    float value;

    bits = ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) | ((uint32_t)src[2] << 8) | (uint32_t)src[3];

    (void)memcpy(&value, &bits, sizeof(value));

    return value;
}

static uint16_t app_modbus_float_word(float value, uint16_t word_index)
{
    uint32_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));

    if (word_index == 0U)
    {
        return (uint16_t)(bits >> 16);
    }

    return (uint16_t)bits;
}

static int app_modbus_baud_to_index(
    uint32_t baudrate,
    uint16_t *index)
{
    uint16_t i;

    for (i = 0U; i < APP_MODBUS_BAUD_COUNT; i++)
    {
        if (s_modbus_baudrates[i] == baudrate)
        {
            *index = i;
            return 1;
        }
    }

    return 0;
}

static uint8_t app_modbus_read_registers(const modbus_request_t *request, const app_config_t *config, app_modbus_result_t *result)
{
    sample_snapshot_t snapshot;
    uint32_t end;
    uint16_t i;
    uint16_t address;
    uint16_t word;
    uint16_t baud_index;
    float value;
    uint8_t input_registers;

    if ((request->quantity == 0U) || (request->quantity > 125U))
    {
        return MODBUS_EXCEPTION_VALUE;
    }

    end = (uint32_t)request->start_address + (uint32_t)request->quantity;

    input_registers = (request->function == MODBUS_FUNCTION_READ_INPUT) ? 1U : 0U;
    baud_index = 0U;

    if (input_registers != 0U)
    {
        if (end > APP_MODBUS_INPUT_END)
        {
            return MODBUS_EXCEPTION_ADDRESS;
        }
        if (sample_task_snapshot_get(&snapshot) == 0)
        {
            return MODBUS_EXCEPTION_BUSY;
        }
        if (snapshot.sequence == 0U)
        {
            return MODBUS_EXCEPTION_BUSY;
        }
    }
    else
    {
        if (!((end <= APP_MODBUS_PARAMETER_END) || ((request->start_address >= APP_MODBUS_REG_DEVICE_ID) && (end <= APP_MODBUS_COMM_END))))
        {
            return MODBUS_EXCEPTION_ADDRESS;
        }

        if ((request->start_address <= APP_MODBUS_REG_BAUDRATE) &&
            (end > APP_MODBUS_REG_BAUDRATE))
        {
            if (app_modbus_baud_to_index(
                    config->rs485_baudrate, &baud_index) == 0)
            {
                return MODBUS_EXCEPTION_BUSY;
            }
        }
    }

    if ((1U + (uint32_t)request->quantity * 2U) > sizeof(result->data))
    {
        return MODBUS_EXCEPTION_BUSY;
    }

    result->data[0] = (uint8_t)(request->quantity * 2U);

    for (i = 0U; i < request->quantity; i++)
    {
        address = (uint16_t)(request->start_address + i);

        if (input_registers != 0U)
        {
            value = (address < 2U) ? snapshot.value_ch0 : snapshot.value_ch1;
            word = app_modbus_float_word(value, (uint16_t)(address & 1U));
        }
        else if (address < APP_MODBUS_PARAMETER_END)
        {
            if (address < 4U)
            {
                value = config->ratio[address / 2U];
            }
            else
            {
                value = config->limit[(address - 4U) / 2U];
            }

            word = app_modbus_float_word(value, (uint16_t)(address & 1U));
        }
        else if (address == APP_MODBUS_REG_DEVICE_ID)
        {
            word = config->device_id;
        }
        else
        {
            word = baud_index;
        }

        app_modbus_store_u16(&result->data[1U + i * 2U], word);
    }
    result->data_length = (uint8_t)(1U + request->quantity * 2U);
    return MODBUS_EXCEPTION_NONE;
}

static uint8_t app_modbus_write_parameters(const modbus_request_t *request)
{
    float values[4];
    uint8_t parameter;
    uint16_t count;
    uint16_t i;
    float min_value;
    float max_value;

    if (((request->start_address & 1U) != 0U) || ((request->quantity & 1U) != 0U))
    {
        return MODBUS_EXCEPTION_ADDRESS;
    }

    parameter = request->start_address / 2U;
    count = request->quantity / 2U;

    for (i = 0U; i < count; i++)
    {
        values[i] = app_modbus_load_float(&request->write_data[i * 4U]);

        if ((parameter + i) < 2U)
        {
            min_value = APP_CONFIG_RATIO_MIN;
            max_value = APP_CONFIG_RATIO_MAX;
        }
        else
        {
            min_value = APP_CONFIG_LIMIT_MIN;
            max_value = APP_CONFIG_LIMIT_MAX;
        }

        /*
         * NaN 不满足大小比较；
         * 正负无穷也不在有限范围内。
         */
        if (!((values[i] >= min_value) && (values[i] <= max_value)))
        {
            return MODBUS_EXCEPTION_VALUE;
        }
    }

    taskENTER_CRITICAL();

    for (i = 0U; i < count; i++)
    {
        if ((parameter + i) < 2U)
        {
            uint8_t channel = (uint8_t)(parameter + i);

            (void)app_config_ratio_set(channel, values[i]);
            (void)sample_task_ratio_set(channel, values[i]);
        }
        else
        {
            uint8_t channel = (uint8_t)(parameter + i - 2U);

            (void)app_config_limit_set(channel, values[i]);
        }
    }

    taskEXIT_CRITICAL();

    return MODBUS_EXCEPTION_NONE;
}

static uint8_t app_modbus_write_registers(const modbus_request_t *request, app_modbus_result_t *result)
{
    uint32_t end;
    uint16_t i;
    uint16_t address;
    uint16_t value;
    uint8_t exception;

    if (request->function == MODBUS_FUNCTION_WRITE_SINGLE)
    {
        if (request->quantity != 1U)
        {
            return MODBUS_EXCEPTION_VALUE;
        }
    }
    else
    {
        if ((request->quantity == 0U) ||
            (request->quantity > 123U) ||
            (request->write_data == NULL) ||
            ((uint16_t)request->write_byte_count !=
             (uint16_t)(request->quantity * 2U)))
        {
            return MODBUS_EXCEPTION_VALUE;
        }
    }

    end = (uint32_t)request->start_address +
          (uint32_t)request->quantity;

    if (end <= APP_MODBUS_PARAMETER_END)
    {
        if (request->function != MODBUS_FUNCTION_WRITE_MULTIPLE)
        {
            return MODBUS_EXCEPTION_ADDRESS;
        }

        exception = app_modbus_write_parameters(request);

        if (exception != MODBUS_EXCEPTION_NONE)
        {
            return exception;
        }
    }
    else if ((request->start_address >= APP_MODBUS_REG_DEVICE_ID) &&
             (end <= APP_MODBUS_COMM_END))
    {
        /*
         * 这里只准备通信参数，等应答发送完成后应用。
         */
        for (i = 0U; i < request->quantity; i++)
        {
            address = (uint16_t)(request->start_address + i);

            if (request->function == MODBUS_FUNCTION_WRITE_SINGLE)
            {
                value = request->value;
            }
            else
            {
                value = app_modbus_load_u16(
                    &request->write_data[i * 2U]);
            }

            if (address == APP_MODBUS_REG_DEVICE_ID)
            {
                if ((value == 0U) ||
                    (value > MODBUS_RTU_MAX_SLAVE_ADDRESS))
                {
                    return MODBUS_EXCEPTION_VALUE;
                }

                result->next_device_id = value;
                result->apply_flags |= APP_MODBUS_APPLY_ID;
            }
            else
            {
                if (value >= APP_MODBUS_BAUD_COUNT)
                {
                    return MODBUS_EXCEPTION_VALUE;
                }

                result->next_baudrate = s_modbus_baudrates[value];
                result->apply_flags |= APP_MODBUS_APPLY_BAUD;
            }
        }
    }
    else
    {
        return MODBUS_EXCEPTION_ADDRESS;
    }

    /*
     * 06：回显寄存器地址和写入值。
     * 10：回显起始地址和写入数量。
     */
    app_modbus_store_u16(
        &result->data[0], request->start_address);

    if (request->function == MODBUS_FUNCTION_WRITE_SINGLE)
    {
        app_modbus_store_u16(&result->data[2], request->value);
    }
    else
    {
        app_modbus_store_u16(&result->data[2], request->quantity);
    }

    result->data_length = 4U;

    return MODBUS_EXCEPTION_NONE;
}

void app_modbus_execute(const modbus_request_t *request, app_modbus_result_t *result)
{
    app_config_t config;
    uint32_t end;
    uint8_t exception;

    if (result == NULL)
    {
        return;
    }

    (void)memset(result, 0, sizeof(*result));

    if (request == NULL)
    {
        return;
    }

    if (app_config_get(&config) == 0)
    {
        return;
    }

    /*
     * 非法本机配置下不提供 Modbus 服务。
     * 正常情况下在切换协议时就会拦截。
     */
    if ((config.device_id == 0U) ||
        (config.device_id > MODBUS_RTU_MAX_SLAVE_ADDRESS))
    {
        return;
    }

    if (request->address == 0U)
    {
        /*
         * 广播只允许参数区完整批量写。
         * 广播读、改 ID/波特率、非法请求均静默忽略。
         */
        end = (uint32_t)request->start_address +
              (uint32_t)request->quantity;

        if ((request->exception != MODBUS_EXCEPTION_NONE) ||
            (request->function != MODBUS_FUNCTION_WRITE_MULTIPLE) ||
            (request->quantity == 0U) ||
            (end > APP_MODBUS_PARAMETER_END))
        {
            return;
        }
    }
    else
    {
        if (request->address != config.device_id)
        {
            return;
        }

        result->reply_required = 1U;
    }

    result->address = request->address;
    result->function = request->function;

    if (request->exception != MODBUS_EXCEPTION_NONE)
    {
        result->exception = request->exception;
        return;
    }

    switch (request->function)
    {
    case MODBUS_FUNCTION_READ_HOLDING:
    case MODBUS_FUNCTION_READ_INPUT:
        exception = app_modbus_read_registers(request, &config, result);
        break;

    case MODBUS_FUNCTION_WRITE_SINGLE:
    case MODBUS_FUNCTION_WRITE_MULTIPLE:
        exception = app_modbus_write_registers(request, result);
        break;

    default:
        exception = MODBUS_EXCEPTION_FUNCTION;
        break;
    }

    result->exception = exception;

    if (exception != MODBUS_EXCEPTION_NONE)
    {
        /*
         * 失败事务只保留异常，不保留正常响应或待应用动作。
         */
        result->data_length = 0U;
        result->apply_flags = APP_MODBUS_APPLY_NONE;
        result->next_device_id = 0U;
        result->next_baudrate = 0U;
    }
}
