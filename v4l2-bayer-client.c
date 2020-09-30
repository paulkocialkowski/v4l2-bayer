#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <netinet/in.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include <linux/videodev2.h>

#include <cairo.h>

#include <v4l2-bayer-protocol.h>

struct v4l2_bayer_client {
	int fd;

	void *raw_buffer;
	unsigned char *raw_pointer;
	unsigned int raw_length;

	void *rgb_buffer;
	unsigned int rgb_length;

	int dump_fd;
};

int v4l2_bayer_client_open(struct v4l2_bayer_client *client, char *host_name)
{
	struct sockaddr_in server_addr = { 0 };
	struct hostent *host;
	int fd = -1;
	int ret;

	if (!client || !host_name)
		return -EINVAL;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		ret = -errno;
		goto error;
	}

	host = gethostbyname(host_name);
	if (!host) {
		ret = -errno;
		goto error;
	}

	server_addr.sin_addr = *(struct in_addr *)host->h_addr;
	server_addr.sin_port = htons(V4L2_BAYER_SERVER_PORT);
	server_addr.sin_family = AF_INET;

	ret = connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (ret) {
		ret = -errno;
		goto error;
	}

	client->fd = fd;

	return 0;

error:
	if (fd >= 0)
		close(fd);

	return ret;
}

int v4l2_bayer_client_close(struct v4l2_bayer_client *client)
{
	if (!client || client->fd < 0)
		return -EINVAL;

	close(client->fd);
	client->fd = -1;

	return 0;
}

static int frame_fragment_process(struct v4l2_bayer_client *client,
				  void *buffer, unsigned int length)
{
	if (client->dump_fd >= 0)
		write(client->dump_fd, buffer, length);

	if (client->raw_pointer) {
		memcpy(client->raw_pointer, buffer, length);
		client->raw_pointer += length;
	}

	return 0;
}

static int frame_fragments_read(struct v4l2_bayer_client *client)
{
	struct v4l2_bayer_message message;
	struct v4l2_bayer_frame_fragment fragment;
	struct timeval timeout = { 0 };
	unsigned int length = V4L2_BAYER_FRAME_FRAGMENT_SIZE;
	void *buffer;
	int ret = -1;

	buffer = malloc(length);

	do {
		timeout.tv_sec = 2;
		timeout.tv_usec = 0;

		ret = v4l2_bayer_data_read_poll(client->fd, &timeout);
		if (ret <= 0)
			break;

		ret = v4l2_bayer_data_read(client->fd, &message,
					   sizeof(message));
		if (ret <= 0)
			goto complete;

		if (message.id != V4L2_BAYER_FRAME_FRAGMENT ||
		    message.length < sizeof(struct v4l2_bayer_frame_fragment))
			return -EINVAL;

		ret = v4l2_bayer_data_read(client->fd, &fragment,
					   sizeof(fragment));
		if (ret <= 0)
			goto complete;

		if (length < fragment.length) {
			length = fragment.length;
			buffer = realloc(buffer, length);
		}

		ret = v4l2_bayer_data_read_poll(client->fd, &timeout);
		if (ret <= 0)
			break;

		ret = v4l2_bayer_data_read(client->fd, buffer, fragment.length);
		if (ret <= 0)
			goto complete;

/*
		printf("Rx fragment %u length %u\n", fragment.serial,
		       fragment.length);
*/

		ret = frame_fragment_process(client, buffer, fragment.length);
		if (ret < 0)
			goto complete;
	} while (1);

	ret = 0;

complete:
	if (buffer)
		free(buffer);

	return ret;
}

static int capture_request(struct v4l2_bayer_client *client, unsigned int width,
			   unsigned int height, unsigned int format)
{
	struct v4l2_bayer_capture_request request = {
		.width = width,
		.height = height,
		.format = format,
	};
	int ret;

	ret = v4l2_bayer_message_write(client->fd, V4L2_BAYER_CAPTURE_REQUEST,
				       sizeof(request));
	if (ret < 0)
		return ret;

	ret = v4l2_bayer_data_write(client->fd, &request, sizeof(request));
	if (ret < 0)
		return ret;

	printf("Tx capture request size %ux%u, format %#x\n", request.width,
	       request.height, request.format);

	return 0;
}

