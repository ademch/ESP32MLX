
#include "MLX90640_frame2bmp.h"
#include <cstddef>
#include "esp_heap_caps.h"
#include <esp32-hal-log.h>
#include <math.h>

template<typename T>
constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
	return (v < lo) ? lo : (v > hi) ? hi : v;
}

bool MLXframe2bmp(float* src, uint16_t src_len, uint16_t width, uint16_t height,
	              uint8_t** out, uint16_t* out_len)
{
	*out = NULL;
	*out_len = 0;

	int pix_count = width * height;
	int bpp = 3;
	int palette_size = 0;

	uint16_t out_size = (pix_count * bpp) + BMP_HEADER_LEN + palette_size;
	uint8_t* out_buf  = (uint8_t*)heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!out_buf) {
		ESP_LOGE("MLXframe2bmp", "heap_caps_malloc failed! %u", out_size);
		return false;
	}

	out_buf[0] = 'B';
	out_buf[1] = 'M';
	bmp_header_t* bitmapH = (bmp_header_t*)&out_buf[2];
	bitmapH->reserved = 0;
	bitmapH->filesize = out_size;
	bitmapH->fileoffset_to_pixelarray = BMP_HEADER_LEN + palette_size;
	bitmapH->dibheadersize = 40;
	bitmapH->width = width;
	bitmapH->height = -height;//set negative for top to bottom
	bitmapH->planes = 1;
	bitmapH->bitsperpixel = bpp * 8;
	bitmapH->compression = 0;
	bitmapH->imagesize = pix_count * bpp;
	bitmapH->ypixelpermeter = 0x0B13; //2835 , 72 DPI
	bitmapH->xpixelpermeter = 0x0B13; //2835 , 72 DPI
	bitmapH->numcolorspallette = 0;
	bitmapH->mostimpcolor = 0;

	uint8_t* palette_buf = out_buf + BMP_HEADER_LEN;
	uint8_t* pix_buf = palette_buf + palette_size;

	float fValue;
	uint8_t iValue;
	//uint8_t gray = static_cast<uint8_t>(clamp((val - fMin) / fRange * 255.0f, 0.0f, 255.0f));
	for (int i = 0; i < pix_count; i++)
	{
		fValue = *src++;
		iValue = round(fValue);

		*pix_buf++ = iValue;
		*pix_buf++ = iValue;
		*pix_buf++ = iValue;
	}

	*out = out_buf;
	*out_len = out_size;

	return true;
}
