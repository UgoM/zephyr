/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/audio/codec.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/rtp.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>
#include <soc.h>

#include "net_sample_common.h"

LOG_MODULE_REGISTER(main);

/* RTP session the sender transmits to. */
#define RTP_MCAST_ADDR "239.0.1.1"
#define RTP_MCAST_PORT 5004

/* L16 (RFC 3551): big-endian signed 16-bit, channels interleaved. */
#define SAMPLE_RATE_HZ   24000U
#define CHANNELS         2U
#define BYTES_PER_SAMPLE 2U
#define FRAME_BYTES      (CHANNELS * BYTES_PER_SAMPLE)

/* Raw WM8904 output volume field: 0 is -57 dB, 57 is 0 dB, 63 is +6 dB. */
#define OUTPUT_VOLUME 57

/* MCLK is fs-ratio * Fs: 128 * 24 kHz = 3.072 MHz, which PCK2 divides PLLACK
 * into exactly. See README.
 */
#define MCLK_FREQ_HZ (SAMPLE_RATE_HZ * 128U)

#define PLLACK_FREQ_HZ                                                                             \
	(MHZ(12) / CONFIG_SOC_ATMEL_SAM_PLLA_DIVA * (CONFIG_SOC_ATMEL_SAM_PLLA_MULA + 1))
#define PCK2_DIVIDER DIV_ROUND_CLOSEST(PLLACK_FREQ_HZ, MCLK_FREQ_HZ)

BUILD_ASSERT(PCK2_DIVIDER >= 1 && PCK2_DIVIDER <= 256, "PCK2 divider out of range");

#define BLOCK_MS       40U
#define BLOCK_FRAMES   (SAMPLE_RATE_HZ * BLOCK_MS / 1000U)
#define BLOCK_SIZE     (BLOCK_FRAMES * FRAME_BYTES)
/* Blocks the I2S driver allocates from; the driver's own DMA queue is
 * CONFIG_I2S_SAM_SSC_TX_BLOCK_COUNT deep on top of this pool.
 */
#define TX_BLOCK_COUNT 4U

/* Jitter buffer, and how much to build up before playout starts. */
#define JITTER_BUF_MS   400U
#define JITTER_BUF_SIZE (SAMPLE_RATE_HZ * FRAME_BYTES * JITTER_BUF_MS / 1000U)
#define PREFILL_MS      200U
#define PREFILL_SIZE    (SAMPLE_RATE_HZ * FRAME_BYTES * PREFILL_MS / 1000U)

BUILD_ASSERT(PREFILL_SIZE < JITTER_BUF_SIZE, "prefill mark is beyond the buffer");

K_MEM_SLAB_DEFINE_STATIC(tx_slab, BLOCK_SIZE, TX_BLOCK_COUNT, 4);
K_THREAD_STACK_DEFINE(playout_stack, 2048);
RING_BUF_DECLARE(jitter_buf, JITTER_BUF_SIZE);

static const struct device *const i2s_dev = DEVICE_DT_GET(DT_NODELABEL(ssc));
static const struct device *const codec_dev = DEVICE_DT_GET(DT_NODELABEL(audio_codec));

static struct k_thread playout_thread;
static RTP_SESSION_DEFINE(rtp_session, 0);

static atomic_t rx_packets;
static atomic_t tx_underruns;

/*
 * Programs the programmable clock the board wires to the codec master clock
 * input. The clock driver models peripheral clocks only, so the registers are
 * written here.
 */
static void enable_mclk(void)
{
	PMC->PMC_PCK[2] = PMC_PCK_CSS_PLLA_CLK | PMC_PCK_PRES(PCK2_DIVIDER - 1U);
	PMC->PMC_SCER = PMC_SCER_PCK2;

	while ((PMC->PMC_SR & PMC_SR_PCKRDY2) == 0U) {
	}
}

/* Runs in the network receive context, so it only moves bytes. */
static void rtp_recv_cb(struct rtp_session *session, struct rtp_packet *packet, void *user_data)
{
	ARG_UNUSED(session);
	ARG_UNUSED(user_data);

	atomic_inc(&rx_packets);

	/* Copy whole frames only; a partial copy would swap the channels. */
	if ((packet->payload_len % FRAME_BYTES) != 0U) {
		return;
	}

	if (ring_buf_space_get(&jitter_buf) < packet->payload_len) {
		return;
	}

	(void)ring_buf_put(&jitter_buf, packet->payload, packet->payload_len);
}

/*
 * Fills one I2S block from the jitter buffer, padding with silence and counting
 * an underrun when the buffer is short.
 */
static void fill_block(int16_t *block)
{
	uint32_t got = ring_buf_get(&jitter_buf, (uint8_t *)block, BLOCK_SIZE);

	if (got < BLOCK_SIZE) {
		memset((uint8_t *)block + got, 0, BLOCK_SIZE - got);
		atomic_inc(&tx_underruns);
	}

	for (size_t i = 0; i < BLOCK_SIZE / sizeof(int16_t); i++) {
		block[i] = (int16_t)sys_be16_to_cpu((uint16_t)block[i]);
	}
}

/*
 * Waits for the prefill mark, starts the transmitter and then keeps its queue
 * fed. i2s_buf_write() blocks until the hardware releases a block, so the codec
 * clock paces this loop.
 */
