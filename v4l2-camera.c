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
#include <v4l2-camera.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

int v4l2_camera_complete(struct v4l2_camera *camera)
{
	if (!camera)
		return -EINVAL;

	camera->capture_buffers_index++;
	camera->capture_buffers_index %= camera->capture_buffers_count;

	return 0;
}

int v4l2_camera_prepare(struct v4l2_camera *camera)
{
	if (!camera)
		return -EINVAL;

	return 0;
}

int v4l2_camera_run(struct v4l2_camera *camera)
{
	struct v4l2_camera_buffer *capture_buffer;
	unsigned int capture_index;
	struct v4l2_buffer buffer;
	struct timeval timeout = { 0, 300000 };
	int ret;

	if (!camera)
		return -EINVAL;

	capture_index = camera->capture_buffers_index;
	capture_buffer = &camera->capture_buffers[capture_index];

	printf("QBUF %d/%d\n", capture_index, capture_buffer->buffer.index);
	ret = v4l2_buffer_queue(camera->video_fd, &capture_buffer->buffer);
	if (ret)
		return ret;

	ret = v4l2_poll(camera->video_fd, &timeout);
	if (ret <= 0)
		printf("poll returned %d\n", ret);

	/* Two buffers back, offset by the enqueue in camera start. */
	capture_index = (camera->capture_buffers_index +
			 camera->capture_buffers_count - 2) %
			camera->capture_buffers_count;
	capture_buffer = &camera->capture_buffers[capture_index];

	v4l2_buffer_setup_base(&buffer, camera->capture_type, camera->memory, 0,
			       NULL, 0);
	
	do {
		ret = v4l2_buffer_dequeue(camera->video_fd,
					  &buffer);
		if (ret && ret != -EAGAIN)
			return ret;
	} while (ret == -EAGAIN);

	camera->capture_buffer_ready_index = capture_index;

	printf("DQBUF %d\n", buffer.index);

	if (buffer.index != capture_index)
		printf("Dequeued buffer is not at expected index: %u/%u\n",
		       capture_buffer->buffer.index, capture_index);

	return 0;
}

int v4l2_camera_start(struct v4l2_camera *camera)
{
	struct v4l2_camera_buffer *capture_buffer;
	unsigned int capture_index;
	int ret;

	if (!camera || camera->started)
		return -EINVAL;

	/* Queue two buffers in advance. */

	capture_index = camera->capture_buffers_index;
	capture_buffer = &camera->capture_buffers[capture_index];

	printf("QBUF %d\n", capture_index);
	ret = v4l2_buffer_queue(camera->video_fd, &capture_buffer->buffer);
	if (ret)
		return ret;

	camera->capture_buffers_index++;
	camera->capture_buffers_index %= camera->capture_buffers_count;

	capture_index = camera->capture_buffers_index;
	capture_buffer = &camera->capture_buffers[capture_index];

	printf("QBUF %d\n", capture_index);
	ret = v4l2_buffer_queue(camera->video_fd, &capture_buffer->buffer);
	if (ret)
		return ret;

	camera->capture_buffers_index++;
	camera->capture_buffers_index %= camera->capture_buffers_count;

	ret = v4l2_stream_on(camera->video_fd, camera->capture_type);
	if (ret)
		return ret;

	camera->started = true;

	return 0;
}

int v4l2_camera_stop(struct v4l2_camera *camera)
{
	int ret;

	if (!camera || !camera->started)
		return -EINVAL;

	ret = v4l2_stream_off(camera->video_fd, camera->capture_type);
	if (ret)
		return ret;

	do {
		struct v4l2_buffer buffer;

		v4l2_buffer_setup_base(&buffer, camera->capture_type,
				       camera->memory, 0, NULL, 0);

		ret = v4l2_buffer_dequeue(camera->video_fd,
					  &buffer);
		if (ret && ret != -EAGAIN)
			break;
		else
			printf("DQBUF %d\n", buffer.index);
	} while (ret == -EAGAIN);

	camera->started = false;

	return 0;
}

