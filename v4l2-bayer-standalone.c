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

#include <v4l2-camera.h>

struct v4l2_bayer_standalone {
	struct v4l2_camera camera;

	void *raw_buffer;
	unsigned char *raw_pointer;
	unsigned int raw_length;

	void *rgb_buffer;
	unsigned int rgb_length;

	int dump_fd;
};

static inline uint32_t pixel_pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	uint32_t color = 0;

	color |= b << 0;
	color |= g << 8;
	color |= r << 16;
	color |= a << 24;

	return color;
}

static inline uint8_t clip(float v)
{
	if (v > 255.)
		return 255;
	else
		return (uint8_t)v;
}

static uint8_t interpol_r(void *raw_data, unsigned int width,
			  unsigned int height, unsigned int x, unsigned int y)
{
	uint16_t *samples = raw_data;
	float value;
	float range_max = (1 << 10) - 1;
	unsigned int xp, yp;
	unsigned int xn, yn;

	if ((x % 2 == 1) && (y % 2 == 1)) {
		value = samples[y * width + x];
		goto ret;
	}

	if (x < 2 || x >= (width - 1))
		return 0;
	if (y < 2 || y >= (height - 1))
		return 0;

	xp = (x - 2) | 1;
	yp = (y - 2) | 1;

	xn = x | 1;
	yn = y | 1;

//	value = (samples[yp * width + xp] + samples[yn * width + xn]) / 2.;
	value = samples[yp * width + xp];

ret:
	return clip(255. * value / range_max);
}

static uint8_t interpol_g(void *raw_data, unsigned int width,
			  unsigned int height, unsigned int x, unsigned int y)
{
	uint16_t *samples = raw_data;
	float value;
	float range_max = (1 << 10) - 1;
	unsigned int xp, yp;
	unsigned int xn, yn;

	if (((x % 2 == 1) && (y % 2 == 0)) || ((x % 2) == 0 && (y % 2 == 1))) {
		value = samples[y * width + x];
		goto ret;
	}

	if (x < 2 || x >= (width - 1))
		return 0;
	if (y < 2 || y >= (height - 1))
		return 0;

	xp = x - 1;
	yp = y;

	xn = x + 1;
	yn = y;

//	value = (samples[yp * width + xp] + samples[yn * width + xn]) / 2.;
	value = samples[yp * width + xp];

ret:
	return clip(255. * value / range_max);
}

static uint8_t interpol_b(void *raw_data, unsigned int width,
			  unsigned int height, unsigned int x, unsigned int y)
{
	uint16_t *samples = raw_data;
	float value;
	float range_max = (1 << 10) - 1;
	unsigned int xp, yp;
	unsigned int xn, yn;

	if (x % 2 == 0 && y % 2 == 0) {
		value = samples[y * width + x];
		goto ret;
	}

	if (x < 2 || x >= (width - 1))
		return 0;
	if (y < 2 || y >= (height - 1))
		return 0;

	xp = x & ~1;
	yp = y & ~1;

	xn = x & ~1;
	yn = y & ~1;

//	value = (samples[yp * width + xp] + samples[yn * width + xn]) / 2.;
	value = samples[yp * width + xp];

ret:
	return clip(255. * value / range_max);
}

int bayer_10_convert(void *rgb_data, void *raw_data, unsigned int length,
		     unsigned int width, unsigned int height)
{
	unsigned int x, y;
	uint16_t *samples = raw_data;
	uint32_t *pixels = rgb_data;
	float range_max = (1 << 10) - 1;
	uint8_t r = 0, g = 0, b = 0;
	// BGGR

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			float sample_range;
			uint8_t v;
/*
			// average B-G-G-R
			accumulator = (samples[0] + (samples[1] + samples[2]) / 2 + samples[3]) / 3;
*/
			//sample_range = 255. * samples[0] / range_max;

//			r = 255;
//			g = 120;
//			b = 255;


			r = interpol_r(raw_data, width, height, x, y);
			g = interpol_g(raw_data, width, height, x, y);
			b = interpol_b(raw_data, width, height, x, y);

/*
			// correct bayer cfa
			if (x % 2 == 0) {
				if (y % 2 == 0)
					b = clip(sample_range);
				else
					g = clip(sample_range);
			} else {
				if (y % 2 == 0)
					g = clip(sample_range);
				else
					r = clip(sample_range);
			}
*/


/*
			if (index % 4 == 0)
				b = clip(sample_range);
			else if (index % 4 == 1)
				g = clip(sample_range);
			else if (index % 4 == 2)
				g = clip(sample_range);
			else if (index % 4 == 3)
				r = clip(sample_range);
*/

//			printf("%3u/%3u/%3u\n", r, g, b);
			*pixels++ = pixel_pack(r, g, b, 255);

			samples++;
		}
	}
}

