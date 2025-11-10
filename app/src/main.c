/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>

/* Get the ADC channel specification from the devicetree's 'zephyr,user' node */
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

/* Get the GPIO specification for the LED (aliased as 'led0') */
const struct gpio_dt_spec ledspec = GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);

/* Register the log module */
LOG_MODULE_REGISTER(Lesson6_Exercise1, LOG_LEVEL_DBG);

int main(void)
{
	int err; /* Variable to store error codes */
	uint32_t count = 0; /* Simple counter for logging */
	uint16_t buf; /* Single-sample buffer for the ADC */
	struct adc_sequence sequence = {
		.buffer = &buf,
		/* We are reading one 16-bit sample (sizeof(uint16_t)) */
		.buffer_size = sizeof(buf),
	};

	/* --- DIAGNOSTIC LOG --- */
	LOG_INF("Application started. Checking devices...");

	/* Check if the ADC controller device is ready to use */
	if (!adc_is_ready_dt(&adc_channel)) {
		/* Fixed typo: "devivce" -> "device" */
		LOG_ERR("ADC controller device %s not ready", adc_channel.dev->name);
		return 0; /* Halt execution if ADC is not ready */
	}

	/* --- DIAGNOSTIC LOG --- */
	LOG_INF("ADC device %s is ready.", adc_channel.dev->name);

	/* Configure the ADC channel using settings from the devicetree */
	err = adc_channel_setup_dt(&adc_channel);
	if (err < 0) {
		LOG_ERR("Could not setup channel #%d (%d)", 0, err);
		return 0;
	}

	/* Initialize the ADC sequence structure */
	err = adc_sequence_init_dt(&adc_channel, &sequence);
	if (err < 0) {
		/* Fixed typo: "initalize sequnce" -> "initialize sequence" */
		LOG_ERR("Could not initialize sequence (%d)", err);
		return 0;
	}

	/* Check if the GPIO controller for the LED is ready */
	err = gpio_is_ready_dt(&ledspec);
	if (!err) {
		LOG_ERR("Error: GPIO device is not ready, err: %d", err);
		return 0;
	}

	/* --- DIAGNOSTIC LOG --- */
	LOG_INF("Setup complete. Entering main loop...");

	/* Configure the LED pin as an active-high output */
	gpio_pin_configure_dt(&ledspec, GPIO_OUTPUT_ACTIVE);

	/* Main application loop */
	while (1) {
		int32_t val_mv; /* Variable to hold the millivolt conversion */

		/* --- DIAGNOSTIC LOG --- */
		LOG_INF("Attempting to read from ADC...");

		
		/* Perform a blocking read from the ADC */
		err = adc_read(adc_channel.dev, &sequence);

		/* * This is dead code (while(0)) and will never execute.
		 * It was likely used for debugging the LED.
		 */
		while(0){		
			gpio_pin_toggle_dt(&ledspec);
			k_msleep(100);
		}

		/* Check for errors during the read operation */
		if (err < 0) {
			LOG_ERR("Could not read (%d)", err);
			continue; /* Skip the rest of the loop and try again */
		}

		
		/* * The raw buffer 'buf' (uint16_t) is now populated.
		 * Copy it to 'val_mv' (int32_t) for processing.
		 */
		val_mv = (int32_t)buf;

		/* Log the raw ADC value */
		LOG_INF("ADC reading[%u]: %s, channel %d: Raw: %u", count++, adc_channel.dev->name,
                adc_channel.channel_id, buf);

		/* * Convert the raw value to millivolts using the properties
		 * defined in the devicetree (vref-mv, gain, etc.)
		 */
		err = adc_raw_to_millivolts_dt(&adc_channel, &val_mv);
        if (err < 0) {
			/* Log a warning if conversion is not possible */
            LOG_WRN(" (value in mV not available)\n");
        } else {
            /* 3. Print the NEW 'val_mv' value, which is now in mV. */
            LOG_INF(" = %d mV", val_mv);
        }

		/* Wait for 1 second before the next reading */
		k_sleep(K_MSEC(1000));
	}
	return 0; /* Should never be reached */
}