static void playout_entry(void *p1, void *p2, void *p3)
{
	static int16_t block[BLOCK_SIZE / sizeof(int16_t)];
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (ring_buf_size_get(&jitter_buf) < PREFILL_SIZE) {
		k_msleep(BLOCK_MS);
	}

	LOG_INF("Starting playout");

	for (uint32_t i = 0U; i < 2U; i++) {
		fill_block(block);

		ret = i2s_buf_write(i2s_dev, block, BLOCK_SIZE);
		if (ret < 0) {
			LOG_ERR("I2S prefill failed: %d", ret);
			return;
		}
	}

	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret < 0) {
		LOG_ERR("I2S start failed: %d", ret);
		return;
	}

	while (true) {
		fill_block(block);

		ret = i2s_buf_write(i2s_dev, block, BLOCK_SIZE);
		if (ret < 0) {
			LOG_ERR("Playout stopped: %d", ret);
			return;
		}
	}
}

static int setup_codec(void)
{
	struct audio_codec_cfg codec_cfg;
	int ret;

	memset(&codec_cfg, 0, sizeof(codec_cfg));
	codec_cfg.mclk_freq = MCLK_FREQ_HZ;
	codec_cfg.dai_type = AUDIO_DAI_TYPE_I2S;
	codec_cfg.dai_route = AUDIO_ROUTE_PLAYBACK;
	codec_cfg.dai_cfg.i2s.word_size = 16;
	codec_cfg.dai_cfg.i2s.channels = CHANNELS;
	codec_cfg.dai_cfg.i2s.format = I2S_FMT_DATA_FORMAT_I2S;
	codec_cfg.dai_cfg.i2s.options = I2S_OPT_BIT_CLK_CONTROLLER | I2S_OPT_FRAME_CLK_CONTROLLER;
	codec_cfg.dai_cfg.i2s.frame_clk_freq = SAMPLE_RATE_HZ;
	codec_cfg.dai_cfg.i2s.mem_slab = &tx_slab;
	codec_cfg.dai_cfg.i2s.block_size = BLOCK_SIZE;

	ret = audio_codec_configure(codec_dev, &codec_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to configure codec: %d", ret);
		return ret;
	}

	ret = audio_codec_set_property(codec_dev, AUDIO_PROPERTY_OUTPUT_VOLUME, AUDIO_CHANNEL_ALL,
				       (audio_property_value_t){.vol = OUTPUT_VOLUME});
	if (ret < 0) {
		LOG_ERR("Failed to set output volume: %d", ret);
		return ret;
	}

	/* The volume field only latches when the volume update bit is written. */
	ret = audio_codec_apply_properties(codec_dev);
	if (ret < 0) {
		LOG_ERR("Failed to apply codec properties: %d", ret);
		return ret;
	}

	audio_codec_start_output(codec_dev);

	return 0;
}

static int setup_i2s(void)
{
	struct i2s_config i2s_cfg;
	int ret;

	memset(&i2s_cfg, 0, sizeof(i2s_cfg));
	i2s_cfg.word_size = 16;
	i2s_cfg.channels = CHANNELS;
	i2s_cfg.format = I2S_FMT_DATA_FORMAT_I2S;
	i2s_cfg.options = I2S_OPT_BIT_CLK_TARGET | I2S_OPT_FRAME_CLK_TARGET;
	i2s_cfg.frame_clk_freq = SAMPLE_RATE_HZ;
	i2s_cfg.mem_slab = &tx_slab;
	i2s_cfg.block_size = BLOCK_SIZE;
	i2s_cfg.timeout = 1000;

	ret = i2s_configure(i2s_dev, I2S_DIR_TX, &i2s_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to configure I2S TX: %d", ret);
	}

	return ret;
}

static int setup_rtp(void)
{
	struct net_sockaddr_in addr = {
		.sin_family = NET_AF_INET,
		.sin_port = net_htons(RTP_MCAST_PORT),
	};
	int ret;

	if (net_addr_pton(NET_AF_INET, RTP_MCAST_ADDR, &addr.sin_addr) != 0) {
		LOG_ERR("Invalid address: %s", RTP_MCAST_ADDR);
		return -EINVAL;
	}

	ret = rtp_session_init_rx(&rtp_session, net_if_get_default(), (struct net_sockaddr *)&addr,
				  rtp_recv_cb, NULL, RTP_TRANSPORT_SOCKET);
	if (ret < 0) {
		LOG_ERR("Failed to init RTP session: %d", ret);
		return ret;
	}

	ret = rtp_session_start(&rtp_session);
	if (ret < 0) {
		LOG_ERR("Failed to start RTP session: %d", ret);
	}

	return ret;
}

int main(void)
{
	int ret;

	LOG_INF("RTP playback on %s at %u Hz", CONFIG_BOARD, SAMPLE_RATE_HZ);

	enable_mclk();

	if (!device_is_ready(i2s_dev)) {
		LOG_ERR("SSC I2S device not ready");
		return -ENODEV;
	}

	if (!device_is_ready(codec_dev)) {
		LOG_ERR("WM8904 codec not ready");
		return -ENODEV;
	}

	ret = setup_codec();
	if (ret < 0) {
		return ret;
	}

	ret = setup_i2s();
	if (ret < 0) {
		return ret;
	}

	wait_for_network();

	ret = setup_rtp();
	if (ret < 0) {
		return ret;
	}

	LOG_INF("Listening on %s:%u", RTP_MCAST_ADDR, RTP_MCAST_PORT);

	k_thread_create(&playout_thread, playout_stack, K_THREAD_STACK_SIZEOF(playout_stack),
			playout_entry, NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	k_thread_name_set(&playout_thread, "playout");

	while (true) {
		k_sleep(K_SECONDS(1));

		LOG_INF("buffer %u ms, packets %u, underruns %u",
			ring_buf_size_get(&jitter_buf) * 1000U / (SAMPLE_RATE_HZ * FRAME_BYTES),
			(uint32_t)atomic_get(&rx_packets), (uint32_t)atomic_get(&tx_underruns));
	}

	return 0;
}
