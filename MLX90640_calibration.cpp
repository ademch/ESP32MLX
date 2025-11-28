
#include "MLX90640_calibration.h"
#include "SPIFFS.h"

#include <string.h>


extern float mlx90640_float_offsets[MLX90640_pixelCOUNT];

namespace MLXcalibration
{

	static uint8_t bObserveOffsetAdjustment = 1;		// static hides variable from extern keyword access


	void clearUserCalibrationOffsets()
	{
		memset(mlx90640_float_offsets, 0, sizeof(mlx90640_float_offsets));
	}


	int readUserCalibrationOffsets()
	{
		memset(mlx90640_float_offsets, 0, sizeof(mlx90640_float_offsets));

		File file;

		// Read calibration file from SPIFFS
		const char* pathFile = "/calibration.txt";
		file = SPIFFS.open(pathFile, "r");

		if (!file || !file.available()) {
			log_e("Failed to open file %s, defaulting to zero user offsets", pathFile);
			return 1;
		}

		long size = file.size();

		if (size != sizeof(mlx90640_float_offsets))
		{
			log_e("User calibration file %s is corrupted, defaulting to zero user offsets", pathFile);
			file.close();
			return 2;
		}

		size_t len = file.readBytes((char*)mlx90640_float_offsets, sizeof(mlx90640_float_offsets));
		log_i("Read %u bytes out of %u from %s", len, sizeof(mlx90640_float_offsets), pathFile);

		if (len != sizeof(mlx90640_float_offsets))
		{
			log_e("Error reading file %s, defaulting to zero user offsets", pathFile);
			memset(mlx90640_float_offsets, 0, sizeof(mlx90640_float_offsets));
			file.close();
			return 3;
		}

		file.close();

		return 0;
	}


	int writeUserCalibrationOffsets(const char* httpDate, const char*  buf)
	{
		memcpy(mlx90640_float_offsets, buf, MLX90640_pixelCOUNT*sizeof(float));

		return writeUserCalibrationOffsets(httpDate);
	}


	int writeUserCalibrationOffsets(const char* httpDate)
	{
		File fd;

		const char* pathFile = "/calibration.txt";
		fd = SPIFFS.open(pathFile, "w");
		if (!fd) {
			log_e("Failed to open %s file for writing", pathFile);
			return ESP_FAIL;
		}

		size_t len = fd.write((uint8_t*)mlx90640_float_offsets, sizeof(mlx90640_float_offsets));

		fd.close();

		log_i("File %s saved to SPIFFS taking %ubytes", pathFile, len);


		//- WRITE CLIENT DATE TO FILE----------------------------------------------

		const char* pathTimestamp = "/calibrationTS.txt";
		fd = SPIFFS.open(pathTimestamp, "w");
		if (!fd) {
			log_e("Failed to open %s file for writing", pathTimestamp);
			return ESP_FAIL;
		}

		fd.print(httpDate);	// long

		fd.close();

		log_i("Timestamp written to %s", pathTimestamp);

		return ESP_OK;
	}


	void readUserCalibrationOffsetsDate(char* strDate)
	{
		File fd;
		const char* pathTimestampFile = "/calibrationTS.txt";
		fd = SPIFFS.open(pathTimestampFile, "r");

		// if file does not exist
		if (!fd || !fd.available()) {
			log_e("Failed to open file %s", pathTimestampFile);
			snprintf(strDate, 32, "never");
			return;
		}

		String strSPIFFSTimestamp = fd.readString();
		log_i("File content: %s", strSPIFFSTimestamp.c_str());

		fd.close();

		snprintf(strDate, 32, "%s", strSPIFFSTimestamp.c_str());
	}


	int setUserCalibrationOffsetsEnabled(uint8_t enabled)
	{
		bObserveOffsetAdjustment = enabled;

		return 0;
	}

	int getUserCalibrationOffsetsEnabled()
	{
		return bObserveOffsetAdjustment;
	}


	void applyUserCalibrationOffsets(mlx_fb_t& fb)
	{
		if (!bObserveOffsetAdjustment) return;

		for (uint16_t i = 0; i < MLX90640_pixelCOUNT; i++) {
			fb.values[i] -= fb.offsets[i];
		}
	}

}