int bayer_8_convert(uint8_t *dst, uint8_t *img, uint32_t length, uint32_t w,
		    uint32_t h, unsigned int format)
{
	uint32_t x = 0, y = 0;
	uint32_t i = w * h;
	
	if(length < i) return(-1);
	
	/* SBGGR8 bayer pattern:
	 * 
	 * BGBGBGBGBG
	 * GRGRGRGRGR
	 * BGBGBGBGBG
	 * GRGRGRGRGR
	 * 
	 * SGBRG8 bayer pattern:
	 * 
	 * GBGBGBGBGB
	 * RGRGRGRGRG
	 * GBGBGBGBGB
	 * RGRGRGRGRG
	 *
	 * SGRBG8 bayer pattern:
	 *
	 * GRGRGRGRGR
	 * BGBGBGBGBG
	 * GRGRGRGRGR
	 * BGBGBGBGBG
	*/
	
	while(i-- > 0)
	{
		uint8_t *p[8];
		uint8_t hn, vn, di;
		uint8_t r, g, b;
		int mode;
		
		/* Setup pointers to this pixel's neighbours. */
		p[0] = img - w - 1;
		p[1] = img - w;
		p[2] = img - w + 1;
		p[3] = img - 1;
		p[4] = img + 1;
		p[5] = img + w - 1;
		p[6] = img + w;
		p[7] = img + w + 1;
		
		/* Juggle pointers if they are out of bounds. */
		if(!y)              { p[0]=p[5]; p[1]=p[6]; p[2]=p[7]; }
		else if(y == h - 1) { p[5]=p[0]; p[6]=p[1]; p[7]=p[2]; }
		if(!x)              { p[0]=p[2]; p[3]=p[4]; p[5]=p[7]; }
		else if(x == w - 1) { p[2]=p[0]; p[4]=p[3]; p[7]=p[5]; }
		
		/* Average matching neighbours. */
		hn = (*p[3] + *p[4]) / 2;
		vn = (*p[1] + *p[6]) / 2;
		di = (*p[0] + *p[2] + *p[5] + *p[7]) / 4;
		
		/* Calculate RGB */
		if(format == V4L2_PIX_FMT_SBGGR8 ||
		   format == V4L2_PIX_FMT_SRGGB8) {
			mode = (x + y) & 0x01;
		} else {
			mode = ~(x + y) & 0x01;
		}
		
		if(mode)
		{
			g = *img;
			if(y & 0x01) { r = hn; b = vn; }
			else         { r = vn; b = hn; }
		}
		else if(y & 0x01) { r = *img; g = (vn + hn) / 2; b = di; }
		else              { b = *img; g = (vn + hn) / 2; r = di; }
		
		if(format == V4L2_PIX_FMT_SGRBG8 ||
		   format == V4L2_PIX_FMT_SRGGB8)
		{
			uint8_t t = r;
			r = b;
			b = t;
		}
		
		*(dst++) += b;
		*(dst++) += g;
		*(dst++) += r;
		*(dst++) += 0;
		
		/* Move to the next pixel (or line) */
		if(++x == w) { x = 0; y++; }
		img++;
	}
	
	return(0);
}

int bayer_convert(uint8_t *dst, uint8_t *img, uint32_t length, uint32_t w,
		  uint32_t h, unsigned int format)
{
	switch (format) {
	case V4L2_PIX_FMT_SBGGR8:
		return bayer_8_convert(dst, img, length, w, h, format);
	case V4L2_PIX_FMT_SBGGR10:
		return bayer_10_convert(dst, img, length, w, h);
	default:
		return -EINVAL;
	}
}

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
	struct v4l2_bayer_standalone standalone = {
		.dump_fd = -1,
	};
	struct v4l2_camera *camera = &standalone.camera;
	struct v4l2_camera_buffer *capture_buffer;
	unsigned int capture_index;
	unsigned int width, height, format;
	bool dump = false;
	int ret;

	if (argc > 2) {
		width = atoi(argv[1]);
		height = atoi(argv[2]);
	} else {
		width = 2592;
		height = 1944;
	}

	format = V4L2_PIX_FMT_SBGGR8;

	ret = v4l2_camera_open(camera, NULL);
	if (ret)
		goto error;

	ret = v4l2_camera_setup_dimensions(camera, width, height);
	if (ret)
		goto error;

	ret = v4l2_camera_setup_format(camera, format);
	if (ret)
		goto error;

	ret = v4l2_camera_setup(camera);
	if (ret)
		goto error;

	ret = v4l2_camera_start(camera);
	if (ret)
		goto error;

	ret = v4l2_camera_prepare(camera);
	if (ret)
		return ret;

	ret = v4l2_camera_run(camera);
	if (ret)
		return ret;

	capture_index = camera->capture_buffers_index;
	capture_buffer = &camera->capture_buffers[capture_index];

	standalone.raw_buffer = capture_buffer->mmap_data[0];

	switch (format) {
	case V4L2_PIX_FMT_SBGGR8:
		standalone.raw_length = width * height;
		break;
	case V4L2_PIX_FMT_SBGGR10:
		standalone.raw_length = width * height * 2;
		break;
	default:
		ret = -EINVAL;
		goto error;
	}

	standalone.rgb_length = width * height * 4;
	standalone.rgb_buffer = malloc(standalone.rgb_length);

	if (dump) {
		standalone.dump_fd = open("frame.raw", O_RDWR | O_CREAT | O_TRUNC,
				  	  0644);
		if (standalone.dump_fd < 0)
			goto error;
	}

	printf("Bayer convert start!\n");

	bayer_convert(standalone.rgb_buffer, standalone.raw_buffer,
		      standalone.raw_length, width, height, format);

	printf("Bayer convert done!\n");

	image_write("frame.png", standalone.rgb_buffer, width, height);

	printf("Image write done!\n");

	if (dump)
		close(standalone.dump_fd);

	ret = v4l2_camera_complete(camera);
	if (ret)
		return ret;

	ret = v4l2_camera_stop(camera);
	if (ret)
		goto error;

	ret = v4l2_camera_teardown(camera);
	if (ret)
		goto error;

	return 0;

error:
	return 1;
}
