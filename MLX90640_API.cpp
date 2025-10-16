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
#include "MLX90640_I2C_Driver.h"
#include "MLX90640_API.h"
#include <math.h>
#include "esp_timer.h"
#include <stdlib.h>
#include <Arduino.h>
#include <Wire.h>

bool mlx_online = false;

// device address
uint8_t mlx_slaveAddr = 0;

// mutex for exclusive device interaction
SemaphoreHandle_t mlxMutex;

// params
paramsMLX90640 mlx90640 = {};

// frame populated by GetFrameData
uint16_t mlx90640_frame[MLX90640_ramSIZEuser];

// frame processed by CalculateTo
float mlx90640_float_frame[MLX90640_pixelCOUNT]   = {0.0};	// 32 columns x 24 rows

// user calibration offsets
float mlx90640_float_offsets[MLX90640_pixelCOUNT] = {0.0};	// 32 columns x 24 rows

// 80% of delta between samples
int16_t msFrame_delay = 0.8 * 1000 / 2;   // 2HZ by default

float TambientReflected = 20.0f;
float fEmissivity = 0.95f;

uint8_t bMLXfastRefreshRate = 1;

void ExtractVDDParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
void ExtractPTATParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
void ExtractGainParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
void ExtractTgcParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
void ExtractResolutionParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
void ExtractKsTaParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
void ExtractKsToParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
void ExtractSensivityAlphaParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
void ExtractOffsetParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
void ExtractKtaPixelParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
void ExtractKvPixelParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
void ExtractCPParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
void ExtractCILCParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);
int  ExtractDeviatingPixels(uint16_t *eeData, paramsMLX90640 *mlx90640);
int  CheckAdjacentPixels(uint16_t pix1, uint16_t pix2);
int  CheckEEPROMvalid(uint16_t *eeData);

int  DumpEE(uint16_t *eeData);

int  GetFrameData(uint16_t *frameData);
void CalculateTo(uint16_t *frameData, const paramsMLX90640 *params, float emissivity, float tr, float *result);
//   Restore params
int  ExtractParameters(uint16_t *eeData, paramsMLX90640 *mlx90640);

float GetVdd(uint16_t *frameData, const paramsMLX90640 *params);
float GetTa(uint16_t *frameData, const paramsMLX90640 *params);

void  MLX90640_GetImage(uint16_t *frameData, const paramsMLX90640 *params, float *result);


int MLX90640_Init(uint8_t _slaveAddr)
{
	mlx_slaveAddr = _slaveAddr;

	Wire.beginTransmission(mlx_slaveAddr);
	if (Wire.endTransmission() != 0) {
		Serial.print("MLX90640 not detected at address ");
		Serial.println(mlx_slaveAddr);
		Serial.println("This will result in zero valued mlx stream");

		mlx_online = false;
		return -3;
	}

	mlx_online = true;

	mlxMutex = xSemaphoreCreateMutex();

	uint16_t eeMLX90640[MLX90640_eepromSIZE] = {0};

	int status = DumpEE(eeMLX90640);
	if (status != 0) return -1;

	status = ExtractParameters(eeMLX90640, &mlx90640);
	if (status != 0) return -2;

	return 0;
}

int DumpEE(uint16_t *eeData)
{
    return MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_EEPROM, MLX90640_eepromSIZE, eeData);
}

int GetFrameData(uint16_t *frameData)
{
	uint16_t controlRegister1;
	uint16_t statusRegister;
	int error;

	// --------------------------------------------------------------------------
	// Logic for measuring how much time has passed since previous frame
	// to be able to pause the task before the next frame becomes available

	static int64_t mlx_update_t1_usec = 0;

	int64_t mlx_update_t2_usec = esp_timer_get_time();

	int64_t msFromPrevFrame = (mlx_update_t2_usec - mlx_update_t1_usec) >> 10;		// convert to MS dividing by 1024
	int64_t msToNextFrame = msFrame_delay - msFromPrevFrame;

	// pause the task for more than 10 ms 
	if (msToNextFrame > 10) {
		log_d("Awaiting mlxFrame for %lld ms", msToNextFrame);
		delay(msToNextFrame);
	}

	xSemaphoreTake(mlxMutex, portMAX_DELAY);

		// Busy wait for the frame ("data ready" flag)
		uint16_t dataReady = 0;
		while (dataReady == 0)
		{
			error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_STATUS_REG, 1, &statusRegister);
			if (error != 0) {
				xSemaphoreGive(mlxMutex);
				return error;
			}

			dataReady = statusRegister & 0x0008;	// B3: New data available in ram
		}

		mlx_update_t1_usec = esp_timer_get_time();

		//  ---------------------------------------------------------------------------

		// Read frame
		error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_RAM, MLX90640_ramSIZEframe, frameData);
		if (error != 0) {
			xSemaphoreGive(mlxMutex);
			return error;
		}

		// Reset "New DATA available in RAM" flag
		error = MLX90640_I2CWrite(mlx_slaveAddr, MLX90640_I2C_STATUS_REG, statusRegister & 0xFFF7);
		if (error == -1) {
			xSemaphoreGive(mlxMutex);
			return error;
		}

		// Read and store controlRegister1
		error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_CTRL_REG1, 1, &controlRegister1);
		if (error != 0) {
			xSemaphoreGive(mlxMutex);
			return error;
		}

	xSemaphoreGive(mlxMutex);

	frameData[MLX90640_FRAME_AUX_CTRL_REG1] = controlRegister1;
	frameData[MLX90640_FRAME_AUX_SUBPAGE]   = statusRegister & 0x0001;
	
	return frameData[MLX90640_FRAME_AUX_SUBPAGE];
}

