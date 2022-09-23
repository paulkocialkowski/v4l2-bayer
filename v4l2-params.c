/*
 * Copyright (C) 2019-2020 Paul Kocialkowski <contact@paulk.fr>
 * Copyright (C) 2020 Bootlin
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <libudev.h>

#include <linux/videodev2.h>

#include <v4l2.h>
#include <v4l2-params.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

int v4l2_params_complete(struct v4l2_params *params)
{
	if (!params)
		return -EINVAL;

	params->state.index++;
	params->state.index %= params->state.count;

	return 0;
}

const struct sun6i_isp_params_config params_config_default = {
	.modules_used = SUN6I_ISP_MODULE_BAYER,

	.bayer = {
		.offset_r	= 32,
		.offset_gr	= 32,
		.offset_gb	= 32,
		.offset_b	= 32,

		.gain_r		= 256,
		.gain_gr	= 256,
		.gain_gb	= 256,
		.gain_b		= 256,

	},

	.bdnf = {
		.in_dis_min		= 8,
		.in_dis_max		= 16,

		.coefficients_g		= { 15, 4, 1 },
		.coefficients_rb	= { 15, 4 },
	},
};

int v4l2_params_prepare(struct v4l2_params *params, bool enable)
{
	struct sun6i_isp_params_config *config;
	struct v4l2_params_buffer *buffer;
	unsigned int index;

	if (!params)
		return -EINVAL;

	index = params->state.index;
	buffer = &params->state.buffers[index];
	config = buffer->config;

	*config = params_config_default;

	if (enable)
		config->modules_used |= SUN6I_ISP_MODULE_BDNF;

	return 0;
}

int v4l2_params_run(struct v4l2_params *params)
{
	struct v4l2_params_buffer *buffer;
	struct v4l2_buffer buffer_dequeue;
	unsigned int index;
	struct timeval timeout = { 0, 300000 };
	int ret;

	if (!params)
		return -EINVAL;

	index = params->state.index;
	buffer = &params->state.buffers[index];

	printf("queue-buffer: %d\n", buffer->buffer.index);
	ret = v4l2_buffer_queue(params->video_fd, &buffer->buffer);
	if (ret)
		return ret;

	ret = v4l2_poll(params->video_fd, &timeout);
	if (ret <= 0)
		printf("poll: %d\n", ret);

	v4l2_buffer_setup_base(&buffer_dequeue, params->type, params->memory,
			       0);

	do {
		ret = v4l2_buffer_dequeue(params->video_fd, &buffer_dequeue);
		if (ret && ret != -EAGAIN)
			return ret;
	} while (ret == -EAGAIN);

	printf("dequeue-buffer: %d\n", buffer_dequeue.index);

	return 0;
}

int v4l2_params_start(struct v4l2_params *params)
{
	struct v4l2_params_buffer *capture_buffer;
	int ret;

	if (!params || params->state.started)
		return -EINVAL;

	ret = v4l2_stream_on(params->video_fd, params->type);
	if (ret)
		return ret;

	params->state.started = true;

	return 0;
}

int v4l2_params_stop(struct v4l2_params *params)
{
	int ret;

	if (!params || !params->state.started)
		return -EINVAL;

	ret = v4l2_stream_off(params->video_fd, params->type);
	if (ret)
		return ret;

	do {
		struct v4l2_buffer buffer;

		v4l2_buffer_setup_base(&buffer, params->type, params->memory,
				       0);

		ret = v4l2_buffer_dequeue(params->video_fd, &buffer);
		if (ret && ret != -EAGAIN)
			break;
		else
			printf("unload-buffer: %d\n", buffer.index);
	} while (!ret || ret == -EAGAIN);

	params->state.started = false;

	return 0;
}

int v4l2_params_setup(struct v4l2_params *params)
{
	unsigned int count;
	unsigned int i;
	int ret;

	if (!params || params->state.up)
		return -EINVAL;

	/* Meta output format */

	v4l2_format_setup_base(&params->format, params->type);

	ret = v4l2_format_get(params->video_fd, &params->format);
	if (ret) {
		fprintf(stderr, "Failed to get output format\n");
		goto complete;
	}

	if (params->format.fmt.meta.dataformat !=
	    V4L2_META_FMT_SUN6I_ISP_PARAMS) {
		fprintf(stderr, "Wrong data format\n");
		goto complete;
	}

	/* Buffers */

	count = ARRAY_SIZE(params->state.buffers);

	ret = v4l2_buffers_request(params->video_fd, params->type,
				   params->memory, count);
	if (ret) {
		fprintf(stderr, "Failed to allocate capture buffers\n");
		goto error;
	}

	for (i = 0; i < count; i++) {
		struct v4l2_params_buffer *buffer = &params->state.buffers[i];

		buffer->params = params;

		buffer->config = malloc(sizeof(*buffer->config));

		v4l2_buffer_setup_base(&buffer->buffer, params->type,
				       params->memory, i);
		v4l2_buffer_setup_userptr(&buffer->buffer, buffer->config,
					  sizeof(*buffer->config));

		buffer->buffer.bytesused = sizeof(*buffer->config);
	}

	params->state.count = count;
	params->state.up = true;

	ret = 0;
	goto complete;

