/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>

#include <storage/gate_counter_store.h>

#define GATE_COUNTER_STORE_VALUE_SIZE 4u

#define NVS_PARTITION storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)
#define NVS_PARTITION_SIZE FIXED_PARTITION_SIZE(NVS_PARTITION)

int gate_counter_store_init(struct gate_counter_store *store)
{
	struct flash_pages_info info;
	int ret;

	if (store == NULL) {
		return -EINVAL;
	}

	memset(store, 0, sizeof(*store));

	store->fs.flash_device = NVS_PARTITION_DEVICE;
	if (!device_is_ready(store->fs.flash_device)) {
		return -ENODEV;
	}

	store->fs.offset = NVS_PARTITION_OFFSET;

	ret = flash_get_page_info_by_offs(store->fs.flash_device, store->fs.offset, &info);
	if (ret < 0) {
		return ret;
	}

	store->fs.sector_size = info.size;
	store->fs.sector_count = CONFIG_GATE_COUNTER_NVS_SECTOR_COUNT;
	if ((uint64_t)store->fs.sector_size * store->fs.sector_count > NVS_PARTITION_SIZE) {
		return -EINVAL;
	}

	ret = nvs_mount(&store->fs);
	if (ret < 0) {
		return ret;
	}

	store->mounted = true;

	return 0;
}

int gate_counter_store_read(struct gate_counter_store *store, uint16_t id, uint32_t *value,
			    bool *found)
{
	uint8_t raw[GATE_COUNTER_STORE_VALUE_SIZE];
	int ret;

	if (store == NULL || value == NULL || found == NULL || !store->mounted) {
		return -EINVAL;
	}

	*found = false;
	*value = 0u;

	ret = nvs_read(&store->fs, id, raw, sizeof(raw));
	if (ret == -ENOENT) {
		return 0;
	}

	if (ret < 0) {
		return ret;
	}

	if (ret != sizeof(raw)) {
		return -EIO;
	}

	*value = sys_get_le32(raw);
	*found = true;

	return 0;
}

int gate_counter_store_write(struct gate_counter_store *store, uint16_t id, uint32_t value)
{
	uint8_t raw[GATE_COUNTER_STORE_VALUE_SIZE];
	int ret;

	if (store == NULL || !store->mounted) {
		return -EINVAL;
	}

	sys_put_le32(value, raw);

	ret = nvs_write(&store->fs, id, raw, sizeof(raw));
	if (ret < 0) {
		return ret;
	}

	return 0;
}
