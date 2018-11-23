/* Spa ALSA Device
 *
 * Copyright © 2018 Wim Taymans
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>

#include <libudev.h>
#include <asoundlib.h>

#include <spa/support/log.h>
#include <spa/utils/type.h>
#include <spa/support/loop.h>
#include <spa/support/plugin.h>
#include <spa/monitor/device.h>

#define NAME  "alsa-device"

#define MAX_DEVICES	64

extern const struct spa_handle_factory spa_alsa_sink_factory;
extern const struct spa_handle_factory spa_alsa_source_factory;

static const char default_device[] = "hw:0";

struct props {
	char device[64];
};

static void reset_props(struct props *props)
{
	strncpy(props->device, default_device, 64);
}

struct impl {
	struct spa_handle handle;
	struct spa_device device;

	struct spa_log *log;
	struct spa_loop *main_loop;

	const struct spa_device_callbacks *callbacks;
	void *callbacks_data;

        snd_ctl_t *ctl_hndl;

	struct props props;
};

static const char *get_class(snd_pcm_info_t *pcminfo)
{
	switch (snd_pcm_info_get_class(pcminfo)) {
	case SND_PCM_CLASS_GENERIC:
		return "generic";
	case SND_PCM_CLASS_MULTI:
		return "multichannel";
	case SND_PCM_CLASS_MODEM:
		return "modem";
	case SND_PCM_CLASS_DIGITIZER:
		return "digitizer";
	default:
		return "unknown";
	}
}

static const char *get_subclass(snd_pcm_info_t *pcminfo)
{
	switch (snd_pcm_info_get_subclass(pcminfo)) {
	case SND_PCM_SUBCLASS_GENERIC_MIX:
		return "generic-mix";
	case SND_PCM_SUBCLASS_MULTI_MIX:
		return "multichannel-mix";
	default:
		return "unknown";
	}
}

static int emit_device(struct impl *this, snd_ctl_card_info_t *info, snd_pcm_info_t *pcminfo)
{
	struct spa_dict_item items[13];
	const struct spa_handle_factory *factory;
	char device_name[128];

	snprintf(device_name, 128, "%s,%d", this->props.device, snd_pcm_info_get_device(pcminfo));

	items[0] = SPA_DICT_ITEM_INIT("alsa.card.id",	      snd_ctl_card_info_get_id(info));
	items[1] = SPA_DICT_ITEM_INIT("alsa.device",          device_name);
	items[2] = SPA_DICT_ITEM_INIT("alsa.card.components", snd_ctl_card_info_get_components(info));
	items[3] = SPA_DICT_ITEM_INIT("alsa.card.driver",     snd_ctl_card_info_get_driver(info));
	items[4] = SPA_DICT_ITEM_INIT("alsa.card.name",       snd_ctl_card_info_get_name(info));
	items[5] = SPA_DICT_ITEM_INIT("alsa.card.longname",   snd_ctl_card_info_get_longname(info));
	items[6] = SPA_DICT_ITEM_INIT("alsa.card.mixername",  snd_ctl_card_info_get_mixername(info));
	items[7] = SPA_DICT_ITEM_INIT("device.name",          snd_ctl_card_info_get_id(info));
	items[8] = SPA_DICT_ITEM_INIT("alsa.pcm.id",          snd_pcm_info_get_id(pcminfo));
	items[9] = SPA_DICT_ITEM_INIT("alsa.pcm.name",        snd_pcm_info_get_name(pcminfo));
	items[10] = SPA_DICT_ITEM_INIT("alsa.pcm.subname",    snd_pcm_info_get_subdevice_name(pcminfo));
	items[11] = SPA_DICT_ITEM_INIT("alsa.pcm.class",      get_class(pcminfo));
	items[12] = SPA_DICT_ITEM_INIT("alsa.pcm.subclass",   get_subclass(pcminfo));

	if (snd_pcm_info_get_stream(pcminfo) == SND_PCM_STREAM_PLAYBACK)
		factory = &spa_alsa_sink_factory;
	else
		factory = &spa_alsa_source_factory;

	if (this->callbacks->add)
		this->callbacks->add(this->callbacks_data, 0,
			factory,
			SPA_TYPE_INTERFACE_Node,
			&SPA_DICT_INIT(items, 13));

	return 0;
}

static int emit_devices(struct impl *this)
{
	int err = 0, dev;
	snd_ctl_card_info_t *info;
	snd_pcm_info_t *pcminfo;

        spa_log_info(this->log, "open card %s", this->props.device);

        if ((err = snd_ctl_open(&this->ctl_hndl, this->props.device, 0)) < 0) {
                spa_log_error(this->log, "can't open control for card %s: %s",
                                this->props.device, snd_strerror(err));
                return err;
        }

	snd_ctl_card_info_alloca(&info);
	if ((err = snd_ctl_card_info(this->ctl_hndl, info)) < 0) {
		spa_log_error(this->log, "error hardware info: %s", snd_strerror(err));
		goto exit;
	}

        snd_pcm_info_alloca(&pcminfo);
	dev = -1;
	while (1) {
		if ((err = snd_ctl_pcm_next_device(this->ctl_hndl, &dev)) < 0) {
			spa_log_error(this->log, "error iterating devices: %s", snd_strerror(err));
			goto exit;
		}
		if (dev < 0)
			break;

		snd_pcm_info_set_device(pcminfo, dev);
		snd_pcm_info_set_subdevice(pcminfo, 0);

		snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_PLAYBACK);
		if ((err = snd_ctl_pcm_info(this->ctl_hndl, pcminfo)) < 0) {
			if (err != -ENOENT)
				spa_log_error(this->log, "error pcm info: %s", snd_strerror(err));
		}
		if (err >= 0)
			emit_device(this, info, pcminfo);

		snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_CAPTURE);
		if ((err = snd_ctl_pcm_info(this->ctl_hndl, pcminfo)) < 0) {
			if (err != -ENOENT)
				spa_log_error(this->log, "error pcm info: %s", snd_strerror(err));
		}
		if (err >= 0)
			emit_device(this, info, pcminfo);
	}

      exit:
        snd_ctl_close(this->ctl_hndl);

	return err;
}

static int impl_set_callbacks(struct spa_device *device,
			   const struct spa_device_callbacks *callbacks,
			   void *data)
{
	struct impl *this;

	spa_return_val_if_fail(device != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(device, struct impl, device);

	this->callbacks = callbacks;
	this->callbacks_data = data;

	if (callbacks)
		emit_devices(this);

	return 0;
}


static int impl_enum_params(struct spa_device *device,
			    uint32_t id, uint32_t *index,
			    const struct spa_pod *filter,
			    struct spa_pod **param,
			    struct spa_pod_builder *builder)
{
	return -ENOTSUP;
}

static int impl_set_param(struct spa_device *device,
			  uint32_t id, uint32_t flags,
			  const struct spa_pod *param)
{
	return -ENOTSUP;
}

static const struct spa_dict_item info_items[] = {
	{ "media.class", "Audio/Device" },
};

static const struct spa_dict info = {
	info_items,
	SPA_N_ELEMENTS(info_items)
};

static const struct spa_device impl_device = {
	SPA_VERSION_DEVICE,
	&info,
	impl_set_callbacks,
	impl_enum_params,
	impl_set_param,
};

static int impl_get_interface(struct spa_handle *handle, uint32_t type, void **interface)
{
	struct impl *this;

	spa_return_val_if_fail(handle != NULL, -EINVAL);
	spa_return_val_if_fail(interface != NULL, -EINVAL);

	this = (struct impl *) handle;

	if (type == SPA_TYPE_INTERFACE_Device)
		*interface = &this->device;
	else
		return -ENOENT;

	return 0;
}

static int impl_clear(struct spa_handle *handle)
{
	return 0;
}

static size_t
impl_get_size(const struct spa_handle_factory *factory,
	      const struct spa_dict *params)
{
	return sizeof(struct impl);
}

static int
impl_init(const struct spa_handle_factory *factory,
	  struct spa_handle *handle,
	  const struct spa_dict *info,
	  const struct spa_support *support,
	  uint32_t n_support)
{
	struct impl *this;
	const char *str;
	uint32_t i;

	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(handle != NULL, -EINVAL);

	handle->get_interface = impl_get_interface;
	handle->clear = impl_clear;

	this = (struct impl *) handle;

	for (i = 0; i < n_support; i++) {
		if (support[i].type == SPA_TYPE_INTERFACE_Log)
			this->log = support[i].data;
		else if (support[i].type == SPA_TYPE_INTERFACE_MainLoop)
			this->main_loop = support[i].data;
	}
	if (this->main_loop == NULL) {
		spa_log_error(this->log, "a main-loop is needed");
		return -EINVAL;
	}

	this->device = impl_device;

	reset_props(&this->props);

	if (info && (str = spa_dict_lookup(info, "alsa.card")))
		snprintf(this->props.device, 64, "hw:%d", atoi(str));

	return 0;
}

static const struct spa_interface_info impl_interfaces[] = {
	{SPA_TYPE_INTERFACE_Device,},
};

static int
impl_enum_interface_info(const struct spa_handle_factory *factory,
			 const struct spa_interface_info **info,
			 uint32_t *index)
{
	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(info != NULL, -EINVAL);
	spa_return_val_if_fail(index != NULL, -EINVAL);

	if (*index >= SPA_N_ELEMENTS(impl_interfaces))
		return 0;

	*info = &impl_interfaces[(*index)++];
	return 1;
}

const struct spa_handle_factory spa_alsa_device_factory = {
	SPA_VERSION_HANDLE_FACTORY,
	NAME,
	NULL,
	impl_get_size,
	impl_init,
	impl_enum_interface_info,
};