
#include "MLX90640_calibration.h"
#include "MLX90640_API.h"
#include "SPIFFS.h"

extern float mlx90640_float_offsets[MLX90640_pixelCOUNT];

void clear_user_calibration_offsets()
{
	memset(mlx90640_float_offsets, 0, sizeof(mlx90640_float_offsets));
}

int read_user_calibration_offsets()
{
	memset(mlx90640_float_offsets, 0, sizeof(mlx90640_float_offsets));

	File file;

	// Read calibration file from SPIFFS
	const char* pathFile = "/calibration.txt";
	file = SPIFFS.open(pathFile, "r");

	if (!file || !file.available()) {
		log_e("Failed to open file %s, defaulting to zero user offsets", pathFile);
		file.close();
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

	if (len != sizeof(mlx90640_float_offsets)) {
		log_e("Error reading file %s, defaulting to zero user offsets", pathFile);
		memset(mlx90640_float_offsets, 0, sizeof(mlx90640_float_offsets));
		file.close();
		return 3;
	}

	file.close();

	return 0;
}


int write_user_calibration_offsets()
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

	return ESP_OK;
}
