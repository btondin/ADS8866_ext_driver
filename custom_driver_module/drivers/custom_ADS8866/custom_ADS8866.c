/*
 * Copyright (c) 2024 (Your Name/Your Company Here)
 * Copyright (c) 2023 Google, LLC (based on ads7052 structure)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The 'compatible' must match the binding and the overlay */
#define DT_DRV_COMPAT ti_custom_ads8866

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(custom_ads8866);

#define ADC_CONTEXT_USES_KERNEL_TIMER
#include "adc_context.h"

#define ADS8866_RESOLUTION 16U

/**
 * @brief Driver configuration data (read-only, from devicetree).
 */
struct ads8866_config {
    /** SPI bus specification from devicetree */
    struct spi_dt_spec bus;
    /** Number of channels (always 1 for ADS8866) */
    uint8_t channels;
    /** GPIO specification for the CONVST (Conversion Start) pin */
	struct gpio_dt_spec convst;
};

/**
 * @brief Driver instance data (per-device, runtime state).
 */
struct ads8866_data {
    /** ADC context for asynchronous operations */
	struct adc_context ctx;
    /** Pointer to the device structure */
	const struct device *dev;
    /** Pointer to the current sample buffer */
	uint16_t *buffer;
    /** Pointer to the buffer for repeated sampling */
	uint16_t *repeat_buffer;
    /** Bitmask of channels to be sampled */
	uint8_t channels;
    /** Acquisition thread for handling sampling */
	struct k_thread thread;
    /** Semaphore to signal the acquisition thread */
	struct k_sem sem;

	/** Stack definition for the acquisition thread */	
	K_KERNEL_STACK_MEMBER(stack, CONFIG_ADC_ADS8866_ACQUISITION_THREAD_STACK_SIZE);
};

/**
 * @brief Implementation of the ADC API 'channel_setup' function.
 *
 * @param dev Pointer to the ADC device instance.
 * @param channel_cfg Pointer to the channel configuration.
 *
 * @return 0 on success.
 * @return -ENOTSUP if the configuration is not supported (invalid gain,
 * channel ID, or acquisition time).
 */
static int adc_ads8866_channel_setup(const struct device *dev,
				     const struct adc_channel_cfg *channel_cfg)
{
	const struct ads8866_config *config = dev->config;

	/* The ADS8866 has a fixed gain */
	if (channel_cfg->gain != ADC_GAIN_1) {
		LOG_ERR("Gain %d not supported.", channel_cfg->gain);
		return -ENOTSUP;
	}
	
	/* The ADS8866 is a single-channel ADC (Channel 0) */
	if (channel_cfg->channel_id >= config->channels) {
		LOG_ERR("Invalid channel %d. The ADS8866 only supports channel 0.", 
			channel_cfg->channel_id);
		return -ENOTSUP;
	}

	/* The ADS8866 does not have a software-configurable acquisition time */
	if (channel_cfg->acquisition_time != ADC_ACQ_TIME_DEFAULT) {
		LOG_ERR("Acquisition time %d not supported.",
			channel_cfg->acquisition_time);
		return -ENOTSUP;
	}	

	/* The reference is defined by 'vref-mv' in the devicetree */
	if (channel_cfg->reference != ADC_REF_EXTERNAL0 &&
	    channel_cfg->reference != ADC_REF_VDD_1) {
		LOG_WRN("Reference ignored. Use 'vref-mv' in the overlay.");
	}

	return 0;
}

/**
 * @brief Validates if the sequence buffer is large enough.
 *
 * @param dev Pointer to the ADC device instance.
 * @param sequence Pointer to the ADC sequence configuration.
 *
 * @return 0 on success.
 * @return -ENOMEM if the buffer is too small.
 */
static int ads8866_validate_buffer_size(const struct device *dev,
					const struct adc_sequence *sequence)
{
	uint8_t channels = 0;
	size_t needed;

	channels = POPCOUNT(sequence->channels);

	needed = channels * sizeof(uint16_t);
	if (sequence->options) {
		needed *= (1 + sequence->options->extra_samplings);
	}

	if (sequence->buffer_size < needed) {
		return -ENOMEM;
	}

	return 0;
}

/**
 * @brief Internal function to configure and start a read sequence.
 *
 * @param dev Pointer to the ADC device instance.
 * @param sequence Pointer to the ADC sequence configuration.
 *
 * @return 0 on success, or a negative error code on failure.
 */
static int ads8866_start_read(const struct device *dev, const struct adc_sequence *sequence)
{
	const struct ads8866_config *config = dev->config;
	struct ads8866_data *data = dev->data;
	int err;	

	if (sequence->resolution != ADS8866_RESOLUTION) {
		LOG_ERR("Resolution %d not supported. Use %d.", sequence->resolution, ADS8866_RESOLUTION);
		return -ENOTSUP;
	}

	/* The ADS8866 only has channel 0 */
	if (find_msb_set(sequence->channels) > config->channels) {
		LOG_ERR("unsupported channels in mask: 0x%08x", sequence->channels);
		return -ENOTSUP;
	}

	err = ads8866_validate_buffer_size(dev, sequence);
	if (err) {
		LOG_ERR("Buffer size too small.");
		return err;
	}

	data->buffer = sequence->buffer;
	adc_context_start_read(&data->ctx, sequence);

	return adc_context_wait_for_completion(&data->ctx);
}