int v4l2_camera_buffer_setup(struct v4l2_camera_buffer *buffer,
			     unsigned int type, unsigned int index)
{
	struct v4l2_camera *camera;
	int ret;

	if (!buffer || !buffer->camera)
		return -EINVAL;

	camera = buffer->camera;

	v4l2_buffer_setup_base(&buffer->buffer, type, camera->memory, index,
			       buffer->planes, buffer->planes_count);

	ret = v4l2_buffer_query(camera->video_fd, &buffer->buffer);
	if (ret) {
		fprintf(stderr, "Failed to query buffer\n");
		goto complete;
	}

	if(camera->memory == V4L2_MEMORY_MMAP) {
		unsigned int i;

		for (i = 0; i < buffer->planes_count; i++) {
			unsigned int offset;
			unsigned int length;

			ret = v4l2_buffer_plane_offset(&buffer->buffer, i,
						       &offset);
			if (ret)
				goto complete;

			ret = v4l2_buffer_plane_length(&buffer->buffer, i,
						       &length);
			if (ret)
				goto complete;

			buffer->mmap_data[i] =
				mmap(NULL, length, PROT_READ | PROT_WRITE,
				     MAP_SHARED, camera->video_fd, offset);
			if (buffer->mmap_data[i] == MAP_FAILED) {
				ret = -errno;
				goto complete;
			}
		}
	}

	ret = 0;

complete:
	return ret;
}

int v4l2_camera_buffer_teardown(struct v4l2_camera_buffer *buffer)
{
	struct v4l2_camera *camera;

	if (!buffer || !buffer->camera)
		return -EINVAL;

	camera = buffer->camera;

	if(camera->memory == V4L2_MEMORY_MMAP) {
		unsigned int i;

		for (i = 0; i < buffer->planes_count; i++) {
			unsigned int length;

			if (!buffer->mmap_data[i] ||
			    buffer->mmap_data[i] == MAP_FAILED)
					continue;

			v4l2_buffer_plane_length(&buffer->buffer, i, &length);
			munmap(buffer->mmap_data[i], length);
		}
	}

	memset(buffer, 0, sizeof(*buffer));

	return 0;
}

int v4l2_camera_setup_defaults(struct v4l2_camera *camera)
{
	int ret;

	if (!camera)
		return -EINVAL;

	if (camera->up)
		return -EBUSY;

	ret = v4l2_camera_setup_dimensions(camera, 1280, 720);
	if (ret)
		return ret;

	ret = v4l2_camera_setup_format(camera, V4L2_PIX_FMT_NV12);
	if (ret)
		return ret;

	return 0;
}

int v4l2_camera_setup_dimensions(struct v4l2_camera *camera,
				 unsigned int width, unsigned int height)
{
	if (!camera || !width || !height)
		return -EINVAL;

	if (camera->up)
		return -EBUSY;

	camera->setup.width = width;
	camera->setup.height = height;

	return 0;
}

int v4l2_camera_setup_format(struct v4l2_camera *camera, uint32_t format)
{
	if (!camera)
		return -EINVAL;

	if (camera->up)
		return -EBUSY;

	camera->setup.format = format;

	return 0;
}

int v4l2_camera_setup(struct v4l2_camera *camera)
{
	unsigned int width, height;
	unsigned int buffers_count;
	uint32_t format;
	unsigned int i;
	int ret;

	if (!camera || camera->up)
		return -EINVAL;

	width = camera->setup.width;
	height = camera->setup.height;
	format = camera->setup.format;

	/* Capture format */

	v4l2_format_setup_pixel(&camera->capture_format, camera->capture_type,
				width, height, format);

	ret = v4l2_format_try(camera->video_fd, &camera->capture_format);
	if (ret) {
		fprintf(stderr, "Failed to try capture format\n");
		goto complete;
	}

	ret = v4l2_format_set(camera->video_fd, &camera->capture_format);
	if (ret) {
		fprintf(stderr, "Failed to set capture format\n");
		goto complete;
	}

	/* Capture buffers */

	buffers_count = ARRAY_SIZE(camera->capture_buffers);

	ret = v4l2_buffers_request(camera->video_fd, camera->capture_type,
				   camera->memory, buffers_count);
	if (ret) {
		fprintf(stderr, "Failed to allocate capture buffers\n");
		goto error;
	}

	for (i = 0; i < buffers_count; i++) {
		struct v4l2_camera_buffer *buffer = &camera->capture_buffers[i];

		buffer->camera = camera;

		if (v4l2_type_mplane_check(camera->capture_type))
			buffer->planes_count =
				camera->capture_format.fmt.pix_mp.num_planes;
		else
			buffer->planes_count = 1;

		ret = v4l2_camera_buffer_setup(buffer, camera->capture_type, i);
		if (ret) {
			fprintf(stderr, "Failed to setup capture buffer\n");
			goto error;
		}
	}

	camera->capture_buffers_count = buffers_count;

	camera->up = true;

	ret = 0;
	goto complete;

error:
	buffers_count = ARRAY_SIZE(camera->capture_buffers);

	for (i = 0; i < buffers_count; i++)
		v4l2_camera_buffer_teardown(&camera->capture_buffers[i]);

	v4l2_buffers_destroy(camera->video_fd, camera->capture_type,
			     camera->memory);

complete:
	return ret;
}

