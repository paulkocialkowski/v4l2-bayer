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

#include <v4l2-params.h>

int main(int argc, char *argv[])
{
	struct v4l2_params params = { 0 };
	bool enable = !!(argc > 1);
	int ret;

	ret = v4l2_params_open(&params, "sun6i-isp", "sun6i-isp-params");
	if (ret)
		goto error;

	ret = v4l2_params_setup(&params);
	if (ret)
		goto error;

	ret = v4l2_params_start(&params);
	if (ret)
		goto error;

	ret = v4l2_params_prepare(&params, enable);
	if (ret)
		return ret;

	ret = v4l2_params_run(&params);
	if (ret)
		return ret;

	ret = v4l2_params_complete(&params);
	if (ret)
		return ret;

	ret = v4l2_params_stop(&params);
	if (ret)
		goto error;

	ret = v4l2_params_teardown(&params);
	if (ret)
		goto error;

	return 0;

error:
	return 1;
}