error:
	v4l2_buffers_destroy(params->video_fd, params->type, params->memory);

	/* TODO: free buffer->config */

complete:
	return ret;
}

int v4l2_params_teardown(struct v4l2_params *params)
{
	unsigned int i;
	int ret;

	if (!params || !params->state.up)
		return -EINVAL;

	v4l2_buffers_destroy(params->video_fd, params->type, params->memory);

	/* TODO: free buffer->config */

	params->state.up = false;

	return 0;
}

static int video_device_probe(struct v4l2_params *params, struct udev *udev,
			      struct udev_device *device, const char *driver,
			      const char *card)
{
	const char *path = udev_device_get_devnode(device);
	bool check;
	int video_fd;
	int ret;

	printf("Probe path %s\n", path);

	video_fd = open(path, O_RDWR | O_NONBLOCK);
	if (video_fd < 0) {
		ret = -errno;
		goto error;
	}

	ret = v4l2_capabilities_probe(video_fd, &params->capabilities,
				      (char *)&params->driver,
				      (char *)&params->card);
	if (ret) {
		fprintf(stderr, "Failed to probe V4L2 capabilities\n");
		goto error;
	}

	printf("Probed driver %s card %s\n", params->driver, params->card);

	if (driver && strcmp(driver, params->driver)) {
		ret = -EINVAL;
		goto error;
	}

	if (card && strcmp(card, params->card)) {
		ret = -EINVAL;
		goto error;
	}

	check = v4l2_capabilities_check(params->capabilities,
					V4L2_CAP_META_OUTPUT);
	if (check) {
		params->type = V4L2_BUF_TYPE_META_OUTPUT;
	} else {
		fprintf(stderr, "Missing V4L2 output support\n");
		ret = -ENODEV;
		goto error;
	}

	ret = v4l2_buffers_capabilities_probe(video_fd, params->type,
					      V4L2_MEMORY_USERPTR,
					      &params->capture_capabilities);
	if (ret) {
		fprintf(stderr, "Missing V4L2 memory type\n");
		goto error;
	}

	params->memory = V4L2_MEMORY_USERPTR;
	params->video_fd = video_fd;

	return 0;

error:
	if (video_fd >= 0)
		close(video_fd);

	return ret;
}

int v4l2_params_open(struct v4l2_params *params, const char *driver,
		     const char *card)
{
	struct udev *udev = NULL;
	struct udev_enumerate *enumerate = NULL;
	struct udev_list_entry *devices;
	struct udev_list_entry *entry;
	int ret;

	if (!params)
		return -EINVAL;

	params->video_fd = -1;

	udev = udev_new();
	if (!udev)
		goto error;

	enumerate = udev_enumerate_new(udev);
	if (!enumerate)
		goto error;

	udev_enumerate_add_match_subsystem(enumerate, "video4linux");
	udev_enumerate_scan_devices(enumerate);

	devices = udev_enumerate_get_list_entry(enumerate);

	udev_list_entry_foreach(entry, devices) {
		struct udev_device *device;
		const char *path;

		path = udev_list_entry_get_name(entry);
		if (!path)
			continue;

		device = udev_device_new_from_syspath(udev, path);
		if (!device)
			continue;

		ret = video_device_probe(params, udev, device, driver, card);

		udev_device_unref(device);

		if (!ret)
			break;
	}

	if (params->video_fd < 0) {
		fprintf(stderr, "Failed to open params video device\n");
		goto error;
	}

	ret = 0;
	goto complete;

error:
	if (params->video_fd) {
		close(params->video_fd);
		params->video_fd = -1;
	}

	ret = -1;

complete:
	if (enumerate)
		udev_enumerate_unref(enumerate);

	if (udev)
		udev_unref(udev);

	return ret;
}

void v4l2_params_close(struct v4l2_params *params)
{
	if (!params)
		return;

	if (params->video_fd > 0) {
		close(params->video_fd);
		params->video_fd = -1;
	}
}
