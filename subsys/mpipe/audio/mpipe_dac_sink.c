/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/audio/codec.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>

#include <zephyr/mpipe/mpipe_pad.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_value.h>
#include <zephyr/mpipe/audio/mpipe_dac_sink.h>

LOG_MODULE_REGISTER(mpipe_dac_sink, CONFIG_MPIPE_LOG_LEVEL);

/* A block is free again at every codec done callback */
static void mpipe_dac_sink_tx_done(const struct device *dev, void *user_data)
{
	struct mpipe_dac_sink *dac_sink = user_data;

	ARG_UNUSED(dev);

	k_sem_give(&dac_sink->block_free);
}

static int mpipe_dac_sink_play(struct mpipe_dac_sink *dac_sink, struct net_buf *buf)
{
	int ret;

	if (buf->len == 0U) {
		return 0;
	}

	ret = k_sem_take(&dac_sink->block_free,
			 K_MSEC(CONFIG_MPIPE_AUDIO_DAC_SINK_TIMEOUT_MS));
	if (ret != 0) {
		LOG_WRN_RATELIMIT("codec did not free a block in time, dropping %u bytes",
				  buf->len);
		return -EAGAIN;
	}

	ret = audio_codec_write(dac_sink->codec, buf->data, buf->len);
	if (ret != 0) {
		LOG_WRN_RATELIMIT("audio_codec_write of %u bytes failed (%d)", buf->len, ret);
	}

	return ret;
}

static int mpipe_dac_sink_chain_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
				   struct net_buf **out_buf)
{
	struct mpipe_dac_sink *dac_sink = (struct mpipe_dac_sink *)pad->object.container;
	struct net_buf *cur = in_buf;
	struct net_buf *next;

	*out_buf = NULL;

	/* A chain carries independent buffers of the same caps: play each on its own */
	while (cur != NULL) {
		next = cur->frags;
		cur->frags = NULL;
		(void)mpipe_dac_sink_play(dac_sink, cur);
		net_buf_unref(cur);
		cur = next;
	}

	return 0;
}

/* The one capability the element accepts is what the codec was told to play */
static int mpipe_dac_sink_enum_caps(struct mpipe_pad *pad, uint32_t index,
				    const struct mpipe_structure *filter,
				    struct mpipe_structure *out)
{
	struct mpipe_dac_sink *dac_sink = (struct mpipe_dac_sink *)pad->object.container;

	if (index > 0U) {
		return -ENOENT;
	}

	return mpipe_pad_enum_filter(&dac_sink->caps, filter, out);
}

static uint32_t mpipe_dac_sink_field(const struct mpipe_structure *caps, uint8_t field_id,
				     uint32_t fallback)
{
	const struct mpipe_value *value = mpipe_structure_get_value(caps, field_id);

	if ((value == NULL) || (value->type != MPIPE_TYPE_UINT)) {
		return fallback;
	}

	return mpipe_value_get_uint(value);
}

/* Negotiation settled: hand the format to the codec */
static int mpipe_dac_sink_set_caps(struct mpipe_sink *sink, const struct mpipe_structure *caps)
{
	struct mpipe_dac_sink *dac_sink = (struct mpipe_dac_sink *)sink;
	struct audio_codec_cfg cfg = {0};
	int ret;

	if (dac_sink->codec == NULL) {
		LOG_ERR("No codec device set");
		return -ENODEV;
	}

	if (caps->media_type_id != MPIPE_MEDIA_AUDIO_PCM) {
		LOG_ERR("Media type %u is not PCM audio", caps->media_type_id);
		return -ENOTSUP;
	}

	cfg.dai_type = AUDIO_DAI_TYPE_PCM;
	cfg.dai_cfg.pcm.samplerate = mpipe_dac_sink_field(caps, MPIPE_CAPS_SAMPLE_RATE, 0);
	cfg.dai_cfg.pcm.pcm_width = mpipe_dac_sink_field(caps, MPIPE_CAPS_BITWIDTH, 0);
	cfg.dai_cfg.pcm.channels = mpipe_dac_sink_field(caps, MPIPE_CAPS_NUM_OF_CHANNEL, 0);
	cfg.dai_cfg.pcm.block_size = CONFIG_MPIPE_AUDIO_DAC_SINK_BLOCK_SIZE;

	ret = audio_codec_configure(dac_sink->codec, &cfg);
	if (ret != 0) {
		LOG_ERR("audio_codec_configure failed (%d)", ret);
		return ret;
	}

	ret = audio_codec_register_done_callback(dac_sink->codec, mpipe_dac_sink_tx_done, dac_sink,
						 NULL, NULL);
	if (ret != 0) {
		LOG_ERR("audio_codec_register_done_callback failed (%d)", ret);
		return ret;
	}

	LOG_DBG("codec configured: %u Hz, %u bits, %u channels",
		cfg.dai_cfg.pcm.samplerate, cfg.dai_cfg.pcm.pcm_width, cfg.dai_cfg.pcm.channels);

	return mpipe_pad_set_caps(&sink->sink_pad, caps);
}

