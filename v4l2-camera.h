/*
 * Copyright (C) 2019-2020 Paul Kocialkowski <contact@paulk.fr>
 * Copyright (C) 2020 Bootlin
 */

#ifndef _V4L2_CAMERA_H_
#define _V4L2_CAMERA_H_

#include <linux/videodev2.h>

struct v4l2_camera;

struct v4l2_camera_buffer {
	struct v4l2_camera *camera;

	struct v4l2_buffer buffer;

	struct v4l2_plane planes[4];
	void *mmap_data[4];
	unsigned int planes_count;
};

struct v4l2_camera_setup {
	/* Dimensions */
	unsigned int width;
	unsigned int height;

	/* Format */
	uint32_t format;
};

struct v4l2_camera {
	int video_fd;

	char driver[32];
	char card[32];

	unsigned int capabilities;
	unsigned int memory;

	bool up;
	bool started;

	struct v4l2_camera_setup setup;

	unsigned int capture_type;
	unsigned int capture_capabilities;
	struct v4l2_format capture_format;
	struct v4l2_camera_buffer *capture_buffers;
	unsigned int capture_buffers_preload_count;
	unsigned int capture_buffers_count;
	unsigned int capture_buffers_index;
	unsigned int capture_buffer_ready_index;
};

int v4l2_camera_prepare(struct v4l2_camera *camera);
int v4l2_camera_complete(struct v4l2_camera *camera);
int v4l2_camera_run(struct v4l2_camera *camera);
int v4l2_camera_start(struct v4l2_camera *camera);
int v4l2_camera_stop(struct v4l2_camera *camera);
int v4l2_camera_setup_defaults(struct v4l2_camera *camera);
int v4l2_camera_setup_dimensions(struct v4l2_camera *camera,
				 unsigned int width, unsigned int height);
int v4l2_camera_setup_format(struct v4l2_camera *camera, uint32_t format);
int v4l2_camera_setup(struct v4l2_camera *camera);
int v4l2_camera_teardown(struct v4l2_camera *camera);
int v4l2_camera_open(struct v4l2_camera *camera, const char *driver);
void v4l2_camera_close(struct v4l2_camera *camera);

#endif
