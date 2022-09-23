/*
 * Copyright (C) 2021 Bootlin
 */

#ifndef _V4L2_PARAMS_H_
#define _V4L2_PARAMS_H_

#include <linux/videodev2.h>

#include "sun6i-isp-config.h"

struct v4l2_params;

struct v4l2_params_buffer {
	struct v4l2_params *params;

	struct v4l2_buffer buffer;

	struct sun6i_isp_params_config *config;
};

struct v4l2_params_state {
	bool up;
	bool started;

	struct v4l2_params_buffer buffers[3];
	unsigned int count;
	unsigned int index;
};

struct v4l2_params {
	int video_fd;

	char driver[32];
	char card[32];

	unsigned int capture_capabilities;
	unsigned int capabilities;
	unsigned int type;
	unsigned int memory;

	struct v4l2_format format;
	struct v4l2_params_state state;
};

int v4l2_params_prepare(struct v4l2_params *params, bool enable);
int v4l2_params_complete(struct v4l2_params *params);
int v4l2_params_run(struct v4l2_params *params);
int v4l2_params_start(struct v4l2_params *params);
int v4l2_params_stop(struct v4l2_params *params);
int v4l2_params_setup_defaults(struct v4l2_params *params);
int v4l2_params_setup_dimensions(struct v4l2_params *params,
				 unsigned int width, unsigned int height);
int v4l2_params_setup_format(struct v4l2_params *params, uint32_t format);
int v4l2_params_setup(struct v4l2_params *params);
int v4l2_params_teardown(struct v4l2_params *params);
int v4l2_params_open(struct v4l2_params *params, const char *driver,
		     const char *card);
void v4l2_params_close(struct v4l2_params *params);

#endif
