/**
 * @copyright (C) 2017 Melexis N.V.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#ifndef _MLX640_API_H_
#define _MLX640_API_H_

#include "sys/time.h"

	#define MLX90640_eepromSIZE         832
	#define MLX90640_ramSIZEframe       832	// ram bytes (768 frame + 64 params tag)
	#define MLX90640_ramSIZEuser        834	// contains two additional bytes
	#define MLX90640_pixelCOUNT         768

	// Number of subpages per second
	#define MLX90640_REFRESH_RATE_05HZ	0
	#define MLX90640_REFRESH_RATE_1HZ	1
	#define MLX90640_REFRESH_RATE_2HZ	2	// default
	#define MLX90640_REFRESH_RATE_4HZ	3
	#define MLX90640_REFRESH_RATE_8HZ	4
	#define MLX90640_REFRESH_RATE_16HZ	5
	#define MLX90640_REFRESH_RATE_32HZ	6
	#define MLX90640_REFRESH_RATE_64HZ	7

	#define MLX90640_RAM_VDD			810
	#define MLX90640_RAM_PTAT			800
	#define MLX90640_RAM_GAIN			778
	#define MLX90640_RAM_CP0          	776
	#define MLX90640_RAM_CP1			808
	#define MLX90640_RAM_VBE			768

	#define MLX90640_RAM_AUX_CTRL_REG1	832
	#define MLX90640_RAM_AUX_SUBPAGE	833

	// addresses
	#define MLX90640_I2C_RAM			0x0400
	#define MLX90640_I2C_EEPROM			0x2400
	#define MLX90640_I2C_STATUS_REG		0x8000
	#define MLX90640_I2C_CTRL_REG1		0x800D

    typedef struct
    {
        int16_t		kVdd;
        int16_t		vdd25;
        float		KvPTAT;
        float		KtPTAT;
        uint16_t	vPTAT25;
        float		alphaPTAT;
        int16_t		gainEE;
        float		tgc;
        float		cpKv;
        float		cpKta;
        uint8_t		resolutionEE;
        uint8_t		calibrationModeEE;
        float		KsTa;
        float		ksTo[4];
        int16_t		ct[4];
        float		alpha[768];    
        int16_t		offset[768];    
        float		kta[768];    
        float		kv[768];
        float		cpAlpha[2];
        int16_t		cpOffset[2];
        float		ilChessC[3]; 
        uint16_t	brokenPixels[5];
        uint16_t	outlierPixels[5];  
    } paramsMLX90640;

	typedef struct {
		void* buf;                  /*!< Pointer to the pixel data */
		uint16_t len;               /*!< Length of the buffer in bytes */
		uint16_t width;             /*!< Width of the buffer in pixels */
		uint16_t height;            /*!< Height of the buffer in pixels */
		struct timeval timestamp;   /*!< Timestamp since boot of the first DMA buffer of the frame */
	} mlx_fb_t;
    
	int MLX90640_Init(uint8_t _slaveAddr);

    int MLX90640_DumpEE(uint16_t *eeData);
	// Restore params
	int MLX90640_ExtractParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
    float MLX90640_GetVdd(uint16_t *frameData, const paramsMLX90640 *params);
    float MLX90640_GetTa(uint16_t *frameData, const paramsMLX90640 *params);
    
	int  MLX90640_GetFrameData(uint16_t *frameData);
	
	void MLX90640_GetImage(uint16_t *frameData, const paramsMLX90640 *params, float *result);
    void MLX90640_CalculateTo(uint16_t *frameData, const paramsMLX90640 *params, float emissivity, float tr, float *result);
    
	mlx_fb_t MLX90640_fb_get();
	void     MLX90640_fb_return(mlx_fb_t& fb);

	int MLX90640_GetCurADCresolution();
	int MLX90640_SetADCresolution(uint8_t resolution);
    
	int MLX90640_GetRefreshRate();
	int MLX90640_SetRefreshRate(uint8_t refreshRate);
    
	int MLX90640_GetSubPageNumber(uint16_t *frameData);
    int MLX90640_GetCurMode();

    int MLX90640_SetInterleavedMode();
    int MLX90640_SetChessMode();
    
#endif
