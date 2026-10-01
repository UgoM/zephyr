/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <sample_usbd.h>

#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_uac2.h>
#include <zephyr/zbus/zbus.h>

#include <soc.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/mpipe_message.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>
#include <zephyr/mpipe/base/mpipe_queue.h>
#include <zephyr/mpipe/audio/mpipe_dac_sink.h>

LOG_MODULE_REGISTER(uac2_dac_sample, LOG_LEVEL_INF);

#define SPEAKER_OUT_TERMINAL_ID UAC2_ENTITY_ID(DT_NODELABEL(out_terminal))

#define SAMPLE_RATE            48000
/* Experiment: this board's oscillator runs ~2.27% fast, so asking the DAC
 * timer for this rate makes it tick at a real 48 kHz.
 */
#define DAC_CAPS_RATE          46935
#define SAMPLE_BIT_WIDTH       16
#define NUMBER_OF_CHANNELS     1
#define BYTES_PER_SAMPLE       (SAMPLE_BIT_WIDTH / 8)
#define FS_SAMPLES_PER_SOF     (SAMPLE_RATE / 1000)
#define MAX_SAMPLES_PER_SOF    FS_SAMPLES_PER_SOF
#define MAX_BLOCK_SIZE         (MAX_SAMPLES_PER_SOF * BYTES_PER_SAMPLE)

/* The UAC2 driver needs buffers that are safe for UDC DMA. Use a small
 * number of blocks, enough to cover the isochronous receive pipeline.
 */
#define USB_BUFFERS_COUNT     7
K_MEM_SLAB_DEFINE_STATIC(usb_rx_slab, ROUND_UP(MAX_BLOCK_SIZE, UDC_BUF_GRANULARITY),
			 USB_BUFFERS_COUNT, UDC_BUF_ALIGN);

/* Element IDs, unique within the pipeline */
enum {
	PIPE_ID,
	APP_SRC_ID,
	QUEUE_ID,
	DAC_SINK_ID,
};

static struct mpipe pipeline;
static struct mpipe_app_src app_src;
static struct mpipe_queue queue;
static struct mpipe_dac_sink dac_sink;

ZBUS_MSG_SUBSCRIBER_DEFINE(pipeline_sub);

static bool terminal_enabled;

#define FEEDBACK_BASE   (FS_SAMPLES_PER_SOF << 14)
/* Window over which delivered samples are compared against the DAC's appetite */
#define FEEDBACK_WINDOW_MS 256
/* Keep the correction inside the +/-12.5% the DAC can still follow */
#define FEEDBACK_MARGIN (FEEDBACK_BASE / 8)
/* What the DAC actually drains: its timer divides 200 MHz by 4166 */
#define DAC_RATE_HZ 48007

static uint32_t feedback_value = FEEDBACK_BASE;
static atomic_t feedback_samples;


/* Temporary instrumentation: account for every sample the host hands us. */
static atomic_t stat_rx_pkt;
static atomic_t stat_rx_bytes;
static atomic_t stat_push_drop;
static atomic_t stat_nobuf;
static atomic_t stat_zlp;
static atomic_t stat_sof;

static void stats_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(stats_work, stats_handler);

static void stats_handler(struct k_work *work)
{
	uint32_t pkt = (uint32_t)atomic_clear(&stat_rx_pkt);
	uint32_t bytes = (uint32_t)atomic_clear(&stat_rx_bytes);
	uint32_t drop = (uint32_t)atomic_clear(&stat_push_drop);
	uint32_t nobuf = (uint32_t)atomic_clear(&stat_nobuf);
	uint32_t zlp = (uint32_t)atomic_clear(&stat_zlp);
	uint32_t sof = (uint32_t)atomic_clear(&stat_sof);

	ARG_UNUSED(work);

	LOG_INF("usb: %u sof/s, %u pkt/s, %u samples/s, drop %u, nobuf %u, zlp %u, fb %u", sof,
		pkt, bytes / BYTES_PER_SAMPLE, drop, nobuf, zlp, feedback_value);

	(void)k_work_schedule(&stats_work, K_SECONDS(1));
}

/* Explicit feedback in Q10.14 format: the nominal 48 samples per Full-Speed
 * SOF frame for 48 kHz. Asking for exactly that is not enough. The DAC is
 * paced by its own timer rather than by SOF, and a host may skip frames
 * outright, so the delivered rate drifts below what the DAC drains and every
 * missing sample becomes a replayed block. The regulator below counts the
 * samples that actually arrive over a window of frames and steers the reported
 * rate to close the gap.
 */
