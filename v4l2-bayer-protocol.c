#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <v4l2-bayer-protocol.h>

static int chunks_write(int fd, void *buffer, unsigned int length)
{
	unsigned char *pointer = buffer;
	unsigned int written = 0;
	int ret;

	do {
		unsigned int count = length - written;

		ret = write(fd, pointer, count);
		if (ret <= 0)
			return -errno;

		count = (unsigned int)ret;
		written += count;
		pointer += count;
	} while (written < length);

	return (int)written;
}

static int chunks_read(int fd, void *buffer, unsigned int length)
{
	unsigned char *pointer = buffer;
	unsigned int written = 0;
	int ret;

	do {
		unsigned int count = length - written;

		ret = read(fd, pointer, count);
		if (ret <= 0)
			return -errno;

		count = (unsigned int)ret;
		written += count;
		pointer += count;
	} while (written < length);

	return (int)written;
}

int v4l2_bayer_message_write(int fd, unsigned int id, unsigned int length)
{
	struct v4l2_bayer_message message = {
		.id = id,
		.length = length,
	};

	return chunks_write(fd, &message, sizeof(message));
}

int v4l2_bayer_data_write(int fd, void *buffer, unsigned int length)
{
	return chunks_write(fd, buffer, length);
}

int v4l2_bayer_data_write_poll(int fd,  struct timeval *timeout)
{
	fd_set write_fds;
	int ret;

	FD_ZERO(&write_fds);
	FD_SET(fd, &write_fds);

	ret = select(fd + 1, NULL, &write_fds, NULL, timeout);
	if (ret < 0)
		return -errno;

	if (!FD_ISSET(fd, &write_fds))
		return 0;

	return ret;
}

int v4l2_bayer_data_read(int fd, void *buffer, unsigned int length)
{
	return chunks_read(fd, buffer, length);
}

int v4l2_bayer_data_read_poll(int fd,  struct timeval *timeout)
{
	fd_set read_fds;
	int ret;

	FD_ZERO(&read_fds);
	FD_SET(fd, &read_fds);

	ret = select(fd + 1, &read_fds, NULL, NULL, timeout);
	if (ret < 0)
		return -errno;

	if (!FD_ISSET(fd, &read_fds))
		return 0;

	return ret;
}
