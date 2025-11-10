/*
 * Copyright (c) 2024 (Seu Nome/Sua Empresa Aqui)
 * Copyright (c) 2023 Google, LLC (estrutura baseada no ads7052)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* O 'compatible' deve bater com o binding e o overlay */
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

/* Configuração do driver (lida do devicetree) */
struct ads8866_config {
    struct spi_dt_spec bus;
    uint8_t channels;
	struct gpio_dt_spec convst;
};

/* Dados de instância do driver */
struct ads8866_data {
	struct adc_context ctx;
	const struct device *dev;
	uint16_t *buffer;
	uint16_t *repeat_buffer;
	uint8_t channels;
	struct k_thread thread;
	struct k_sem sem;

	/* Define um stack para a thread de aquisição */	
	K_KERNEL_STACK_MEMBER(stack, CONFIG_ADC_ADS8866_ACQUISITION_THREAD_STACK_SIZE);
};

static int adc_ads8866_channel_setup(const struct device *dev,
				     const struct adc_channel_cfg *channel_cfg)
{
	const struct ads8866_config *config = dev->config;

	/* O ADS8866 tem ganho fixo */
	if (channel_cfg->gain != ADC_GAIN_1) {
		LOG_ERR("Ganho %d não suportado.", channel_cfg->gain);
		return -ENOTSUP;
	}
	

	/* O ADS8866 é um ADC de canal único (Canal 0) */
	if (channel_cfg->channel_id >= config->channels) {
		LOG_ERR("Canal inválido %d. O ADS8866 só suporta o canal 0.", 
			channel_cfg->channel_id);
		return -ENOTSUP;
	}

	/* O ADS8866 não tem tempo de aquisição configurável via software */
	if (channel_cfg->acquisition_time != ADC_ACQ_TIME_DEFAULT) {
		LOG_ERR("Tempo de aquisição %d não suportado.",
			channel_cfg->acquisition_time);
		return -ENOTSUP;
	}	

	

	/* A referência é definida pelo 'vref-mv' no devicetree */
	if (channel_cfg->reference != ADC_REF_EXTERNAL0 &&
	    channel_cfg->reference != ADC_REF_VDD_1) {
		LOG_WRN("Referência ignorada. Use 'vref-mv' no overlay.");
	}

	return 0;
}

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

static int ads8866_start_read(const struct device *dev, const struct adc_sequence *sequence)
{
	const struct ads8866_config *config = dev->config;
	struct ads8866_data *data = dev->data;
	int err;	

	if (sequence->resolution != ADS8866_RESOLUTION) {
		LOG_ERR("Resolução %d não suportada. Use %d.", sequence->resolution, ADS8866_RESOLUTION);
		return -ENOTSUP;
	}

	/* O ADS8866 só tem o canal 0 */
	if (find_msb_set(sequence->channels) > config->channels) {
		LOG_ERR("unsupported channels in mask: 0x%08x", sequence->channels);
		return -ENOTSUP;
	}

	err = ads8866_validate_buffer_size(dev, sequence);
	if (err) {
		LOG_ERR("Tamanho do buffer muito pequeno.");
		return err;
	}

	data->buffer = sequence->buffer;
	adc_context_start_read(&data->ctx, sequence);

	return adc_context_wait_for_completion(&data->ctx);
}

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

static int adc_ads8866_read(const struct device *dev, 
	const struct adc_sequence *sequence)
{
	return adc_ads8866_read_async(dev, sequence, NULL);
}

static void adc_context_start_sampling(struct adc_context *ctx)
{
	struct ads8866_data *data = CONTAINER_OF(ctx, struct ads8866_data, ctx);

	data->channels = ctx->sequence.channels;
	data->repeat_buffer = data->buffer;

	k_sem_give(&data->sem);
}

static void adc_context_update_buffer_pointer(struct adc_context *ctx, 
	bool repeat_sampling)
{
	struct ads8866_data *data = CONTAINER_OF(ctx, struct ads8866_data, ctx);

	if (repeat_sampling) {
		data->buffer = data->repeat_buffer;
	}
}

/**
 * @brief Inicia a conversão e lê uma amostra de 16 bits do ADC.
 * (Modificado para incluir a lógica do pino CONVST)
 */
