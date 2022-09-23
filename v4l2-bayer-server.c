#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>

#include <v4l2-bayer-protocol.h>
#include <v4l2-camera.h>
#include <v4l2.h>

struct v4l2_bayer_server {
	int server_fd;
	int client_fd;

	bool run;

	struct v4l2_camera camera;
};

int v4l2_bayer_server_open(struct v4l2_bayer_server *server)
{
	struct sockaddr_in server_addr = { 0 };
	int reuse = 1;
	int fd = -1;
	int ret;

	if (!server)
		return -EINVAL;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		ret = -errno;
		goto error;
	}

	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	if (ret) {
		ret = -errno;
		goto error;
	}

	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(V4L2_BAYER_SERVER_PORT);
	server_addr.sin_family = AF_INET;

	ret = bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (ret) {
		ret = -errno;
		goto error;
	}

	ret = listen(fd, 1);
	if (ret) {
		ret = -errno;
		goto error;
	}

	server->server_fd = fd;
	server->client_fd = -1;
	server->run = true;

	return 0;

error:
	if (fd >= 0)
		close(fd);

	return ret;
}

int v4l2_bayer_server_close(struct v4l2_bayer_server *server)
{
	if (!server || server->server_fd < 0)
		return -EINVAL;

	if (server->client_fd >= 0) {
		close(server->client_fd);
		server->client_fd = -1;
	}

	close(server->server_fd);
	server->server_fd = -1;

	return 0;
}

static int frame_fragments_write(struct v4l2_bayer_server *server,
				 struct v4l2_camera_buffer *buffer)
{
	struct v4l2_bayer_frame_fragment fragment = { 0 };
	unsigned int fragment_size = V4L2_BAYER_FRAME_FRAGMENT_SIZE;
	struct timeval timeout = { 0 };
	unsigned char *pointer = buffer->mmap_data[0];
	unsigned int written = 0;
	unsigned int length = 0;
	int ret;

	ret = v4l2_buffer_plane_length(&buffer->buffer, 0, &length);
	if (ret)
		return ret;

	printf("Tx frame size %u\n", length);

	do {
		unsigned int count = length - written;

		if (count > fragment_size)
			count = fragment_size;

		fragment.length = count;

		timeout.tv_sec = 0;
		timeout.tv_usec = 300000;

		ret = v4l2_bayer_data_write_poll(server->client_fd, &timeout);
		if (ret <= 0)
			return ret;

		ret = v4l2_bayer_message_write(server->client_fd,
					       V4L2_BAYER_FRAME_FRAGMENT,
					       sizeof(fragment) + count);
		if (ret < 0)
			return ret;

		ret = v4l2_bayer_data_write(server->client_fd, &fragment,
					    sizeof(fragment));
		if (ret < 0)
			return ret;

		ret = v4l2_bayer_data_write_poll(server->client_fd, &timeout);
		if (ret <= 0)
			return ret;

		ret = v4l2_bayer_data_write(server->client_fd, pointer, count);
		if (ret < count)
			return ret;

/*
		printf("Tx fragment %u length %u\n", fragment.serial,
		       fragment.length);
*/

		fragment.serial++;

		written += count;
		pointer += count;
	} while (written < length);

	return 0;
}

static int capture_request(struct v4l2_bayer_server *server)
{
	struct v4l2_bayer_capture_request request;
	struct v4l2_camera *camera = &server->camera;
	struct v4l2_camera_buffer *capture_buffer;
	unsigned int capture_index;
	bool started_already = true;
	int ret;

	ret = v4l2_bayer_data_read(server->client_fd, &request,
				   sizeof(request));
	if (ret <= 0)
		return ret;

	printf("Rx capture request size %ux%u, format %#x\n", request.width,
	       request.height, request.format);

	if (!camera->started)
		started_already = false;

	if (camera->up && (camera->setup.width != request.width ||
	                   camera->setup.height != request.height ||
	                   camera->setup.format != request.format)) {
		if (camera->started) {
			ret = v4l2_camera_stop(camera);
			if (ret)
				return ret;
		}

		ret = v4l2_camera_teardown(camera);
		if (ret)
			return ret;
	}

	if (!camera->up) {
		ret = v4l2_camera_setup_dimensions(camera, request.width,
						   request.height);
		if (ret)
			return ret;

		ret = v4l2_camera_setup_format(camera, request.format);
		if (ret)
			return ret;

		ret = v4l2_camera_setup(camera);
		if (ret)
			return ret;		
	}

	if (!camera->started) {
		ret = v4l2_camera_start(camera);
		if (ret)
			return ret;
	}

	ret = v4l2_camera_prepare(camera);
	if (ret)
		return ret;

	ret = v4l2_camera_run(camera);
	if (ret)
		return ret;

	capture_index = camera->capture_buffer_ready_index;
	capture_buffer = &camera->capture_buffers[capture_index];

	ret = v4l2_camera_complete(camera);
	if (ret)
		return ret;

	if (!started_already) {
		ret = v4l2_camera_stop(camera);
		if (ret)
			return ret;
	}

	ret = frame_fragments_write(server, capture_buffer);
	if (ret)
		return ret;

	return 0;
}