/**
 * @brief Implementation of the ADC API 'read_async' function.
 *
 * @param dev Pointer to the ADC device instance.
 * @param sequence Pointer to the ADC sequence configuration.
 * @param async Pointer to a poll signal for async notification (can be NULL).
 *
 * @return 0 on success, or a negative error code on failure.
 */
static int adc_ads8866_read_async(const struct device *dev, 
	const struct adc_sequence *sequence,
				  struct k_poll_signal *async)
{
	struct ads8866_data *data = dev->data;
	int error;

	adc_context_lock(&data->ctx, async ? true : false, async);
	error = ads8866_start_read(dev, sequence);
	adc_context_release(&data->ctx, error);

	return error;
}

/**
 * @brief Implementation of the ADC API 'read' (blocking) function.
 *
 * @param dev Pointer to the ADC device instance.
 * @param sequence Pointer to the ADC sequence configuration.
 *
 * @return 0 on success, or a negative error code on failure.
 */
static int adc_ads8866_read(const struct device *dev, 
	const struct adc_sequence *sequence)
{
	/* This is just a blocking wrapper for the async read function */
	return adc_ads8866_read_async(dev, sequence, NULL);
}

/**
 * @brief Callback from adc_context to start the sampling.
 *
 * This function is called by the adc_context helper when a read is initiated.
 * It signals the acquisition thread to perform the actual read.
 *
 * @param ctx Pointer to the ADC context.
 */
static void adc_context_start_sampling(struct adc_context *ctx)
{
	struct ads8866_data *data = CONTAINER_OF(ctx, struct ads8866_data, ctx);

	data->channels = ctx->sequence.channels;
	data->repeat_buffer = data->buffer;

	/* Give the semaphore to wake up the acquisition thread */
	k_sem_give(&data->sem);
}

/**
 * @brief Callback from adc_context to update the buffer pointer.
 *
 * @param ctx Pointer to the ADC context.
 * @param repeat_sampling True if this is a repeated sampling.
 */
static void adc_context_update_buffer_pointer(struct adc_context *ctx, 
	bool repeat_sampling)
{
	struct ads8866_data *data = CONTAINER_OF(ctx, struct ads8866_data, ctx);

	if (repeat_sampling) {
		data->buffer = data->repeat_buffer;
	}
}

/**
 * @brief Starts the conversion and reads one 16-bit sample from the ADC.
 *
 * This function handles the specific hardware signaling for the ADS8866:
 * 1. Pulses CONVST high to start conversion.
 * 2. Waits for conversion time.
 * 3. Pulls CONVST low.
 * 4. Reads the 16-bit result from SPI.
 *
 * @param dev Pointer to the ADC device instance.
 * @param result Pointer to a uint16_t to store the read sample.
 *
 * @return 0 on success, or a negative SPI error code.
 */
static int ads8866_read_channel(const struct device *dev, uint16_t *result)
{
    const struct ads8866_config *config = dev->config;
    int err;

    /*
     * STEP 1: Pulse the CONVST pin to start the conversion
     * The pin was configured as GPIO_OUTPUT_INACTIVE (LOW) in init()
     */
    
    // 1.a: Pull CONVST HIGH (ACTIVE)
    gpio_pin_set_dt(&config->convst, 1);

    // 1.b: Wait for the conversion time (t_conv-max)
    //      (Datasheet specifies max 8.3 µs. 9 µs is safe)
    k_busy_wait(9); 

    // 1.c: Pull CONVST LOW (INACTIVE) to enable DOUT output
    gpio_pin_set_dt(&config->convst, 0);

    // 1.d: Wait for the quiet time (t_quiet) before SCLK
    //      (Datasheet specifies min 25 ns. 1 µs is safe)
    //k_busy_wait(1);

    /*
     * STEP 2: Now that the conversion is ready and DOUT is enabled,
     * read the 16 bits of data via SPI.
     */
    uint8_t rx_buffer[2];

    struct spi_buf rx_buf = {
        .buf = rx_buffer,
        .len = sizeof(rx_buffer) // We want to read 2 bytes
    };
    const struct spi_buf_set rx_bufs = {
        .buffers = &rx_buf,
        .count = 1
    };

    /*
     * Use spi_read_dt() to read the data.
     * The SPIM driver (nrf-spim) will trigger the CS pin
     * automatically during this transaction.
     */
    err = spi_read_dt(&config->bus, &rx_bufs);
    if (err) {
        LOG_ERR("SPI read failed: %d", err);
        return err;
    }

    /* Converts the 2 received bytes (Big Endian) to uint16_t */
    *result = sys_be16_to_cpu(*((uint16_t *)rx_buffer));
    *result &= BIT_MASK(ADS8866_RESOLUTION); // Apply the mask (0xFFFF)

    return 0;
}


