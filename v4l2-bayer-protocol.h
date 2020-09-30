#ifndef _V4L2_BAYER_PROTOCOL_H_
#define _V4L2_BAYER_PROTOCOL_H_

#define V4L2_BAYER_SERVER_PORT		4321
#define V4L2_BAYER_FRAME_FRAGMENT_SIZE	1024

#define V4L2_BAYER_CAPTURE_REQUEST	0x1001

#define V4L2_BAYER_STREAM_START		0x2001
#define V4L2_BAYER_STREAM_STOP		0x2002

#define V4L2_BAYER_FRAME_FRAGMENT	0x3001

struct v4l2_bayer_message {
	unsigned int id;
	unsigned int length;
} __attribute__((packed));

struct v4l2_bayer_stream_start {
	unsigned int width;
	unsigned int height;
	unsigned int format;
} __attribute__((packed));

struct v4l2_bayer_capture_request {
	unsigned int width;
	unsigned int height;
	unsigned int format;
} __attribute__((packed));

struct v4l2_bayer_frame_fragment {
	unsigned int serial;
	unsigned int length;
} __attribute__((packed));

int v4l2_bayer_message_write(int fd, unsigned int id, unsigned int length);
int v4l2_bayer_data_write(int fd, void *buffer, unsigned int length);
int v4l2_bayer_data_write_poll(int fd,  struct timeval *timeout);
int v4l2_bayer_data_read(int fd, void *buffer, unsigned int length);
int v4l2_bayer_data_read_poll(int fd,  struct timeval *timeout);

#endif