static int ads8866_read_channel(const struct device *dev, uint16_t *result)
{
    const struct ads8866_config *config = dev->config;
    int err;

    /*
     * PASSO 1: Pulsar o pino CONVST para iniciar a conversão
     * O pino foi configurado como GPIO_OUTPUT_INACTIVE (LOW) no init()
     */
    
    // 1.a: Puxar CONVST para ALTO (ATIVO)
    gpio_pin_set_dt(&config->convst, 1);

    // 1.b: Esperar pelo tempo de conversão (t_conv-max)
    //      (O datasheet especifica max 8.3 µs. 9 µs é seguro)
    k_busy_wait(9); 

    // 1.c: Puxar CONVST para BAIXO (INATIVO) para habilitar a saída DOUT
    gpio_pin_set_dt(&config->convst, 0);

    // 1.d: Esperar pelo tempo quieto (t_quiet) antes do SCLK
    //      (O datasheet especifica min 25 ns. 1 µs é seguro)
    //k_busy_wait(1);

    /*
     * PASSO 2: Agora que a conversão está pronta e DOUT está habilitado,
     * ler os 16 bits de dados via SPI.
     */
    uint8_t rx_buffer[2];

    struct spi_buf rx_buf = {
        .buf = rx_buffer,
        .len = sizeof(rx_buffer) // Queremos ler 2 bytes
    };
    const struct spi_buf_set rx_bufs = {
        .buffers = &rx_buf,
        .count = 1
    };

    /*
     * Usar spi_read_dt() para ler os dados.
     * O driver SPIM (nrf-spim) irá acionar o pino CS (Pino 10)
     * automaticamente durante esta transação.
     */
    err = spi_read_dt(&config->bus, &rx_bufs);
    if (err) {
        LOG_ERR("SPI read failed: %d", err);
        return err;
    }

    /* Converte os 2 bytes recebidos (Big Endian) para uint16_t */
    *result = sys_be16_to_cpu(*((uint16_t *)rx_buffer));
    *result &= BIT_MASK(ADS8866_RESOLUTION); // Aplica a máscara (0xFFFF)

    return 0;
}


static void ads8866_acquisition_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct ads8866_data *data = p1;
	const struct device *dev = data->dev;
	uint16_t result = 0;
	int err = 0;

	while (true) {
		/* Espera a permissão para começar a ler */
		k_sem_take(&data->sem, K_FOREVER);

		/* O ADS8866 só tem o canal 0, então 'channels' será BIT(0) */
		if (data->channels != 0) {

			LOG_DBG("Lendo canal 0");
			err = ads8866_read_channel(dev, &result);
			if (err) {
				LOG_ERR("Falha ao ler o canal 0 (err %d)", err);
				adc_context_complete(&data->ctx, err);
				break; /* Aborta em caso de erro */
			}

			LOG_DBG("Canal 0, resultado = %d", result);

			*data->buffer++ = result;
			data->channels = 0; /* Marca como lido */
		}

		adc_context_on_sampling_done(&data->ctx, dev);
	}
}

static int adc_ads8866_init(const struct device *dev)
{
	const struct ads8866_config *config = dev->config;
	struct ads8866_data *data = dev->data;

	data->dev = dev;

	adc_context_init(&data->ctx);
	k_sem_init(&data->sem, 0, 1);

	if (!spi_is_ready_dt(&config->bus)) {
		LOG_ERR("SPI bus %s not ready", config->bus.bus->name);
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->convst)) {
		LOG_ERR("Pino CONVST GPIO não está pronto");
		return -ENODEV;
	}
	/* Configura o pino como saída e o define como INATIVO (LOW) */
	gpio_pin_configure_dt(&config->convst, GPIO_OUTPUT_INACTIVE);

	/* O ADS8866 não requer calibração ou configuração na inicialização */

	k_thread_create(&data->thread, data->stack,
			K_KERNEL_STACK_SIZEOF(data->stack),
			ads8866_acquisition_thread, data, NULL, NULL,
			CONFIG_ADC_ADS8866_ACQUISITION_THREAD_PRIO, 0, K_NO_WAIT);

	adc_context_unlock_unconditionally(&data->ctx);

	LOG_INF("Device %s inicializado.", dev->name);

	return 0;
}

/* Define a API do driver ADC */
static const struct adc_driver_api adc_ads8866_api = {
	.channel_setup = adc_ads8866_channel_setup,
	.read = adc_ads8866_read,
#ifdef CONFIG_ADC_ASYNC
	.read_async = adc_ads8866_read_async,
#endif
};

/* Define a operação SPI: 8 bits, MSB first, Modo 0 (CPOL=0, CPHA=0) */
/* (Ajuste CPHA/CPOL se o datasheet do ADS8866 exigir) */
#define ADC_ADS8866_SPI_CFG \
	(SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB)

/* Macro de inicialização para cada instância do device */
#define ADC_ADS8866_INIT(n)                                                   \
    static const struct ads8866_config ads8866_cfg_##n = {                 \
        .bus = SPI_DT_SPEC_INST_GET(n, ADC_ADS8866_SPI_CFG, 0U),       \
        .channels = 1,                \
		.convst = GPIO_DT_SPEC_INST_GET(n, convst_gpios),             \
    };                                                                    \
                                                                    \
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

/* Itera por todas as instâncias "okay" no devicetree */
DT_INST_FOREACH_STATUS_OKAY(ADC_ADS8866_INIT)