mlx_fb_t MLX90640_fb_get()
{
	mlx_fb_t fb = {};
	
	if (mlx_online)
	{
		// sample twice sequentially to be sure we get 0th and 1st subpages
		for (uint8_t x = 0; x < 2; x++)
		{
			int status = GetFrameData(mlx90640_frame);
			if (status < 0)
			{
				log_e("GetFrame Error: %d", status);
				return fb;	// empty fb
			}

			CalculateTo(mlx90640_frame, &mlx90640, fEmissivity, TambientReflected, mlx90640_float_frame);
		}
	}
	// prepare fb data even if sensor is offline

	//MLXframe2bmp(mlx90640_float_frame,MLX90640_pixelCOUNT, 32,24,  &fb.buf,&fb.len);

	uint64_t us = (uint64_t)esp_timer_get_time();
	fb.timestamp.tv_sec  = us / 1000000UL;
	fb.timestamp.tv_usec = us % 1000000UL;

	fb.width    = 32;
	fb.height   = 24;
	fb.values   = mlx90640_float_frame;
	fb.offsets  = mlx90640_float_offsets;
	fb.nBytes   = fb.width * fb.height * sizeof(float);
	fb.TambientReflected = TambientReflected;

	return fb;
}

void MLX90640_fb_return(mlx_fb_t& fb)
{
	//free(fb.buf);
	fb.values  = NULL;
	fb.offsets = NULL;
}

mlx_ob_t MLX90640_ob_get()
{
	mlx_ob_t ob = {};

	uint64_t us = (uint64_t)esp_timer_get_time();
	ob.timestamp.tv_sec  = us / 1000000UL;
	ob.timestamp.tv_usec = us % 1000000UL;

	ob.width   = 32;
	ob.height  = 24;
	ob.offsets = mlx90640_float_offsets;
	ob.nBytes  = ob.width * ob.height * sizeof(float);

	return ob;
}

void MLX90640_ob_return(mlx_ob_t& ob)
{
	ob.offsets = NULL;
}

int ExtractParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
    int error = CheckEEPROMvalid(eeData);
    
    if (error == 0)
    {
        ExtractVDDParameters(eeData, mlx90640);
        ExtractPTATParameters(eeData, mlx90640);
        ExtractGainParameters(eeData, mlx90640);
        ExtractTgcParameters(eeData, mlx90640);
        ExtractResolutionParameters(eeData, mlx90640);
        ExtractKsTaParameters(eeData, mlx90640);
        ExtractKsToParameters(eeData, mlx90640);
		ExtractSensivityAlphaParameters(eeData, mlx90640);
        ExtractOffsetParameters(eeData, mlx90640);
        ExtractKtaPixelParameters(eeData, mlx90640);
        ExtractKvPixelParameters(eeData, mlx90640);
        ExtractCPParameters(eeData, mlx90640);
        ExtractCILCParameters(eeData, mlx90640);
        error = ExtractDeviatingPixels(eeData, mlx90640);  
    }
    
    return error;
}

//------------------------------------------------------------------------------

int MLX90640_SetADCresolution(uint8_t resolution)
{
	if (!mlx_online) return -1000;

    int value = (resolution & 0x03) << 10;
    
	uint16_t controlRegister1;
	int error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_CTRL_REG1, 1, &controlRegister1);
    if (error == 0)
    {
		// no error
        value = (controlRegister1 & 0xF3FF) | value;
        error = MLX90640_I2CWrite(mlx_slaveAddr, MLX90640_I2C_CTRL_REG1, value);
    }    
    
    return error;
}

//------------------------------------------------------------------------------

int MLX90640_GetCurADCresolution()
{
	if (!mlx_online) return -1000;

    uint16_t controlRegister1;
    int error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_CTRL_REG1, 1, &controlRegister1);
    if (error != 0) return error;

    int resolution = (controlRegister1 & 0x0C00) >> 10;
    
    return resolution; 
}

//------------------------------------------------------------------------------

int MLX90640_SetRefreshRate(uint8_t refreshRate)
{
	if (!mlx_online) return -1000;

	xSemaphoreTake(mlxMutex, portMAX_DELAY);
	
   		int value = (refreshRate & 0x07) << 7;
    
		uint16_t controlRegister1;

		int error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_CTRL_REG1, 1, &controlRegister1);
		if (error == 0)
		{
			// success
			value = (controlRegister1 & 0xFC7F) | value;			// B7-B9
			error = MLX90640_I2CWrite(mlx_slaveAddr, MLX90640_I2C_CTRL_REG1, value);
		}

	xSemaphoreGive(mlxMutex);

	switch (refreshRate) {
	case MLX90640_REFRESH_RATE_05HZ:
		msFrame_delay = 0.8 * 1000 / 0.5;
		break;
	case MLX90640_REFRESH_RATE_1HZ:
		msFrame_delay = 0.8 * 1000 / 1;
		break;
	case MLX90640_REFRESH_RATE_4HZ:
		msFrame_delay = 0.8 * 1000 / 4;
		break;
	case MLX90640_REFRESH_RATE_8HZ:
		msFrame_delay = 0.8 * 1000 / 8;
		break;
	case MLX90640_REFRESH_RATE_16HZ:
		msFrame_delay = 0.8 * 1000 / 16;
		break;
	case MLX90640_REFRESH_RATE_32HZ:
		msFrame_delay = 0.8 * 1000 / 32;
		break;
	case MLX90640_REFRESH_RATE_64HZ:
		msFrame_delay = 0.8 * 1000 / 64;
		break;

	case MLX90640_REFRESH_RATE_2HZ:
	default:
		msFrame_delay = 0.8 * 1000 / 2;
		break;
	}
    
    return error;
}

// fast/slow 4HZ vs 05HZ
int MLX90640_SetFastRefreshRate(uint8_t fast)
{
	if (!mlx_online) return -1000;

	bMLXfastRefreshRate = fast;

	if (bMLXfastRefreshRate)
		return MLX90640_SetRefreshRate(MLX90640_REFRESH_RATE_4HZ);
	else
		return MLX90640_SetRefreshRate(MLX90640_REFRESH_RATE_05HZ);
}

// fast/slow 4HZ vs 05HZ
int MLX90640_GetFastRefreshRate()
{
	return bMLXfastRefreshRate;
}

//------------------------------------------------------------------------------