static uint32_t uac2_feedback_cb(const struct device *dev, uint8_t terminal,
				 void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(terminal);
	ARG_UNUSED(user_data);

	return feedback_value;
}

static void feedback_reset(void)
{
	feedback_value = FEEDBACK_BASE;
	(void)atomic_clear(&feedback_samples);
}

static void uac2_sof_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	atomic_inc(&stat_sof);
}

/*
 * The SOF callback cannot be the time base: it only fires for frames the host
 * actually transfers, so a host that skips frames looks perfectly on rate. The
 * kernel clock is driven by the same oscillator as the DAC timer, which makes
 * it an independent reference for how many samples the DAC has drained.
 */
static void feedback_timer_cb(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	uint32_t got = (uint32_t)atomic_clear(&feedback_samples);

	if (!terminal_enabled) {
		return;
	}

	int32_t want = (DAC_RATE_HZ * FEEDBACK_WINDOW_MS) / 1000;
	int32_t err = want - (int32_t)got;
	/*
	 * err samples short over a window of FEEDBACK_WINDOW_MS frames is
	 * err/FEEDBACK_WINDOW_MS samples short per frame. Apply half of it so
	 * the loop settles instead of ringing.
	 */
	int32_t adj = (err << 13) / FEEDBACK_WINDOW_MS;
	int32_t next = (int32_t)feedback_value + adj;

	feedback_value = (uint32_t)CLAMP(next, (int32_t)(FEEDBACK_BASE - FEEDBACK_MARGIN),
					 (int32_t)(FEEDBACK_BASE + FEEDBACK_MARGIN));
}

static K_TIMER_DEFINE(feedback_timer, feedback_timer_cb, NULL);

static void uac2_terminal_update_cb(const struct device *dev, uint8_t terminal,
				    bool enabled, bool microframes,
				    void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(microframes);
	ARG_UNUSED(user_data);

	__ASSERT_NO_MSG(terminal == SPEAKER_OUT_TERMINAL_ID);

	feedback_reset();
	terminal_enabled = enabled;

	if (enabled) {
		k_timer_start(&feedback_timer, K_MSEC(FEEDBACK_WINDOW_MS),
			      K_MSEC(FEEDBACK_WINDOW_MS));
	} else {
		k_timer_stop(&feedback_timer);
	}
	LOG_INF("Output terminal %s", enabled ? "enabled" : "disabled");
}

static void *uac2_get_recv_buf(const struct device *dev, uint8_t terminal,
			       uint16_t size, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(terminal);
	ARG_UNUSED(user_data);
	void *buf = NULL;
	int ret;

	ret = k_mem_slab_alloc(&usb_rx_slab, &buf, K_NO_WAIT);
	if (ret != 0) {
		atomic_inc(&stat_nobuf);
		buf = NULL;
	}

	return buf;
}

static void uac2_data_recv_cb(const struct device *dev, uint8_t terminal,
			      void *buf, uint16_t size, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(terminal);
	ARG_UNUSED(user_data);

	if (size == 0U) {
		atomic_inc(&stat_zlp);
	}

	/* A zero-length packet is how the host skips a frame: nothing to play */
	if (terminal_enabled && (size > 0U)) {
		/* Runs in the USB context: never block, drop when the pipeline
		 * cannot take the payload right away.
		 */
		atomic_inc(&stat_rx_pkt);
		atomic_add(&stat_rx_bytes, (atomic_val_t)size);
		atomic_add(&feedback_samples, (atomic_val_t)(size / BYTES_PER_SAMPLE));

		if (mpipe_app_src_push(&app_src, buf, size, K_NO_WAIT) != 0) {
			atomic_inc(&stat_push_drop);
		}
	}

	k_mem_slab_free(&usb_rx_slab, buf);
}

static void uac2_buf_release_cb(const struct device *dev, uint8_t terminal,
				void *buf, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(terminal);
	ARG_UNUSED(buf);
	ARG_UNUSED(user_data);
}

static struct uac2_ops usb_audio_ops = {
	.sof_cb = uac2_sof_cb,
	.terminal_update_cb = uac2_terminal_update_cb,
	.get_recv_buf = uac2_get_recv_buf,
	.data_recv_cb = uac2_data_recv_cb,
	.buf_release_cb = uac2_buf_release_cb,
	.feedback_cb = uac2_feedback_cb,
};

/* app_src → queue → dac_sink, the queue giving the DAC its own thread so a
 * write waiting on a free DMA block never holds the USB context.
 */
