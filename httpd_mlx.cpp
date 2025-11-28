
#include "httpd_mlx.h"
#include "MLX90640_API.h"
#include "MLX90640_calibration.h"
#include "esp32-hal-log.h"
#include "Arduino.h"

extern esp_err_t parse_get(httpd_req_t *req, char **obuf);
extern bool isStreaming;

extern uint8_t mlx90640calibration_frame;

// GET /mlx
esp_err_t mlx_handler(httpd_req_t *req)
{
	char variable[32];
	char value[32];

	char *buf = NULL;
	if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;

		// httpd_query_key_value is a helper function to obtain a URL query tag from a query string
		// of the format param1=val1&param2=val2
		//
		// `${baseHost}/mlx?var=${el.id}&val=${value}`;
		if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK ||
			httpd_query_key_value(buf, "val", value,    sizeof(value))    != ESP_OK)
		{
			free(buf);
			httpd_resp_send_404(req);

			return ESP_FAIL;
		}

	free(buf);

	MLX90640& mlx90640 = MLX90640::getInstance();

	if (!strcmp(variable, "ambReflected"))
	{
		float ambReflected = atof(value);
		log_i("ambReflected: %f C", ambReflected);

		mlx90640.SetAmbientReflected(ambReflected);

		httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
		return httpd_resp_send(req, NULL, 0);
	}
	else if (!strcmp(variable, "emissivity"))
	{
		float fEmissivity = atof(value);
		log_i("emissivity: %f", fEmissivity);

		mlx90640.SetEmissivity(fEmissivity);

		httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
		return httpd_resp_send(req, NULL, 0);
	}
	else if (!strcmp(variable, "device_voltage"))
	{
		log_i("device_voltage");

		float vdd = mlx90640.GetVddRAM();

		char str_vdd[10];
		snprintf(str_vdd, 10, "%.2f", vdd);

		httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
		return httpd_resp_send(req, (const char*)str_vdd, strlen(str_vdd));
	}
	else if (!strcmp(variable, "device_temperature"))
	{
		log_i("device_temperature");

		float ta = mlx90640.GetTaRAM();

		char str_ta[10];
		snprintf(str_ta, 10, "%.2f", ta);

		httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
		return httpd_resp_send(req, (const char*)&str_ta, strlen(str_ta));
	}
	else if (!strcmp(variable, "calibrate"))
	{
		esp_err_t res;

		char httpDate[64] = {};
		if (httpd_req_get_hdr_value_str(req, "X-Client-Date", httpDate, 64) != ESP_OK) {
			log_e("X-Client-Date is missing in request");
			httpd_resp_send_500(req);
			return ESP_FAIL;
		}
		log_i("Received X-Client-Date: %s", httpDate);
		
		float fMeanTemp = atof(value);
		log_i("Calibrating to %f mean temperature", fMeanTemp);

		if (!isStreaming)
			return httpd_resp_send_500(req);

		if (!mlx90640.IsOnline())
			return httpd_resp_send_500(req);

		MLXcalibration::clearUserCalibrationOffsets();
		mlx90640calibration_frame = 1;

		httpd_resp_set_type(req, HTTPD_TYPE_OCTET);
		httpd_resp_set_hdr(req,  "Access-Control-Allow-Origin", "*");
		httpd_resp_set_hdr(req,  "Transfer-Encoding", "chunked");

		int j = 0;
		while (true)
		{
			// send progress
			res = httpd_resp_send_chunk(req, (const char*)&mlx90640calibration_frame, 1);

			if (res != ESP_OK) {
				log_e("Sending status failed");
				mlx90640calibration_frame = 0;
				return res;
			}

			// stream is ongoing
			delay(1000);

			// prevent infinite loop (theoretical code)
			// 05HZ in the worst case gives ~400 (one full frame consists of two subpages)
			if (j++ > 800)
			{
				mlx90640calibration_frame = 0;
				// Finalize chunked response
				return httpd_resp_send_chunk(req, NULL, 0);
			}

			// mlx90640calibration_frame is reset to 0 in stream90640_handler after 100 frames captured
			if (mlx90640calibration_frame == 0) break;
		}

		// success

		res = MLXcalibration::writeUserCalibrationOffsets(httpDate);
		if (res != ESP_OK) {
			// abort connection
			int sockfd = httpd_req_to_sockfd(req);
			close(sockfd);
		}

		// Finalize chunked response
		return httpd_resp_send_chunk(req, NULL, 0);
	}
	else
	{
		log_i("Unknown command: %s", variable);
		return httpd_resp_send_500(req);
	}

}