static int mpipe_dac_sink_set_property(struct mpipe_object *obj, uint32_t key, const void *val)
{
	struct mpipe_dac_sink *dac_sink = (struct mpipe_dac_sink *)obj;

	if (val == NULL) {
		return -EINVAL;
	}

	switch (key) {
	case MPIPE_PROP_AUDIO_DAC_SINK_CODEC:
		dac_sink->codec = (const struct device *)val;
		return 0;
	case MPIPE_PROP_AUDIO_DAC_SINK_CAPS:
		dac_sink->caps = *(const struct mpipe_structure *)val;
		return 0;
	default:
		LOG_ERR("Property %d is unknown", key);
		return -ENOTSUP;
	}
}

static int mpipe_dac_sink_get_property(struct mpipe_object *obj, uint32_t key, void *val)
{
	struct mpipe_dac_sink *dac_sink = (struct mpipe_dac_sink *)obj;

	if (val == NULL) {
		return -EINVAL;
	}

	switch (key) {
	case MPIPE_PROP_AUDIO_DAC_SINK_CODEC:
		*(const struct device **)val = dac_sink->codec;
		return 0;
	case MPIPE_PROP_AUDIO_DAC_SINK_CAPS:
		*(const struct mpipe_structure **)val = &dac_sink->caps;
		return 0;
	default:
		LOG_ERR("Property %d is unknown", key);
		return -ENOTSUP;
	}
}

static int mpipe_dac_sink_change_state(struct mpipe_element *element,
				       enum mpipe_state_change transition)
{
	struct mpipe_dac_sink *dac_sink = (struct mpipe_dac_sink *)element;
	int ret;

	switch (transition) {
	case MPIPE_STATE_CHANGE_READY_TO_PAUSED:
		if (!device_is_ready(dac_sink->codec)) {
			LOG_ERR("Codec device is not ready");
			return -ENODEV;
		}
		break;
	case MPIPE_STATE_CHANGE_PAUSED_TO_PLAYING:
		k_sem_reset(&dac_sink->block_free);

		ret = audio_codec_start(dac_sink->codec, AUDIO_DAI_DIR_TX);
		if (ret != 0) {
			LOG_ERR("audio_codec_start failed (%d)", ret);
			return ret;
		}

		dac_sink->playing = true;
		break;
	case MPIPE_STATE_CHANGE_PLAYING_TO_PAUSED:
		if (dac_sink->playing) {
			(void)audio_codec_stop(dac_sink->codec, AUDIO_DAI_DIR_TX);
			dac_sink->playing = false;
		}
		break;
	default:
		break;
	}

	return mpipe_sink_change_state(element, transition);
}

int mpipe_dac_sink_init(struct mpipe_dac_sink *dac_sink, uint8_t id)
{
	__ASSERT_NO_MSG(dac_sink != NULL);

	struct mpipe_element *self = &dac_sink->sink.element;
	int ret = mpipe_sink_init(&dac_sink->sink, id);

	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "dac_sink");

	self->object.set_property = mpipe_dac_sink_set_property;
	self->object.get_property = mpipe_dac_sink_get_property;
	self->change_state = mpipe_dac_sink_change_state;

	ret = mpipe_structure_init_fields(&dac_sink->caps, MPIPE_MEDIA_AUDIO_PCM,
					  MPIPE_CAPS_SAMPLE_RATE, MPIPE_TYPE_UINT_RANGE,
					  8000, 96000, 1,
					  MPIPE_CAPS_BITWIDTH, MPIPE_TYPE_UINT, 16,
					  MPIPE_CAPS_NUM_OF_CHANNEL, MPIPE_TYPE_UINT, 1,
					  MPIPE_CAPS_INTERLEAVED, MPIPE_TYPE_BOOLEAN, true,
					  MPIPE_CAPS_END);
	if (ret != 0) {
		return ret;
	}

	dac_sink->sink.sink_pad.enum_caps_fn = mpipe_dac_sink_enum_caps;
	dac_sink->sink.sink_pad.chain_fn = mpipe_dac_sink_chain_fn;
	dac_sink->sink.set_caps = mpipe_dac_sink_set_caps;

	dac_sink->codec = NULL;
	dac_sink->playing = false;

	k_sem_init(&dac_sink->block_free, 0, 1);

	return 0;
}