static int pipeline_setup(void)
{
	const struct device *codec = DEVICE_DT_GET(DT_ALIAS(codec0));
	enum mpipe_base_queue_leak leak = MPIPE_BASE_QUEUE_LEAK_OLDEST;
	struct mpipe_structure caps;
	int ret;

	if (!device_is_ready(codec)) {
		LOG_ERR("Codec device %s is not ready", codec->name);
		return -ENODEV;
	}

	ret = mpipe_pipeline_init(&pipeline, PIPE_ID);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_app_src_init(&app_src, APP_SRC_ID);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_queue_init(&queue, QUEUE_ID);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_dac_sink_init(&dac_sink, DAC_SINK_ID);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_structure_init_fields(&caps, MPIPE_MEDIA_AUDIO_PCM,
					  MPIPE_CAPS_SAMPLE_RATE, MPIPE_TYPE_UINT, DAC_CAPS_RATE,
					  MPIPE_CAPS_BITWIDTH, MPIPE_TYPE_UINT, SAMPLE_BIT_WIDTH,
					  MPIPE_CAPS_NUM_OF_CHANNEL, MPIPE_TYPE_UINT,
					  NUMBER_OF_CHANNELS,
					  MPIPE_CAPS_INTERLEAVED, MPIPE_TYPE_BOOLEAN, true,
					  MPIPE_CAPS_END);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&app_src,
					  MPIPE_PROP_BASE_APP_SRC_CAPS, &caps,
					  MPIPE_PROP_LIST_END);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&queue,
					  MPIPE_PROP_BASE_QUEUE_LEAK, &leak,
					  MPIPE_PROP_LIST_END);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&dac_sink,
					  MPIPE_PROP_AUDIO_DAC_SINK_CODEC, codec,
					  MPIPE_PROP_LIST_END);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_bin_add((struct mpipe_bin *)&pipeline, (struct mpipe_element *)&app_src,
			    (struct mpipe_element *)&queue, (struct mpipe_element *)&dac_sink,
			    NULL);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_element_link((struct mpipe_element *)&app_src, (struct mpipe_element *)&queue,
				 (struct mpipe_element *)&dac_sink, NULL);
	if (ret != 0) {
		return ret;
	}

	ret = zbus_chan_add_obs(mpipe_element_get_bus_chan((struct mpipe_element *)&pipeline),
				&pipeline_sub, K_FOREVER);
	if (ret != 0) {
		return ret;
	}

	return mpipe_element_set_state((struct mpipe_element *)&pipeline, MPIPE_STATE_PLAYING);
}

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(uac2_speaker));
	struct usbd_context *sample_usbd;
	const struct zbus_channel *chan;
	struct mpipe_message msg;
	int ret;

	ret = pipeline_setup();
	if (ret != 0) {
		LOG_ERR("Pipeline setup failed: %d", ret);
		return ret;
	}

	usbd_uac2_set_ops(dev, &usb_audio_ops, NULL);

	sample_usbd = sample_usbd_init_device(NULL);
	if (sample_usbd == NULL) {
		return -ENODEV;
	}

	ret = usbd_enable(sample_usbd);
	if (ret != 0) {
		return ret;
	}

	(void)k_work_schedule(&stats_work, K_SECONDS(1));

	LOG_INF("clk: sysclk %u Hz, hclk %u Hz, pclk1 %u Hz, cyc/s %u", HAL_RCC_GetSysClockFreq(),
		HAL_RCC_GetHCLKFreq(), HAL_RCC_GetPCLK1Freq(),
		sys_clock_hw_cycles_per_sec());
	LOG_INF("rcc: CR %08x CFGR %08x PLLCKSELR %08x PLL1DIVR %08x D1CFGR %08x D2CFGR %08x",
		RCC->CR, RCC->CFGR, RCC->PLLCKSELR, RCC->PLL1DIVR, RCC->D1CFGR, RCC->D2CFGR);

	LOG_INF("UAC2 DAC sample started: %d Hz, %d-bit, %d channel(s), DMA-paced DAC on PA4",
		SAMPLE_RATE, SAMPLE_BIT_WIDTH, NUMBER_OF_CHANNELS);

	while (zbus_sub_wait_msg(&pipeline_sub, &chan, &msg, K_FOREVER) == 0) {
		if (msg.type == MPIPE_MESSAGE_ERROR) {
			LOG_ERR("Pipeline error %d from element %u in domain %u", msg.code,
				(msg.origin != NULL) ? msg.origin->object.id : UINT8_MAX,
				msg.domain);
		}
	}

	return 0;
}
