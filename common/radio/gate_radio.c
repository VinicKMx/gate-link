/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

#include <radio/gate_radio.h>

LOG_MODULE_REGISTER(gate_radio, CONFIG_GATE_RADIO_LOG_LEVEL);

#define GATE_LORA_NODE DT_ALIAS(lora0)
#define SX127X_REG_VERSION 0x42
#define SX1276_REG_VERSION_VALUE 0x12

#if CONFIG_GATE_RADIO_BANDWIDTH_KHZ == 125
#define GATE_RADIO_BANDWIDTH BW_125_KHZ
#elif CONFIG_GATE_RADIO_BANDWIDTH_KHZ == 250
#define GATE_RADIO_BANDWIDTH BW_250_KHZ
#elif CONFIG_GATE_RADIO_BANDWIDTH_KHZ == 500
#define GATE_RADIO_BANDWIDTH BW_500_KHZ
#else
#error "CONFIG_GATE_RADIO_BANDWIDTH_KHZ must be 125, 250, or 500"
#endif

#if DT_NODE_HAS_STATUS(GATE_LORA_NODE, okay) && IS_ENABLED(CONFIG_LORA)

static const struct device *const lora_dev = DEVICE_DT_GET(GATE_LORA_NODE);
static const struct spi_dt_spec lora_spi = SPI_DT_SPEC_GET(
	GATE_LORA_NODE, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8), 0);

static int sx1276_read_version(uint8_t *version)
{
	uint8_t reg = SX127X_REG_VERSION;
	uint8_t value = 0;
	const struct spi_buf tx_buf[] = {
		{
			.buf = &reg,
			.len = sizeof(reg),
		},
		{
			.buf = &value,
			.len = sizeof(value),
		},
	};
	struct spi_buf rx_buf[] = {
		{
			.buf = &reg,
			.len = sizeof(reg),
		},
		{
			.buf = &value,
			.len = sizeof(value),
		},
	};
	const struct spi_buf_set tx = {
		.buffers = tx_buf,
		.count = ARRAY_SIZE(tx_buf),
	};
	const struct spi_buf_set rx = {
		.buffers = rx_buf,
		.count = ARRAY_SIZE(rx_buf),
	};
	int ret;

	ret = spi_transceive_dt(&lora_spi, &tx, &rx);
	if (ret < 0) {
		return ret;
	}

	*version = value;
	return 0;
}

int gate_radio_init(void)
{
	struct lora_modem_config config = {
		.frequency = CONFIG_GATE_RADIO_FREQUENCY_HZ,
		.bandwidth = GATE_RADIO_BANDWIDTH,
		.datarate = (enum lora_datarate)CONFIG_GATE_RADIO_SPREADING_FACTOR,
		.coding_rate = (enum lora_coding_rate)(CONFIG_GATE_RADIO_CODING_RATE_DENOMINATOR - 4),
		.preamble_len = CONFIG_GATE_RADIO_PREAMBLE_LEN,
		.tx_power = CONFIG_GATE_RADIO_TX_POWER_DBM,
		.tx = false,
		.iq_inverted = false,
		.public_network = IS_ENABLED(CONFIG_GATE_RADIO_PUBLIC_NETWORK),
	};
	uint8_t version;
	int ret;

	if (!device_is_ready(lora_dev)) {
		LOG_ERR("LoRa device is not ready");
		return -ENODEV;
	}

	ret = sx1276_read_version(&version);
	if (ret < 0) {
		LOG_ERR("SX1276 version read failed: %d", ret);
		return ret;
	}

	if (version != SX1276_REG_VERSION_VALUE) {
		LOG_ERR("Unexpected SX1276 version 0x%02x, expected 0x%02x", version,
			SX1276_REG_VERSION_VALUE);
		return -EIO;
	}

	ret = lora_config(lora_dev, &config);
	if (ret < 0) {
		LOG_ERR("LoRa config failed: %d", ret);
		return ret;
	}

	LOG_INF("LoRa ready: freq=%u Hz bw=%u kHz sf=%u cr=4/%u preamble=%u tx_power=%d dBm",
		CONFIG_GATE_RADIO_FREQUENCY_HZ, CONFIG_GATE_RADIO_BANDWIDTH_KHZ,
		CONFIG_GATE_RADIO_SPREADING_FACTOR, CONFIG_GATE_RADIO_CODING_RATE_DENOMINATOR,
		CONFIG_GATE_RADIO_PREAMBLE_LEN, CONFIG_GATE_RADIO_TX_POWER_DBM);

	return 0;
}

#else

int gate_radio_init(void)
{
	LOG_INF("LoRa radio is not configured in this build");
	return -ENODEV;
}

#endif