int MLX90640_GetRefreshRate()
{
	if (!mlx_online) return -1000;

	xSemaphoreTake(mlxMutex, portMAX_DELAY);

		uint16_t controlRegister1;
    
		int error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_CTRL_REG1, 1, &controlRegister1);
		if (error != 0) {
			xSemaphoreGive(mlxMutex);
			return error;
		}

		int refreshRate = (controlRegister1 & 0x0380) >> 7;

	xSemaphoreGive(mlxMutex);
    
    return refreshRate;
}

//------------------------------------------------------------------------------

int MLX90640_SetInterleavedMode()
{
	if (!mlx_online) return -1000;

    uint16_t controlRegister1;
    int value;
    
    int error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_CTRL_REG1, 1, &controlRegister1);
    if (error == 0)
	{
		// no error

        value = (controlRegister1 & 0xEFFF);
        error = MLX90640_I2CWrite(mlx_slaveAddr, MLX90640_I2C_CTRL_REG1, value);
    }    
    
    return error;
}

//------------------------------------------------------------------------------

int MLX90640_SetChessMode()
{
	if (!mlx_online) return -1000;

    uint16_t controlRegister1;
    int value;
        
    int error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_CTRL_REG1, 1, &controlRegister1);
    if (error == 0)
    {
		// no error

        value = (controlRegister1 | 0x1000);
        error = MLX90640_I2CWrite(mlx_slaveAddr, MLX90640_I2C_CTRL_REG1, value);
    }    
    
    return error;
}

//------------------------------------------------------------------------------

int MLX90640_GetCurMode()
{
	if (!mlx_online) return -1000;

    uint16_t controlRegister1;
    
    int error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_CTRL_REG1, 1, &controlRegister1);
    if (error != 0) return error;

    int mode = (controlRegister1 & 0x1000) >> 12;
    
    return mode; 
}


//------------------------------------------------------------------------------
void MLX90640_SetAmbientReflected(float value)
{
	TambientReflected = value;
}

void MLX90640_SetEmissivity(float value)
{
	fEmissivity = value;
}


//------------------------------------------------------------------------------
// Calculate Object Temperature from raw data
//
// frameData  - raw frame from GetFrameData()
// params     - structure holding calibration constants after ExtractParameters()
// emissivity - target surface emissivity (0.02-0.2: Shiny metal, 0.96: Matte black paint)
// tr         - ambient temperature reflected by the object into the sensor in Celsius
//              (in the air the sensor is 8 degrees hotter, ie. tr ~ ta-8)
// afResult   - output array of 768 floats (32x24 pixels) in Celsius
void CalculateTo(uint16_t* frameData,
	                      const paramsMLX90640* params,
	                      float emissivity,
	                      float tr,
	                      float *afResult)
{
	uint16_t subPage = frameData[MLX90640_FRAME_AUX_SUBPAGE];

	float       vdd  = GetVdd(frameData, params);
    float       ta   = GetTa(frameData, params);

    // 11.2.2.9
	// ta_r^4 = ta^4 - (1-eps)*tr^4 / eps
	float       ta4   = pow((ta + 273.15), (double)4);
    float       tr4   = pow((tr + 273.15), (double)4);
    float       ta_r4 = tr4 - (tr4-ta4)/emissivity;

	ESP_LOGD("Frame data", "Subpage %d: Tdie=%3.1f, Vdd=%4.2f", subPage, ta, vdd);
    
	float alphaCorrR[4];
    alphaCorrR[0] = 1 / (1 + params->ksTo[0] * 40);
    alphaCorrR[1] = 1;
    alphaCorrR[2] = (1 + params->ksTo[2] * params->ct[2]);
    alphaCorrR[3] = alphaCorrR[2] * (1 + params->ksTo[3] * (params->ct[3] - params->ct[2]));
    
//------------------------- Gain calculation -----------------------------------    
    float gain = frameData[MLX90640_FRAME_GAIN];
    if (gain > 32767) gain = gain - 65536;
    gain = params->gainEE / gain; 
  
//------------------------- To calculation -------------------------------------    
	// 0x80 chess pattern or 0x00 interleaved
	uint8_t modeFrame = (frameData[MLX90640_FRAME_AUX_CTRL_REG1] & 0x1000) >> 5;
    
	float irDataCP[2];
	irDataCP[0] = frameData[MLX90640_FRAME_CP0];
    irDataCP[1] = frameData[MLX90640_FRAME_CP1];
    
	for (int i = 0; i < 2; i++)
    {
		// observe sign
        if (irDataCP[i] > 32767) irDataCP[i] = irDataCP[i] - 65536;

        irDataCP[i] = irDataCP[i] * gain;
    }
    
	irDataCP[0] = irDataCP[0] - params->cpOffset[0] * (1 + params->cpKta * (ta - 25)) * (1 + params->cpKv * (vdd - 3.3));
    
	if (modeFrame ==  params->calibrationModeEE)
        irDataCP[1] = irDataCP[1] - params->cpOffset[1] * (1 + params->cpKta * (ta - 25)) * (1 + params->cpKv * (vdd - 3.3));
    else
        irDataCP[1] = irDataCP[1] - (params->cpOffset[1] + params->ilChessC[0]) * (1 + params->cpKta * (ta - 25)) * (1 + params->cpKv * (vdd - 3.3));

    for (int pixelNumber = 0; pixelNumber < MLX90640_pixelCOUNT; pixelNumber++)
    {
		int8_t intlvdPattern = pixelNumber / 32 - (pixelNumber / 64) * 2;
		int8_t chessPattern  = intlvdPattern ^ (pixelNumber - (pixelNumber/2)*2);
		int8_t convPattern   = ((pixelNumber + 2) / 4 - (pixelNumber + 3) / 4 + (pixelNumber + 1) / 4 - pixelNumber / 4) * (1 - 2 * intlvdPattern);
        
		int8_t pattern;
		if (modeFrame == 0)	// interleaved
            pattern = intlvdPattern; 
        else				// chess
            pattern = chessPattern;          
        
        if (pattern == frameData[MLX90640_FRAME_AUX_SUBPAGE])
		{
			float irData;

			irData = frameData[pixelNumber];
            if (irData > 32767) irData = irData - 65536;

            irData = irData * gain;
            irData = irData - params->offset[pixelNumber]*(1 + params->kta[pixelNumber]*(ta - 25))*(1 + params->kv[pixelNumber]*(vdd - 3.3));
            
			if (modeFrame !=  params->calibrationModeEE)
                irData = irData + params->ilChessC[2] * (2 * intlvdPattern - 1) - params->ilChessC[1] * convPattern; 
            
            irData = irData / emissivity;
            irData = irData - params->tgc * irDataCP[subPage];
            
			float alphaCompensated;
			alphaCompensated = (params->alpha[pixelNumber] - params->tgc * params->cpAlpha[subPage])*(1 + params->KsTa * (ta - 25));
            
			float Sx;
			Sx = pow((double)alphaCompensated, (double)3) * (irData + alphaCompensated * ta_r4);
            Sx = sqrt(sqrt(Sx)) * params->ksTo[1];
            
            float fTo = sqrt(sqrt( irData/(alphaCompensated * (1 - params->ksTo[1] * 273.15) + Sx) + ta_r4 )) - 273.15;
 
			int8_t range;
			if      (fTo < params->ct[1]) range = 0;
			else if (fTo < params->ct[2]) range = 1;
            else if (fTo < params->ct[3]) range = 2;
            else                          range = 3;
            
            fTo = sqrt(sqrt( irData / (alphaCompensated * alphaCorrR[range] * (1 + params->ksTo[range] * (fTo - params->ct[range]))) + ta_r4)) - 273.15;
            
            afResult[pixelNumber] = fTo;
        }
    }
}