int v4l2_camera_teardown(struct v4l2_camera *camera)
{
	unsigned int buffers_count;
	unsigned int i;
	int ret;

	if (!camera || !camera->up)
		return -EINVAL;

	buffers_count = ARRAY_SIZE(camera->capture_buffers);

	for (i = 0; i < buffers_count; i++)
		v4l2_camera_buffer_teardown(&camera->capture_buffers[i]);

	v4l2_buffers_destroy(camera->video_fd, camera->capture_type,
			     camera->memory);

	camera->up = false;

	return 0;
}

static int video_device_probe(struct v4l2_camera *camera, struct udev *udev,
			      struct udev_device *device)
{
	const char *path = udev_device_get_devnode(device);
	bool check, mplane_check;
	int video_fd;
	int ret;

	video_fd = open(path, O_RDWR | O_NONBLOCK);
	if (video_fd < 0) {
		ret = -errno;
		goto error;
	}

	ret = v4l2_capabilities_probe(video_fd, &camera->capabilities,
				      (char *)&camera->driver,
				      (char *)&camera->card);
	if (ret) {
		fprintf(stderr, "Failed to probe V4L2 capabilities\n");
		goto error;
	}

	printf("Probed driver %s card %s\n", camera->driver, camera->card);

	mplane_check = v4l2_capabilities_check(camera->capabilities,
					       V4L2_CAP_VIDEO_CAPTURE_MPLANE);
	check = v4l2_capabilities_check(camera->capabilities,
					V4L2_CAP_VIDEO_CAPTURE);
	if (mplane_check) {
		camera->capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	} else if (check) {
		camera->capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	} else {
		fprintf(stderr, "Missing V4L2 capture support\n");
		ret = -ENODEV;
		goto error;
	}

	ret = v4l2_buffers_capabilities_probe(video_fd, camera->capture_type,
					      &camera->capture_capabilities);
	if (ret)
		goto error;

	camera->memory = V4L2_MEMORY_MMAP;
	camera->video_fd = video_fd;

	return 0;

error:
	if (video_fd >= 0)
		close(video_fd);

	return ret;
}

int v4l2_camera_open(struct v4l2_camera *camera)
{
	struct udev *udev = NULL;
	struct udev_enumerate *enumerate = NULL;
	struct udev_list_entry *devices;
	struct udev_list_entry *entry;
	int ret;

	if (!camera)
		return -EINVAL;

	camera->video_fd = -1;

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

		ret = video_device_probe(camera, udev, device);

		udev_device_unref(device);

		if (!ret)
			break;
	}

	if (camera->video_fd < 0) {
		fprintf(stderr, "Failed to open camera video device\n");
		goto error;
	}

	ret = 0;
	goto complete;

error:
	if (camera->video_fd) {
		close(camera->video_fd);
		camera->video_fd = -1;
	}

	ret = -1;

complete:
	if (enumerate)
		udev_enumerate_unref(enumerate);

	if (udev)
		udev_unref(udev);

	return ret;
}

void v4l2_camera_close(struct v4l2_camera *camera)
{
	if (!camera)
		return;

	if (camera->video_fd > 0) {
		close(camera->video_fd);
		camera->video_fd = -1;
	}
}
