
#ifndef CALIBRATION_H
#define CALIBRATION_H

#include "MLX90640_API.h"

// nice way to split away isolated code to another module
namespace MLXcalibration {

	void clearUserCalibrationOffsets();

	int  readUserCalibrationOffsets();
	void readUserCalibrationOffsetsDate(char* strDate);

	int  writeUserCalibrationOffsets(const char* httpDate);
	int  writeUserCalibrationOffsets(const char* httpDate, const char*  buf);

	int  setUserCalibrationOffsetsEnabled(uint8_t enabled);
	int  getUserCalibrationOffsetsEnabled();

	void applyUserCalibrationOffsets(mlx_fb_t& fb);
}

#endif