// Outputs values are in arbitrary ADC-related units (counts) and can be negative
// without converting to absolute temperatures
// Output is good for visualization (grayscale) but not for precise thermometry
// E.g.: Motion detection, Scene change detection, Simple tracking
void MLX90640_GetImage(uint16_t *frameData, const paramsMLX90640 *params, float *afResult)
{
	uint16_t subPage = frameData[MLX90640_FRAME_AUX_SUBPAGE];

    float        vdd = GetVdd(frameData, params);
    float        ta  = GetTa(frameData, params);
    
//------------------------- Gain calculation -----------------------------------    
    float gain = frameData[MLX90640_FRAME_GAIN];
    if (gain > 32767) gain = gain - 65536;
    gain = params->gainEE / gain; 
  
//------------------------- Image calculation -------------------------------------    
	// 0x80 chess pattern or 0x00 interleaved
	uint8_t modeFrame = (frameData[MLX90640_FRAME_AUX_CTRL_REG1] & 0x1000) >> 5;
    
	float irDataCP[2];
	irDataCP[0] = frameData[MLX90640_FRAME_CP0];	// subpage0
    irDataCP[1] = frameData[MLX90640_FRAME_CP1];	// subpage1

    for (int i = 0; i < 2; i++)
    {
		// observe sign
        if (irDataCP[i] > 32767) irDataCP[i] = irDataCP[i] - 65536;

        // 11.2.2.6.1
		irDataCP[i] = irDataCP[i] * gain;
    }

	// 11.2.2.6.2 Compensating offset, Ta, Vdd of CP pixel
	irDataCP[0] = irDataCP[0] - params->cpOffset[0] * (1 + params->cpKta * (ta - 25)) * (1 + params->cpKv * (vdd - 3.3));

	if (modeFrame ==  params->calibrationModeEE)	// chess
        irDataCP[1] = irDataCP[1] - params->cpOffset[1] * (1 + params->cpKta * (ta - 25)) * (1 + params->cpKv * (vdd - 3.3));
    else                                            // interleaved
        irDataCP[1] = irDataCP[1] - (params->cpOffset[1] + params->ilChessC[0]) * (1 + params->cpKta * (ta - 25)) * (1 + params->cpKv * (vdd - 3.3));

    for (int pixelNumber = 0; pixelNumber < MLX90640_pixelCOUNT; pixelNumber++)
    {
		// 11.2.2.7
		int8_t intlvdPattern = pixelNumber / 32 - (pixelNumber / 64) * 2;
		int8_t chessPattern  = intlvdPattern ^ (pixelNumber - (pixelNumber/2)*2);
		int8_t convPattern   = ((pixelNumber + 2) / 4 - (pixelNumber + 3) / 4 + (pixelNumber + 1) / 4 - pixelNumber / 4) * (1 - 2 * intlvdPattern);
        
		int8_t pattern;
		if (modeFrame == 0)	// interleaved
            pattern = intlvdPattern; 
        else				// chess
            pattern = chessPattern; 
        
        if (pattern == frameData[MLX90640_FRAME_AUX_SUBPAGE])
        {   
			float irData;

            irData = frameData[pixelNumber];
            if (irData > 32767) irData = irData - 65536;
            
			// 11.2.2.5.1
			irData = irData * gain;
			// 11.2.2.5.3
            irData = irData - params->offset[pixelNumber]*(1 + params->kta[pixelNumber]*(ta - 25))*(1 + params->kv[pixelNumber]*(vdd - 3.3));
            
			// 11.1.3.1
			if (modeFrame !=  params->calibrationModeEE)
                irData = irData + params->ilChessC[2] * (2 * intlvdPattern - 1) - params->ilChessC[1] * convPattern; 
 
			// 11.2.2.7
            irData = irData - params->tgc * irDataCP[subPage];
 
			// 11.2.2.8
			float alphaCompensated;
            alphaCompensated = (params->alpha[pixelNumber] - params->tgc * params->cpAlpha[subPage])*(1 + params->KsTa * (ta - 25));
 
            afResult[pixelNumber] = irData / alphaCompensated;
        }
    }
}

