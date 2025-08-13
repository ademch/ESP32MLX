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

	#define MLX90640_eeSIZE 832

	#define MLX90640_REFRESH_RATE_05HZ	0
	#define MLX90640_REFRESH_RATE_1HZ	1
	#define MLX90640_REFRESH_RATE_2HZ	2	// deafult
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
    
    int MLX90640_DumpEE(uint8_t slaveAddr, uint16_t *eeData);
	// Restore params
	int MLX90640_ExtractParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
    float MLX90640_GetVdd(uint16_t *frameData, const paramsMLX90640 *params);
    float MLX90640_GetTa(uint16_t *frameData, const paramsMLX90640 *params);
    
	int  MLX90640_GetFrameData(uint8_t slaveAddr, uint16_t *frameData);
	void MLX90640_GetImage(uint16_t *frameData, const paramsMLX90640 *params, float *result);
    void MLX90640_CalculateTo(uint16_t *frameData, const paramsMLX90640 *params, float emissivity, float tr, float *result);
    
	int MLX90640_GetCurResolution(uint8_t slaveAddr);
	int MLX90640_SetResolution(uint8_t slaveAddr, uint8_t resolution);
    
	int MLX90640_GetRefreshRate(uint8_t slaveAddr);
	int MLX90640_SetRefreshRate(uint8_t slaveAddr, uint8_t refreshRate);
    
	int MLX90640_GetSubPageNumber(uint16_t *frameData);
    int MLX90640_GetCurMode(uint8_t slaveAddr);

    int MLX90640_SetInterleavedMode(uint8_t slaveAddr);
    int MLX90640_SetChessMode(uint8_t slaveAddr);
    
#endif