static int stream_start(struct v4l2_bayer_server *server)
{
	struct v4l2_bayer_stream_start stream;
	struct v4l2_camera *camera = &server->camera;
	int ret;

	ret = v4l2_bayer_data_read(server->client_fd, &stream, sizeof(stream));
	if (ret <= 0)
		return ret;

	printf("Stream size %ux%u, format %#x\n", stream.width, stream.height,
	       stream.format);

	if (camera->up && (camera->setup.width != stream.width ||
	                   camera->setup.height != stream.height ||
	                   camera->setup.format != stream.format)) {
		if (camera->started) {
			ret = v4l2_camera_stop(camera);
			if (ret)
				return ret;
		}

		ret = v4l2_camera_teardown(camera);
		if (ret)
			return ret;
	}

	if (!camera->up) {
		ret = v4l2_camera_setup_dimensions(camera, stream.width,
						   stream.height);
		if (ret)
			return ret;

		ret = v4l2_camera_setup_format(camera, stream.format);
		if (ret)
			return ret;

		ret = v4l2_camera_setup(camera);
		if (ret)
			return ret;
	}

	if (!camera->started) {
		ret = v4l2_camera_start(camera);
		if (ret)
			return ret;
	}

	printf("Stream started OK\n");

	return 0;
}

static int stream_stop(struct v4l2_bayer_server *server)
{
	struct v4l2_camera *camera = &server->camera;
	int ret;

	if (camera->started) {
		ret = v4l2_camera_stop(camera);
		if (ret)
			return ret;
	}

	printf("Stream stopped OK\n");

	return 0;
}

static int message_handle(struct v4l2_bayer_server *server)
{
	struct v4l2_bayer_message message;
	int ret;

	ret = v4l2_bayer_data_read(server->client_fd, &message,
				   sizeof(message));
	if (ret == 0)
		return -EPIPE;
	else if (ret < 0)
		return ret;

	switch (message.id) {
	case V4L2_BAYER_CAPTURE_REQUEST:
		if (message.length < sizeof(struct v4l2_bayer_capture_request))
			return -EINVAL;

		capture_request(server);
		break;
	case V4L2_BAYER_STREAM_START:
		if (message.length < sizeof(struct v4l2_bayer_stream_start))
			return -EINVAL;

		stream_start(server);
		break;
	case V4L2_BAYER_STREAM_STOP:
		stream_stop(server);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int v4l2_bayer_server_poll(struct v4l2_bayer_server *server)
{
	struct sockaddr_in client_addr = { 0 };
	fd_set read_fds;
	int ret;

	if (!server || server->server_fd < 0)
		return -EINVAL;

	if (server->client_fd < 0) {
		unsigned int client_addr_size = sizeof(client_addr);
		server->client_fd = accept(server->server_fd,
					   (struct sockaddr *)&client_addr,
					   &client_addr_size);
		if (server->client_fd < 0) {
			ret = -errno;
			goto error;
		}
	}

	ret = v4l2_bayer_data_read_poll(server->client_fd, NULL);
	if (ret <= 0) {
		ret = -errno;
		goto error;
	}

	ret = message_handle(server);
	if (ret)
		goto error;

	return 0;

error:
	if (server->client_fd >= 0) {
		close(server->client_fd);
		server->client_fd = -1;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	struct v4l2_bayer_server server = {
		.server_fd = -1,
		.client_fd = -1,
	};
	char *driver = NULL;
	unsigned int buffers_count = 2;
	unsigned int buffers_preload_count = 1;
	int option = 0;
	int ret;

	while (option != -1) {
		option = getopt(argc, argv, "d:c:p:s");
		if (option < 0)
			break;

		switch (option) {
		case 'd':
			driver = strdup(optarg);
			break;
		case 'c':
			buffers_count = atoi(optarg);
			break;
		case 'p':
			buffers_preload_count = atoi(optarg);
			break;
		}
	}

	ret = v4l2_bayer_server_open(&server);
	if (ret)
		goto error;

	ret = v4l2_camera_open(&server.camera, driver);
	if (ret)
		goto error;

	server.camera.capture_buffers_preload_count = buffers_preload_count;
	server.camera.capture_buffers_count = buffers_count;

	while (server.run)
		v4l2_bayer_server_poll(&server);

	if (server.camera.started) {
		ret = v4l2_camera_stop(&server.camera);
		if (ret)
			goto error;
	}

	if (server.camera.up) {
		ret = v4l2_camera_teardown(&server.camera);
		if (ret)
			goto error;
	}

	v4l2_camera_close(&server.camera);

	ret = v4l2_bayer_server_close(&server);
	if (ret)
		goto error;

	if (driver)
		free(driver);

	return 0;

error:
	if (driver)
		free(driver);

	return 1;
}