// Calculats power supply voltage from its internal ADC readings,
// compensating for resolution differences and calibration constants
float GetVdd(uint16_t *frameData, const paramsMLX90640 *params)
{
    float vdd = frameData[MLX90640_FRAME_VDD];
    if (vdd > 32767) vdd = vdd - 65536;

    int resolutionADC = (frameData[MLX90640_FRAME_AUX_CTRL_REG1] & 0x0C00) >> 10;
    
	// The ADC resolution can vary depending on sensor settings.
	// Compute a correction factor between the EEPROM default ADC resolution (params->resolutionEE)
	// and the actual runtime ADC resolution (resolutionADC)
	float resolutionCor = pow(2, (double)params->resolutionEE) /
		                  pow(2, (double)resolutionADC);
    
	// Convert from adc counts to voltage
	// vdd25 is the sensor's ADC offset value at 25 C, stored during calibration.
	// It acts as the reference point for the supply voltage calculation.
	// Subtracting it removes the offset so voltage can be computed relative to this baseline
	vdd = (resolutionCor * vdd - params->vdd25) / params->kVdd + 3.3;
    
    return vdd;
}

//------------------------------------------------------------------------------
// Calculate ambient/device temperature (die temperature)
// Without correction, the object temperature(To) would be biased by how warm the chip is
float GetTa(uint16_t *frameData, const paramsMLX90640 *params)
{
    float vdd = GetVdd(frameData, params);
    
	// Voltage proportional to ambient temperature constant
	float Vptat = frameData[MLX90640_FRAME_PTAT];
    if (Vptat > 32767) Vptat = Vptat - 65536;
 
    float Vbe = frameData[MLX90640_FRAME_VBE];
    if (Vbe > 32767) Vbe = Vbe - 65536;

	// The combination of PTAT and Vbe cancels out nonlinear effects and supply voltage dependency
    float VptatArt = (Vptat / (Vptat * params->alphaPTAT + Vbe)) * pow(2, (double)18);
    
    float Ta = (VptatArt / (1 + params->KvPTAT * (vdd - 3.3)) - params->vPTAT25);
          Ta = Ta / params->KtPTAT + 25;
    
    return Ta;
}

// Calculate power supply voltage from its internal ADC readings,
// compensating for resolution differences and calibration constants
float MLX90640_GetVddRAM()
{
	if (!mlx_online) return 0.0f;

	xSemaphoreTake(mlxMutex, portMAX_DELAY);

		uint16_t vdd_ram;
		int error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_RAM + MLX90640_FRAME_VDD, 1, &vdd_ram);
		if (error != 0) {
			xSemaphoreGive(mlxMutex);
			return 0.0f;
		}

		float vdd = vdd_ram;
		if (vdd > 32767) vdd = vdd - 65536;

		uint16_t ctrl_reg1_ram;
		error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_CTRL_REG1, 1, &ctrl_reg1_ram);
		if (error != 0) {
			xSemaphoreGive(mlxMutex);
			return 0.0f;
		}

	xSemaphoreGive(mlxMutex);

	int resolutionADC = (ctrl_reg1_ram & 0x0C00) >> 10;

	// The ADC resolution can vary depending on sensor settings.
	// Compute a correction factor between the EEPROM default ADC resolution (params->resolutionEE)
	// and the actual runtime ADC resolution (resolutionADC)
	float resolutionCor = pow(2, (double)mlx90640.resolutionEE) /
						  pow(2, (double)resolutionADC);

	// Convert from adc counts to voltage
	// vdd25 is the sensor's ADC offset value at 25 C, stored during calibration.
	// It acts as the reference point for the supply voltage calculation.
	// Subtracting it removes the offset so voltage can be computed relative to this baseline
	vdd = (resolutionCor * vdd - mlx90640.vdd25) / mlx90640.kVdd + 3.3;

	return vdd;
}

//------------------------------------------------------------------------------
// Calculate ambient/device temperature (die temperature)
// Without correction, the object temperature(To) would be biased by how warm the chip is
float MLX90640_GetTaRAM()
{
	if (!mlx_online) return 0.0f;

	float vdd = MLX90640_GetVddRAM();

	xSemaphoreTake(mlxMutex, portMAX_DELAY);

		// Voltage proportional to ambient temperature constant
		uint16_t vptat_ram;
		int error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_RAM + MLX90640_FRAME_PTAT, 1, &vptat_ram);
		if (error != 0) {
			xSemaphoreGive(mlxMutex);
			return 0.0f;
		}

		float Vptat = vptat_ram;
		if (Vptat > 32767) Vptat = Vptat - 65536;

		uint16_t vbe_ram;
		error = MLX90640_I2CRead(mlx_slaveAddr, MLX90640_I2C_RAM + MLX90640_FRAME_VBE, 1, &vbe_ram);
		if (error != 0) {
			xSemaphoreGive(mlxMutex);
			return 0.0f;
		}

	xSemaphoreGive(mlxMutex);

	float Vbe = vbe_ram;
	if (Vbe > 32767) Vbe = Vbe - 65536;

	// The combination of PTAT and Vbe cancels out nonlinear effects and supply voltage dependency
	float VptatArt = (Vptat / (Vptat * mlx90640.alphaPTAT + Vbe)) * pow(2, (double)18);

	float Ta = (VptatArt / (1 + mlx90640.KvPTAT * (vdd - 3.3)) - mlx90640.vPTAT25);
	Ta = Ta / mlx90640.KtPTAT + 25;

	return Ta;
}

//------------------------------------------------------------------------------

int MLX90640_GetSubPageNumber(uint16_t *frameData)
{
    return frameData[MLX90640_FRAME_AUX_SUBPAGE];
}    

//------------------------------------------------------------------------------

void ExtractVDDParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
    int16_t vdd25;
	int16_t kVdd;
    
    kVdd = (eeData[51] & 0xFF00) >> 8;	// MSB
    if (kVdd > 127) kVdd = kVdd - 256;	// observe sign
    kVdd = 32 * kVdd;					// * 2^5
  
	vdd25 = eeData[51] & 0x00FF;		// LSB
    vdd25 = ((vdd25 - 256) << 5) - 8192;
    
    mlx90640->kVdd  = kVdd;
    mlx90640->vdd25 = vdd25; 
}

//------------------------------------------------------------------------------

void ExtractPTATParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
	// Voltage proportional to ambient temperature constant
	float KvPTAT = (eeData[50] & 0xFC00) >> 10;
    if (KvPTAT > 31) KvPTAT = KvPTAT - 64;
    KvPTAT = KvPTAT/4096;						// /2^10
    
	// Temperature proportional to ambient temperature constant
    float KtPTAT = eeData[50] & 0x03FF;
    if (KtPTAT > 511) KtPTAT = KtPTAT - 1024;
    KtPTAT = KtPTAT/8;
    
	// Voltage proportional to ambient temperature at 25C
	int16_t vPTAT25 = eeData[49];
    
	// Sensitivity proportional to ambient temperature
    float alphaPTAT = (eeData[16] & 0xF000) / pow(2, (double)14) + 8.0f;
    
    mlx90640->KvPTAT = KvPTAT;
    mlx90640->KtPTAT = KtPTAT;    
    mlx90640->vPTAT25 = vPTAT25;
    mlx90640->alphaPTAT = alphaPTAT;   
}

//------------------------------------------------------------------------------

void ExtractGainParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
    mlx90640->gainEE = (int16_t)eeData[48];	// no bits are changed, the number type just gets reinterpeted
}

//------------------------------------------------------------------------------

void ExtractTgcParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
    float tgc = eeData[60] & 0x00FF;
    if (tgc > 127) tgc = tgc - 256;
 
    tgc = tgc / 32.0f;
    
    mlx90640->tgc = tgc;        
}

//------------------------------------------------------------------------------

void ExtractResolutionParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
	// bytes 12,13
	uint8_t resolutionEE = (eeData[56] & 0x3000) >> 12;
    
    mlx90640->resolutionEE = resolutionEE;
}

//------------------------------------------------------------------------------

void ExtractKsTaParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
	float KsTa = (eeData[60] & 0xFF00) >> 8;
    if (KsTa > 127) KsTa = KsTa - 256;

    KsTa = KsTa / 8192.0f;
    
    mlx90640->KsTa = KsTa;
}

//------------------------------------------------------------------------------

void ExtractKsToParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
	// Extract corner temperatures
	int8_t step = ((eeData[63] & 0x3000) >> 12) * 10;
    
    mlx90640->ct[0] = -40;
    mlx90640->ct[1] = 0;
    mlx90640->ct[2] = (eeData[63] & 0x00F0) >> 4;
    mlx90640->ct[3] = (eeData[63] & 0x0F00) >> 8;
    
    mlx90640->ct[2] = mlx90640->ct[2]*step;
    mlx90640->ct[3] = mlx90640->ct[2] + mlx90640->ct[3]*step;
    
	// Extract KsTo coefficients common for all pixels
    int KsToScale = (eeData[63] & 0x000F) + 8;		// unsigned
    KsToScale = 1 << KsToScale;
    
	// Constant for the object temperature sensitivity depending on the temperature range
    mlx90640->ksTo[0] =  eeData[61] & 0x00FF;
    mlx90640->ksTo[1] = (eeData[61] & 0xFF00) >> 8;
    mlx90640->ksTo[2] =  eeData[62] & 0x00FF;
    mlx90640->ksTo[3] = (eeData[62] & 0xFF00) >> 8;
    
    for (int i = 0; i < 4; i++)
    {
        // observe sign
		if (mlx90640->ksTo[i] > 127)
            mlx90640->ksTo[i] = mlx90640->ksTo[i] - 256;
        
        mlx90640->ksTo[i] = mlx90640->ksTo[i] / KsToScale;
    } 
}

//------------------------------------------------------------------------------

void ExtractSensivityAlphaParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
    int accRow[24];
    int accColumn[32];
    int p = 0;

	uint8_t accRemScale    =   eeData[32] & 0x000F;					// unsigned
	uint8_t accColumnScale =  (eeData[32] & 0x00F0) >> 4;			// unsigned
	uint8_t accRowScale    =  (eeData[32] & 0x0F00) >> 8;			// unsigned
	uint8_t alphaScale     = ((eeData[32] & 0xF000) >> 12) + 30;	// unsigned
    int     alphaAverage   =   eeData[33];							// signed
    
    for (int i = 0; i < 6; i++)
    {
        p = i * 4;
        accRow[p + 0] = (eeData[34 + i] & 0x000F);
        accRow[p + 1] = (eeData[34 + i] & 0x00F0) >> 4;
        accRow[p + 2] = (eeData[34 + i] & 0x0F00) >> 8;
        accRow[p + 3] = (eeData[34 + i] & 0xF000) >> 12;
    }
	// observe sign
    for (int i = 0; i < 24; i++) {
        if (accRow[i] > 7) accRow[i] = accRow[i] - 16;
    }
    
    for (int i = 0; i < 8; i++)
    {
        p = i * 4;
        accColumn[p + 0] = (eeData[40 + i] & 0x000F);
        accColumn[p + 1] = (eeData[40 + i] & 0x00F0) >> 4;
        accColumn[p + 2] = (eeData[40 + i] & 0x0F00) >> 8;
        accColumn[p + 3] = (eeData[40 + i] & 0xF000) >> 12;
    }
	// observe sign
    for (int i = 0; i < 32; i ++) {
        if (accColumn[i] > 7) accColumn[i] = accColumn[i] - 16;
    }

    for (int i = 0; i < 24; i++)
    {
        for (int j = 0; j < 32; j ++)
        {
            p = 32 * i +j;
            mlx90640->alpha[p] = (eeData[64 + p] & 0x03F0) >> 4;
            if (mlx90640->alpha[p] > 31)
                mlx90640->alpha[p] = mlx90640->alpha[p] - 64;

			mlx90640->alpha[p] = mlx90640->alpha[p] * (1 << accRemScale);
			mlx90640->alpha[p] = alphaAverage + (accRow[i] << accRowScale) + (accColumn[j] << accColumnScale) + mlx90640->alpha[p];
			mlx90640->alpha[p] = mlx90640->alpha[p] / pow(2, (double)alphaScale);
        }
    }
}

//------------------------------------------------------------------------------

void ExtractOffsetParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
    int occRow[24];
    int occColumn[32];
    int p = 0;

	uint8_t occRemScale    = (eeData[16] & 0x000F);			// unsigned
	uint8_t occColumnScale = (eeData[16] & 0x00F0) >> 4;	// unsigned
	uint8_t occRowScale    = (eeData[16] & 0x0F00) >> 8;	// unsigned
	int16_t offsetAverage  = (int16_t)eeData[17];			// signed, no bits are changed, just reinterpreted
    
    for (int i = 0; i < 6; i++)
    {
        p = i * 4;
        occRow[p + 0] = (eeData[18 + i] & 0x000F);
        occRow[p + 1] = (eeData[18 + i] & 0x00F0) >> 4;
        occRow[p + 2] = (eeData[18 + i] & 0x0F00) >> 8;
        occRow[p + 3] = (eeData[18 + i] & 0xF000) >> 12;
    }
    // observe sign
	for (int i = 0; i < 24; i++) {
        if (occRow[i] > 7) occRow[i] = occRow[i] - 16;
    }
    
    for (int i = 0; i < 8; i++)
    {
        p = i * 4;
        occColumn[p + 0] = (eeData[24 + i] & 0x000F);
        occColumn[p + 1] = (eeData[24 + i] & 0x00F0) >> 4;
        occColumn[p + 2] = (eeData[24 + i] & 0x0F00) >> 8;
        occColumn[p + 3] = (eeData[24 + i] & 0xF000) >> 12;
    }
	// observe sign
    for (int i = 0; i < 32; i ++) {
        if (occColumn[i] > 7) occColumn[i] = occColumn[i] - 16;
    }

    for (int i=0; i < 24; i++)
    {
        for (int j=0; j < 32; j++)
        {
            p = 32*i + j;
            mlx90640->offset[p] = (eeData[64 + p] & 0xFC00) >> 10;
            if (mlx90640->offset[p] > 31)
                mlx90640->offset[p] = mlx90640->offset[p] - 64;

			mlx90640->offset[p] = mlx90640->offset[p] * (1 << occRemScale);
			mlx90640->offset[p] = (offsetAverage + (occRow[i] << occRowScale) + (occColumn[j] << occColumnScale) + mlx90640->offset[p]);
		}
    }
}

//------------------------------------------------------------------------------

// The per pixel ambient temperature calibration constants
void ExtractKtaPixelParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
    int8_t KtaRC[4];

	// row even, column odd
	int8_t KtaRoCo = (int8_t)((eeData[54] & 0xFF00) >> 8);	// signed

    KtaRC[0] = KtaRoCo;
    
	// row even, column odd
	int8_t KtaReCo = int8_t(eeData[54] & 0x00FF);			// signed

    KtaRC[2] = KtaReCo;
 
	// row odd, column even
	int8_t KtaRoCe = int8_t((eeData[55] & 0xFF00) >> 8);	// signed

    KtaRC[1] = KtaRoCe;
 
	// row even, column even
	int8_t KtaReCe = int8_t(eeData[55] & 0x00FF);			// signed

    KtaRC[3] = KtaReCe;
  
	uint8_t ktaScale1 = ((eeData[56] & 0x00F0) >> 4) + 8;	// unsigned
	uint8_t ktaScale2 =  (eeData[56] & 0x000F);				// unsigned

    for (int i = 0; i < 24; i++)
    {
        for (int j = 0; j < 32; j ++)
        {
            int p = 32 * i +j;
			uint8_t split = 2*(p/32 - (p/64)*2) + p%2;
            mlx90640->kta[p] = (eeData[64 + p] & 0x000E) >> 1;
            if (mlx90640->kta[p] > 3)
                mlx90640->kta[p] = mlx90640->kta[p] - 8;

            mlx90640->kta[p] = mlx90640->kta[p] * (1 << ktaScale2);
			mlx90640->kta[p] = KtaRC[split] + mlx90640->kta[p];
			mlx90640->kta[p] = mlx90640->kta[p] / pow(2, (double)ktaScale1);
        }
    }
}

//------------------------------------------------------------------------------

void ExtractKvPixelParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
    int8_t KvT[4];

	// row odd, column odd
	int8_t KvRoCo = (eeData[52] & 0xF000) >> 12;
    if (KvRoCo > 7) KvRoCo = KvRoCo - 16;

    KvT[0] = KvRoCo;
    
	// row even, column odd
	int8_t KvReCo = (eeData[52] & 0x0F00) >> 8;
    if (KvReCo > 7) KvReCo = KvReCo - 16;

    KvT[2] = KvReCo;
      
	// row odd, column even
	int8_t KvRoCe = (eeData[52] & 0x00F0) >> 4;
    if (KvRoCe > 7) KvRoCe = KvRoCe - 16;

    KvT[1] = KvRoCe;
    
	// row even, column even
	int8_t KvReCe = (eeData[52] & 0x000F);
    if (KvReCe > 7) KvReCe = KvReCe - 16;

    KvT[3] = KvReCe;
  
	uint8_t kvScale = (eeData[56] & 0x0F00) >> 8;	// unsigned

    for (int i = 0; i < 24; i++)
    {
        for (int j = 0; j < 32; j++)
        {
            int p = 32 * i + j;
			uint8_t split = 2*(p/32 - (p/64)*2) + p%2;
			mlx90640->kv[p] = KvT[split];
			mlx90640->kv[p] = mlx90640->kv[p] / pow(2, (double)kvScale);
        }
    }
}

//------------------------------------------------------------------------------

void ExtractCPParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
    int16_t offsetSP[2];
    
    // offset subpage0
	offsetSP[0] = (eeData[58] & 0x03FF);
    if (offsetSP[0] > 511) offsetSP[0] = offsetSP[0] - 1024;

	// offset subpage1
    offsetSP[1] = (eeData[58] & 0xFC00) >> 10;
    if (offsetSP[1] > 31)  offsetSP[1] = offsetSP[1] - 64;
	offsetSP[1] = offsetSP[1] + offsetSP[0];
    
	mlx90640->cpOffset[0] = offsetSP[0];
	mlx90640->cpOffset[1] = offsetSP[1];
	
	float alphaSP[2];

	alphaSP[0] = (eeData[57] & 0x03FF);
    if (alphaSP[0] > 511) alphaSP[0] = alphaSP[0] - 1024;

	uint8_t alphaScale = ((eeData[32] & 0xF000) >> 12) + 27;
	alphaSP[0] = alphaSP[0] / pow(2, (double)alphaScale);
    
    alphaSP[1] = (eeData[57] & 0xFC00) >> 10;
    if (alphaSP[1] > 31) alphaSP[1] = alphaSP[1] - 64;

    alphaSP[1] = (1 + alphaSP[1]/128) * alphaSP[0];
    
	mlx90640->cpAlpha[0] = alphaSP[0];
	mlx90640->cpAlpha[1] = alphaSP[1];
	
	// Kta CP coefficient
	float cpKta = (eeData[59] & 0x00FF);	// signed
    if (cpKta > 127) cpKta = cpKta - 256;

	uint8_t ktaScale1 = ((eeData[56] & 0x00F0) >> 4) + 8;
    mlx90640->cpKta = cpKta / pow(2, (double)ktaScale1);
    
    // Kv CP coefficient
	float cpKv = (eeData[59] & 0xFF00) >> 8;
    if (cpKv > 127) cpKv = cpKv - 256;

	uint8_t kvScale = (eeData[56] & 0x0F00) >> 8;	// unsigned

    mlx90640->cpKv = cpKv / pow(2, (double)kvScale);
 }

//------------------------------------------------------------------------------
// Chess Interleaved Line Correction
void ExtractCILCParameters(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
    uint8_t calibrationModeEE;
    
    calibrationModeEE = (eeData[10] & 0x0800) >> 4;
    calibrationModeEE = calibrationModeEE ^ 0x80;

	// either 0x80 or 0x00
	mlx90640->calibrationModeEE = calibrationModeEE;

	float ilChessC[3];
	
	ilChessC[0] = (eeData[53] & 0x003F);
    if (ilChessC[0] > 31) ilChessC[0] = ilChessC[0] - 64;
    ilChessC[0] = ilChessC[0] / 16.0f;
    
    ilChessC[1] = (eeData[53] & 0x07C0) >> 6;
    if (ilChessC[1] > 15) ilChessC[1] = ilChessC[1] - 32;
    ilChessC[1] = ilChessC[1] / 2.0f;
    
    ilChessC[2] = (eeData[53] & 0xF800) >> 11;
    if (ilChessC[2] > 15) ilChessC[2] = ilChessC[2] - 32;
    ilChessC[2] = ilChessC[2] / 8.0f;
    
    mlx90640->ilChessC[0] = ilChessC[0];
    mlx90640->ilChessC[1] = ilChessC[1];
    mlx90640->ilChessC[2] = ilChessC[2];
}

//------------------------------------------------------------------------------

int ExtractDeviatingPixels(uint16_t *eeData, paramsMLX90640 *mlx90640)
{
    uint16_t pixCnt = 0;
    uint16_t brokenPixCnt = 0;
    uint16_t outlierPixCnt = 0;
    int warn = 0;
    int i;
    
    for (pixCnt = 0; pixCnt<5; pixCnt++)
    {
        mlx90640->brokenPixels[pixCnt]  = 0xFFFF;
        mlx90640->outlierPixels[pixCnt] = 0xFFFF;
    }
        
    pixCnt = 0;    
    while (pixCnt < MLX90640_pixelCOUNT && brokenPixCnt < 5 && outlierPixCnt < 5)
    {
        if (eeData[pixCnt+64] == 0)
        {
            mlx90640->brokenPixels[brokenPixCnt] = pixCnt;
            brokenPixCnt = brokenPixCnt + 1;
        }    
        else if((eeData[pixCnt+64] & 0x0001) != 0)
        {
            mlx90640->outlierPixels[outlierPixCnt] = pixCnt;
            outlierPixCnt = outlierPixCnt + 1;
        }    
        
        pixCnt = pixCnt + 1;
    } 
    
    if (brokenPixCnt > 4) 
        warn = -3;
     else if (outlierPixCnt > 4)  
        warn = -4;
    else if ((brokenPixCnt + outlierPixCnt) > 4)  
        warn = -5;
    else
    {
        for (pixCnt=0; pixCnt<brokenPixCnt; pixCnt++)
        {
            for (i=pixCnt+1; i<brokenPixCnt; i++)
            {
                warn = CheckAdjacentPixels(mlx90640->brokenPixels[pixCnt],mlx90640->brokenPixels[i]);
                if (warn != 0) return warn;
            }    
        }
        
        for (pixCnt=0; pixCnt<outlierPixCnt; pixCnt++)
        {
            for (i=pixCnt+1; i<outlierPixCnt; i++)
            {
                warn = CheckAdjacentPixels(mlx90640->outlierPixels[pixCnt],mlx90640->outlierPixels[i]);
                if (warn != 0) return warn;  
            }    
        } 
        
        for (pixCnt=0; pixCnt<brokenPixCnt; pixCnt++)
        {
            for (i=0; i<outlierPixCnt; i++)
            {
                warn = CheckAdjacentPixels(mlx90640->brokenPixels[pixCnt],mlx90640->outlierPixels[i]);
                if (warn != 0) return warn;
            }    
        }    
    }
    
    return warn;
}

//------------------------------------------------------------------------------

 int CheckAdjacentPixels(uint16_t pix1, uint16_t pix2)
 {
     int pixPosDif = pix1 - pix2;

     if (pixPosDif > -34 && pixPosDif < -30)  return -6;
     if (pixPosDif >  -2 && pixPosDif <   2)  return -6;
     if (pixPosDif >  30 && pixPosDif <  34)  return -6;
     
     return 0;    
 }
 
 //------------------------------------------------------------------------------
 
 int CheckEEPROMvalid(uint16_t *eeData)  
 {
     int deviceSelect = eeData[10] & 0x0040;
     if (deviceSelect == 0) return 0;

     return -7;    
 }        
