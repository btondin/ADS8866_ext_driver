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

static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

const struct gpio_dt_spec ledspec = GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);


LOG_MODULE_REGISTER(Lesson6_Exercise1, LOG_LEVEL_DBG);

int main(void)
{
	int err;
	uint32_t count = 0;
	uint16_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};

	/* --- LOG DE DIAGNÓSTICO --- */
	LOG_INF("Aplicativo iniciado. Verificando dispositivos...");

	if (!adc_is_ready_dt(&adc_channel)) {
		LOG_ERR("ADC controller devivce %s not ready", adc_channel.dev->name);
		return 0;
	}

	/* --- LOG DE DIAGNÓSTICO --- */
	LOG_INF("Dispositivo ADC %s pronto.", adc_channel.dev->name);

	err = adc_channel_setup_dt(&adc_channel);
	if (err < 0) {
		LOG_ERR("Could not setup channel #%d (%d)", 0, err);
		return 0;
	}

	err = adc_sequence_init_dt(&adc_channel, &sequence);
	if (err < 0) {
		LOG_ERR("Could not initalize sequnce");
		return 0;
	}

	err = gpio_is_ready_dt(&ledspec);
	if (!err) {
		LOG_ERR("Error: GPIO device is not ready, err: %d", err);
		return 0;
	}

	/* --- LOG DE DIAGNÓSTICO --- */
	LOG_INF("Setup completo. Entrando no loop principal...");

	gpio_pin_configure_dt(&ledspec, GPIO_OUTPUT_ACTIVE);

	while (1) {
		int32_t val_mv;

		/* --- LOG DE DIAGNÓSTICO --- */
		LOG_INF("Tentando ler do ADC...");

		

		err = adc_read(adc_channel.dev, &sequence);

		while(0){		
			gpio_pin_toggle_dt(&ledspec);
			k_msleep(100);
		}

		if (err < 0) {
			LOG_ERR("Could not read (%d)", err);
			continue;
		}

		

		val_mv = (int32_t)buf;
		LOG_INF("ADC reading[%u]: %s, channel %d: Raw: %u", count++, adc_channel.dev->name,
                adc_channel.channel_id, buf);

		err = adc_raw_to_millivolts_dt(&adc_channel, &val_mv);
        if (err < 0) {
            LOG_WRN(" (value in mV not available)\n");
        } else {
            /* 3. Imprima o NOVO valor de 'val_mv', que agora está em mV. */
            LOG_INF(" = %d mV", val_mv);
        }

		k_sleep(K_MSEC(1000));
	}
	return 0;
}