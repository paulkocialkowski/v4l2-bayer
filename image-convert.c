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

static inline uint8_t byte_range(float v)
{
	if (v < 0.)
		return 0;
	else if (v > 255.)
		return 255;
	else
		return (uint8_t)v;
}

int nv12_convert(void *luma, void *chroma, void *rgb, unsigned int width, unsigned int height, unsigned int stride)
{
	unsigned int x, y;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			unsigned int xc, yc;
			unsigned int ol, oc;
			uint32_t *rgbx;
			float value;
			uint8_t vy, vu, vv;
			uint8_t vr, vg, vb;

			rgbx = (uint32_t *)((uint8_t *)rgb + stride * y + x * 4);

			ol = y * width + x;
			vy = *((uint8_t *)luma + ol);

			xc = x / 2;
			yc = y / 2;
			oc = yc * width + xc * 2;
			vu = *((uint8_t *)chroma + oc);
			vv = *((uint8_t *)chroma + oc + 1);

			value = (float)vy + 1.13983 * ((float)vv - 128.0);
			vr = byte_range(value);
			value = (float)vy - 0.39466 * ((float)vu - 128.0) - 0.58060 * ((float)vv - 128.0);
			vg = byte_range(value);
			value = (float)vy + 2.03211 * ((float)vu - 128.0);
			vb = byte_range(value);

			*rgbx = vb | (vg << 8) | (vr << 16) | (0 << 24);
		}
	}

	return 0;
}

/* offsets: Y0 Y1 U V */
int yuyv_convert(void *packed, void *rgb, unsigned int width, unsigned int height, unsigned int stride, unsigned int *offsets)
{
	unsigned int x, y;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			unsigned int xc, yc;
			unsigned int o, oy, ou, ov;
			uint32_t *rgbx;
			float value;
			uint8_t vy, vu, vv;
			uint8_t vr, vg, vb;
			unsigned int xa, xf;

			rgbx = (uint32_t *)((uint8_t *)rgb + stride * y + x * 4);

			xa = x / 2;

			o = y * width * 2 + xa * 4;

			if (x & 1)
				oy = o + offsets[1];
			else
				oy = o + offsets[0];

			ou = o + offsets[2];
			ov = o + offsets[3];

			vy = *((uint8_t *)packed + oy);
			vu = *((uint8_t *)packed + ou);
			vv = *((uint8_t *)packed + ov);

			value = (float)vy + 1.13983 * ((float)vv - 128.0);
			vr = byte_range(value);
			value = (float)vy - 0.39466 * ((float)vu - 128.0) - 0.58060 * ((float)vv - 128.0);
			vg = byte_range(value);
			value = (float)vy + 2.03211 * ((float)vu - 128.0);
			vb = byte_range(value);

			*rgbx = vb | (vg << 8) | (vr << 16) | (0 << 24);
		}
	}

	return 0;
}

int image_convert(uint8_t *dst, uint8_t *img, uint32_t length, uint32_t w,
		  uint32_t h, unsigned int format)
{
	int offsets_uyvy[] = { 1, 3, 0, 2 };
	int offsets_yuyv[] = { 0, 2, 1, 3 };

	switch (format) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SRGGB8:
		return bayer_8_convert(dst, img, length, w, h, format);
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SRGGB10:
		return bayer_10_convert(dst, img, length, w, h);
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		return nv12_convert(img, img + w*h, dst, w, h, w*4);
	case V4L2_PIX_FMT_UYVY:
		return yuyv_convert(img, dst, w, h, w * 4, offsets_uyvy);
	case V4L2_PIX_FMT_YUYV:
		return yuyv_convert(img, dst, w, h, w * 4, offsets_yuyv);
	default:
		return -EINVAL;
	}
}