static int stream_start(struct v4l2_bayer_client *client, unsigned int width,
		     unsigned int height, unsigned int format)
{
	struct v4l2_bayer_stream_start stream = {
		.width = width,
		.height = height,
		.format = format,
	};
	int ret;

	ret = v4l2_bayer_message_write(client->fd, V4L2_BAYER_STREAM_START,
				       sizeof(stream));
	if (ret < 0)
		return ret;

	ret = v4l2_bayer_data_write(client->fd, &stream, sizeof(stream));
	if (ret < 0)
		return ret;

	return 0;
}

static int stream_stop(struct v4l2_bayer_client *client)
{
	int ret;

	ret = v4l2_bayer_message_write(client->fd, V4L2_BAYER_STREAM_STOP, 0);
	if (ret < 0)
		return ret;

	return 0;
}

#include "bayer.c"

void image_write(char *path, void *rgb_data, unsigned int width, unsigned int height)
{
	cairo_surface_t *surface = NULL;

	surface = cairo_image_surface_create_for_data(rgb_data, CAIRO_FORMAT_RGB24, width, height, width * 4);
	if (!surface)
		return;

	cairo_surface_write_to_png(surface, path);

	cairo_surface_destroy(surface);
}

int main(int argc, char *argv[])
{
	struct v4l2_bayer_client client = {
		.dump_fd = -1,
		.fd = -1,
	};
	char *host_name = strdup("localhost");
	unsigned int width, height, format;
	unsigned int command;
	int option = 0;
	bool dump = false;
	int ret;

	width = 2592;
	height = 1944;
	format = V4L2_PIX_FMT_SBGGR8;
	command = V4L2_BAYER_CAPTURE_REQUEST;

	while (option != -1) {
		option = getopt(argc, argv, "w:h:f:r:");
		if (option < 0)
			break;

		switch (option) {
		case 'w':
			width = atoi(optarg);
			break;
		case 'h':
			height = atoi(optarg);
			break;
		case 'f':
			switch (atoi(optarg)) {
			case 8:
				format = V4L2_PIX_FMT_SBGGR8;
				break;
			case 10:
				format = V4L2_PIX_FMT_SBGGR10;
				break;
			default:
				goto error;
			}
			break;
		case 'r':
			host_name = strdup(optarg);
		}
	}

	if (optind < argc) {
		if (!strcmp(argv[optind], "request"))
			command = V4L2_BAYER_CAPTURE_REQUEST;
		else if (!strcmp(argv[optind], "stream-start"))
			command = V4L2_BAYER_STREAM_START;
		else if (!strcmp(argv[optind], "stream-stop"))
			command = V4L2_BAYER_STREAM_STOP;
		else
			printf("Invalid command, using default.\n");
	}

	ret = v4l2_bayer_client_open(&client, host_name);
	if (ret)
		goto error;

	free(host_name);

	switch (command) {
	case V4L2_BAYER_CAPTURE_REQUEST:
		switch (format) {
		case V4L2_PIX_FMT_SBGGR8:
			client.raw_length = width * height;
			break;
		case V4L2_PIX_FMT_SBGGR10:
			client.raw_length = width * height * 2;
			break;
		default:
			goto error;
		}
		client.raw_buffer = malloc(client.raw_length);
		client.raw_pointer = client.raw_buffer;

		client.rgb_length = width * height * 4;
		client.rgb_buffer = malloc(client.rgb_length);

		if (dump) {
			client.dump_fd = open("frame.raw", O_RDWR | O_CREAT | O_TRUNC,
					      0644);
			if (client.dump_fd < 0)
				goto error;
		}

		ret = capture_request(&client, width, height, format);
		if (ret)
			goto error;

		printf("Capture requested!\n");

		ret = frame_fragments_read(&client);
		if (ret)
			goto error;

		printf("Frame fragments read done!\n");

		bayer_convert(client.rgb_buffer, client.raw_buffer, client.raw_length,
			      width, height, format);

		printf("Bayer convert done!\n");

		image_write("frame.png", client.rgb_buffer, width, height);

		printf("Image write done!\n");

		if (dump)
			close(client.dump_fd);
		break;
	case V4L2_BAYER_STREAM_START:
		ret = stream_start(&client, width, height, format);
		if (ret)
			goto error;

		printf("Stream on requested!\n");
		break;
	case V4L2_BAYER_STREAM_STOP:
		ret = stream_stop(&client);
		if (ret)
			goto error;

		printf("Stream off requested!\n");
		break;
	}

	ret = v4l2_bayer_client_close(&client);
	if (ret)
		goto error;

	return 0;

error:
	return 1;
}
