/**
   @copyright (C) 2017 Melexis N.V.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/
#ifndef _MLX90640_FRAME2BMP_H_
#define _MLX90640_FRAME2BMP_H_

#include <stdint.h>

static const int BMP_HEADER_LEN = 54;

typedef struct {
	uint32_t filesize;
	uint32_t reserved;
	uint32_t fileoffset_to_pixelarray;
	uint32_t dibheadersize;
	int32_t  width;
	int32_t  height;
	uint16_t planes;
	uint16_t bitsperpixel;
	uint32_t compression;
	uint32_t imagesize;
	uint32_t ypixelpermeter;
	uint32_t xpixelpermeter;
	uint32_t numcolorspallette;
	uint32_t mostimpcolor;
} bmp_header_t;

bool MLXframe2bmp(float* src, uint16_t src_len,
	              uint16_t width, uint16_t height, uint8_t** out, uint16_t* out_len);

struct RGB {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

RGB ironbow(float value, float minVal, float maxVal);


#endif
