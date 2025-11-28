
#include "httpd_capture_stream.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "sdkconfig.h"
#include "board_config.h"
#include "esp32-hal-log.h"
#include "esp32-hal-psram.h"
#include "esp32-hal-ledc.h"
#include "MLX90640_API.h"
#include "MLX90640_calibration.h"

bool isStreaming = false;
uint8_t mlx90640calibration_frame = 0;

#define CONFIG_LED_MAX_INTENSITY 255
int led_duty = 0;


typedef struct
{
	httpd_req_t*  req;
	size_t        len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_MULTIPART_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY               = "\r\n--" PART_BOUNDARY "\r\n";


// Turn LED On/Off
void enable_LED(bool en)
{
	int duty = en ? led_duty : 0;

	if (led_duty > CONFIG_LED_MAX_INTENSITY)
		duty = CONFIG_LED_MAX_INTENSITY;

	ledcWrite(LED_GPIO_NUM, duty);
	//ledc_set_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_LED_LEDC_CHANNEL, duty);
	//ledc_update_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_esp_err_tED_LEDC_CHANNEL);
	log_d("Set LED intensity to %d", duty);
}


// GET /bmp
// Input: req- valid request
esp_err_t bmp_handler(httpd_req_t *req)
{
	[[maybe_unused]] uint64_t fr_start = esp_timer_get_time();

	// lock spi memory dma buffer to read from
	camera_fb_t *fb = esp_camera_fb_get();
		if (!fb)
		{
			log_e("Camera capture failed");
			httpd_resp_send_500(req);
			return ESP_FAIL;
		}

		// request has httpd_req_aux structure holding temporarily response details
		httpd_resp_set_type(req, "image/x-windows-bmp");
		httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.bmp");
		httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

		char ts[32];
		snprintf(ts, 32, "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
		httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

		uint8_t *buf = NULL;
		size_t buf_len = 0;
		// calls fmt2bmp to convert camera supported types to BMP type
		// (JPEG is supported) allocates buf internally
		bool converted = frame2bmp(fb, &buf, &buf_len);

	// unlock spi dma buffer
	esp_camera_fb_return(fb);

	if (!converted)
	{
		log_e("BMP Conversion failed");
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}

	// send response
	esp_err_t res = httpd_resp_send(req, (const char *)buf, buf_len);

	free(buf);

	[[maybe_unused]] uint64_t fr_end = esp_timer_get_time();

	log_d("BMP: %llu ms, %ubytes", (uint64_t)((fr_end - fr_start) >> 10), buf_len);

	return res;
}


// Callback function for jpeg stream output in capture_handler
static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
	jpg_chunking_t *j = (jpg_chunking_t *)arg;

	if (!index) j->len = 0;

	// send data as soon as available in chunks, headers are sent only during first call
	// First or next call is saved in req->aux->first_chunk_sent
	if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK)
		return 0;

	j->len += len;

	return len;
}