/**
 * @brief Main function for the acquisition thread.
 *
 * This thread waits on a semaphore. When signaled by adc_context,
 * it performs the actual hardware read via ads8866_read_channel()
 * and notifies the context when complete.
 *
 * @param p1 Pointer to the device's data structure.
 * @param p2 Unused.
 * @param p3 Unused.
 */
static void ads8866_acquisition_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct ads8866_data *data = p1;
	const struct device *dev = data->dev;
	uint16_t result = 0;
	int err = 0;

	while (true) {
		/* Wait for permission to start reading */
		k_sem_take(&data->sem, K_FOREVER);

		/* The ADS8866 only has channel 0, so 'channels' will be BIT(0) */
		if (data->channels != 0) {

			LOG_DBG("Reading channel 0");
			err = ads8866_read_channel(dev, &result);
			if (err) {
				LOG_ERR("Failed to read channel 0 (err %d)", err);
				adc_context_complete(&data->ctx, err);
				break; /* Abort on error */
			}

			LOG_DBG("Channel 0, result = %d", result);

			/* Place the result in the buffer and advance the pointer */
			*data->buffer++ = result;
			data->channels = 0; /* Mark as read */
		}

		/* Notify the adc_context that sampling is done */
		adc_context_on_sampling_done(&data->ctx, dev);
	}
}

/**
 * @brief Initialization function for the ADS8866 driver.
 *
 * This function is called by the kernel at boot time.
 * It initializes the driver data, checks SPI/GPIO readiness,
 * and starts the acquisition thread.
 *
 * @param dev Pointer to the ADC device instance.
 *
 * @return 0 on success.
 * @return -ENODEV if the SPI or GPIO peripherals are not ready.
 */
static int adc_ads8866_init(const struct device *dev)
{
	const struct ads8866_config *config = dev->config;
	struct ads8866_data *data = dev->data;

	data->dev = dev;

	adc_context_init(&data->ctx);
	k_sem_init(&data->sem, 0, 1); /* Init semaphore (0 initial, 1 limit) */

	if (!spi_is_ready_dt(&config->bus)) {
		LOG_ERR("SPI bus %s not ready", config->bus.bus->name);
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->convst)) {
		LOG_ERR("CONVST GPIO pin is not ready");
		return -ENODEV;
	}
	/* Configure the pin as output and set it to INACTIVE (LOW) */
	gpio_pin_configure_dt(&config->convst, GPIO_OUTPUT_INACTIVE);

	/* The ADS8866 does not require calibration or configuration at init */

	k_thread_create(&data->thread, data->stack,
			K_KERNEL_STACK_SIZEOF(data->stack),
			ads8866_acquisition_thread, data, NULL, NULL,
			CONFIG_ADC_ADS8866_ACQUISITION_THREAD_PRIO, 0, K_NO_WAIT);

	adc_context_unlock_unconditionally(&data->ctx);

	LOG_INF("Device %s initialized.", dev->name);

	return 0;
}

/**
 * @brief Defines the ADC driver API structure.
 */
static const struct adc_driver_api adc_ads8866_api = {
	.channel_setup = adc_ads8866_channel_setup,
	.read = adc_ads8866_read,
#ifdef CONFIG_ADC_ASYNC
	.read_async = adc_ads8866_read_async,
#endif
};

/**
 * @brief Defines the SPI configuration for the ADS8866.
 * 8 bits, MSB first, Mode 0 (CPOL=0, CPHA=0).
 */
#define ADC_ADS8866_SPI_CFG \
	(SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB)

/**
 * @brief Macro to initialize a driver instance.
 *
 * This macro defines the config and data structures for each
 * instance of the driver found in the devicetree.
 */
#define ADC_ADS8866_INIT(n)                                                   \
    static const struct ads8866_config ads8866_cfg_##n = {                 \
        .bus = SPI_DT_SPEC_INST_GET(n, ADC_ADS8866_SPI_CFG, 0U),       \
        .channels = 1,                \
		.convst = GPIO_DT_SPEC_INST_GET(n, convst_gpios),             \
    };                                                                    \
                                                                    \
	static struct ads8866_data ads8866_data_##n = {                       \
		ADC_CONTEXT_INIT_TIMER(ads8866_data_##n, ctx),                \
		ADC_CONTEXT_INIT_LOCK(ads8866_data_##n, ctx),                 \
		ADC_CONTEXT_INIT_SYNC(ads8866_data_##n, ctx),                 \
	};                                                                    \
                                                                              \
	DEVICE_DT_INST_DEFINE(n, adc_ads8866_init, NULL, &ads8866_data_##n,   \
			      &ads8866_cfg_##n, POST_KERNEL,                  \
			      CONFIG_ADC_INIT_PRIORITY, &adc_ads8866_api);

/* Iterates over all "okay" instances in the devicetree and calls INIT */
DT_INST_FOREACH_STATUS_OKAY(ADC_ADS8866_INIT)