// GET /capture2640
// Get image in JPEG format
//
// Input: req- valid request
esp_err_t ov2640_capture_handler(httpd_req_t *req)
{
	camera_fb_t *fb = NULL;
	esp_err_t res = ESP_OK;

	[[maybe_unused]] int64_t fr_start = esp_timer_get_time();

	log_i("/capture2640 received");

	enable_LED(true);
		// The LED needs to be turned on ~150ms before the call to esp_camera_fb_get()
		// or it won't be visible in the frame. A better way to do this is needed.
		vTaskDelay(150 / portTICK_PERIOD_MS);

		fb = esp_camera_fb_get();
	enable_LED(false);

		if (!fb)
		{
			log_e("Camera capture failed");
			httpd_resp_send_500(req);
			return ESP_FAIL;
		}

		httpd_resp_set_type(req, "image/jpeg");
		httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
		httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

		char ts[32];
		snprintf(ts, 32, "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
		httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

		[[maybe_unused]] size_t fb_len;

		if (fb->format == PIXFORMAT_JPEG)
		{
			log_d("PIXFORMAT == JPEG");

			fb_len = fb->len;

			res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
		}
		else
		{
			log_d("PIXFORMAT != JPEG");

			jpg_chunking_t jchunk = { req, 0 };
			res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;

			// marks the end of chunk stream
			httpd_resp_send_chunk(req, NULL, 0);

			fb_len = jchunk.len;
		}

	// unlock dma memory buffer
	esp_camera_fb_return(fb);

	[[maybe_unused]] int64_t fr_end = esp_timer_get_time();

	log_d("JPG: %ubytes %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) >> 10));

	return res;
}

// GET /capture90640
//
// Input: req- valid request
esp_err_t mlx90640_capture_handler(httpd_req_t *req)
{
	esp_err_t res;

	[[maybe_unused]] int64_t fr_start = esp_timer_get_time();

	log_i("/capture90640 received");

	MLX90640& mlx90640 = MLX90640::getInstance();

	mlx_fb_t fb = {};
	fb = mlx90640.fb_get();

		httpd_resp_set_type(req, HTTPD_TYPE_OCTET);
		httpd_resp_set_hdr(req,  "Content-Disposition", "inline; filename=capture.bmp");
		httpd_resp_set_hdr(req,  "Access-Control-Allow-Origin", "*");

		char ts[32];
		snprintf(ts, 32, "%lld.%06ld", fb.timestamp.tv_sec, fb.timestamp.tv_usec);
		httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

		MLXcalibration::applyUserCalibrationOffsets(fb);

		res = httpd_resp_send(req, (const char *)fb.values, fb.nBytes);

	mlx90640.fb_return(fb);

	[[maybe_unused]] int64_t fr_end = esp_timer_get_time();
	log_d("RAW: %ubytes %ums", (uint32_t)(fb.nBytes), (uint32_t)((fr_end - fr_start) >> 10));

	return res;
}

// GET /get_offsets90640
//
// Input: req- valid request
esp_err_t mlx90640_get_offsets_handler(httpd_req_t *req)
{
	esp_err_t res;

	[[maybe_unused]] int64_t fr_start = esp_timer_get_time();

	log_i("GET /get_offsets90640 received");

	MLX90640& mlx90640 = MLX90640::getInstance();

	mlx_ob_t ob = {};

	ob = mlx90640.ob_get();

		httpd_resp_set_type(req, HTTPD_TYPE_OCTET);
		httpd_resp_set_hdr(req,  "Content-Disposition", "inline; filename=capture.txt");
		httpd_resp_set_hdr(req,  "Access-Control-Allow-Origin", "*");

		char ts[32];
		snprintf(ts, 32, "%lld.%06ld", ob.timestamp.tv_sec, ob.timestamp.tv_usec);
		httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

		res = httpd_resp_send(req, (const char *)ob.offsets, ob.nBytes);

	mlx90640.ob_return(ob);

	[[maybe_unused]] int64_t fr_end = esp_timer_get_time();
	log_d("RAW: %ubytes %ums", (uint32_t)(ob.nBytes), (uint32_t)((fr_end - fr_start) >> 10));

	return res;
}


// POST /set_offsets90640
//
// Input: req- valid request
esp_err_t mlx90640_set_offsets_handler(httpd_req_t *req)
{
	log_i("POST /set_offsets90640 received");

	int remaining = req->content_len;
	log_i("Receiving data, size: %d", remaining);

	char* buf = (char*)ps_malloc(MLX90640_pixelCOUNT*sizeof(float));

	if (remaining != sizeof(buf))
	{
		log_e("Error: expected length %d", sizeof(buf));
		httpd_resp_send_500(req);

		free(buf);
		return ESP_FAIL;
	}

	char httpDate[64] = {};
	if (httpd_req_get_hdr_value_str(req, "X-Client-Date", httpDate, 64) != ESP_OK)
	{
		log_e("X-Client-Date is missing in request");
		httpd_resp_send_500(req);

		free(buf);
		return ESP_FAIL;
	}
	log_i("Received X-Client-Date: %s", httpDate);

	char* writePtr = buf;

	int iRetries = 0;
	while (remaining > 0)
	{
		int nRead = httpd_req_recv(req, writePtr, remaining);
		if (nRead <= 0)
		{
			if (nRead == HTTPD_SOCK_ERR_TIMEOUT) {
				if (iRetries++ < 3) continue; // retry
			}

			log_e("Receive error");
			httpd_resp_send_500(req);

			free(buf);
			return ESP_FAIL;
		}

		remaining -= nRead;
		writePtr  += nRead;
	}

	MLXcalibration::writeUserCalibrationOffsets(httpDate, buf);

	free(buf);

	return httpd_resp_sendstr(req, "Upload successful");
}


// GET :81/stream
//
// Input: req- valid request
esp_err_t stream2640_handler(httpd_req_t *req)
{
	struct timeval _timestamp;
	size_t   buffer_jpg_len = 0;
	uint8_t *buffer_jpg = NULL;

	static int64_t last_frame = 0;
	if (!last_frame) last_frame = esp_timer_get_time();

	log_i("GET :81/stream received");

	esp_err_t res;
	res = httpd_resp_set_type(req, _STREAM_MULTIPART_CONTENT_TYPE);
	if (res != ESP_OK) return res;

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

	isStreaming = true;
	enable_LED(true);
	
	camera_fb_t* fb = NULL;
	while (true)
	{
		fb = esp_camera_fb_get();
		if (!fb) {
			log_e("Camera capture failed");
			res = ESP_FAIL;
			break;
		}

		_timestamp.tv_sec  = fb->timestamp.tv_sec;
		_timestamp.tv_usec = fb->timestamp.tv_usec;

		if (fb->format != PIXFORMAT_JPEG)
		{
			bool jpeg_converted = frame2jpg(fb, 80, &buffer_jpg, &buffer_jpg_len);

			esp_camera_fb_return(fb);
			fb = NULL;

			if (!jpeg_converted) {
				log_e("JPEG compression failed");
				res = ESP_FAIL;
			}
		}
		else {
			buffer_jpg     = fb->buf;
			buffer_jpg_len = fb->len;
		}

		// --boundary
		if (res == ESP_OK)
			res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

		if (res == ESP_OK)
		{
			char* bufferHeader = (char*)ps_malloc(256);
				size_t hlen = snprintf(bufferHeader, 256,
									   "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %lld.%06ld\r\n\r\n",
									   buffer_jpg_len, _timestamp.tv_sec, _timestamp.tv_usec);

				// Content-Type: type
				// Content-Length: len
				// X-Timestamp:
				// new line
				res = httpd_resp_send_chunk(req, bufferHeader, hlen);
			free(bufferHeader);
		}

		// Data
		if (res == ESP_OK)
			res = httpd_resp_send_chunk(req, (const char *)buffer_jpg, buffer_jpg_len);

		if (fb)
		{
			esp_camera_fb_return(fb);
			fb = NULL;
			buffer_jpg = NULL;	// pointer to fb internal structure
		}
		else if (buffer_jpg)
		{
			free(buffer_jpg);
			buffer_jpg = NULL;
		}

		if (res != ESP_OK) {
			log_e("Frame sending failed");
			break;
		}

		int64_t fr_end = esp_timer_get_time();
		[[maybe_unused]] int64_t frame_time = (fr_end - last_frame) / 1000;
		last_frame = fr_end;

		log_d("MJPG: %ubytes %ums (%.1ffps)", (uint32_t)(buffer_jpg_len),
			                                  (uint32_t)frame_time,
			                                  1000.0 / (uint32_t)frame_time );
	}

	isStreaming = false;
	enable_LED(false);

	return res;
}

// GET :82/stream
//
// Input: req- valid request
esp_err_t stream90640_handler(httpd_req_t *req)
{
	static int64_t last_frame = 0;
	if (!last_frame) last_frame = esp_timer_get_time();

	log_i("GET :82/stream received");

	esp_err_t res;
	res = httpd_resp_set_type(req, _STREAM_MULTIPART_CONTENT_TYPE);
	if (res != ESP_OK) return res;

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

	MLX90640& mlx90640 = MLX90640::getInstance();

	mlx_fb_t fb = {};
	while (true)
	{
		fb = mlx90640.fb_get();

			res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
			if (res == ESP_OK)
			{
				char* bufferHeader = (char*)ps_malloc(256);
					size_t hlen = snprintf(bufferHeader, 256,
										   "Content-Type: application/octet-stream\r\nContent-Length: %u\r\nX-Timestamp: %lld.%06ld\r\n\r\n",
										   fb.nBytes, fb.timestamp.tv_sec, fb.timestamp.tv_usec);

					res = httpd_resp_send_chunk(req, bufferHeader, hlen);
				free(bufferHeader);
			}

			// if calibration is in progress
			if (mlx90640calibration_frame > 0) {
				mlx90640calibration_frame++;

				for (uint16_t i = 0; i < MLX90640_pixelCOUNT; i++) {
					fb.offsets[i] += fb.values[i]/100.0f - fb.fTambientReflected/100.0f;
				}

				if (mlx90640calibration_frame > 100)
					// disable calibration after 100 full frames
					mlx90640calibration_frame = 0;
			}

			MLXcalibration::applyUserCalibrationOffsets(fb);

			if (res == ESP_OK)
				res = httpd_resp_send_chunk(req, (const char *)fb.values, fb.nBytes);

		mlx90640.fb_return(fb);

		if (res != ESP_OK) {
			log_e("Send frame failed");
			break;
		}

		int64_t fr_end = esp_timer_get_time();
		[[maybe_unused]] int64_t frame_time = (fr_end - last_frame) / 1000;
		last_frame = fr_end;

		log_d("RAW: %ubytes %ums (%.1ffps)", (uint32_t)(fb.nBytes),
										     (uint32_t)frame_time,
										     1000.0 / (uint32_t)frame_time );
	}

	return res;
}
