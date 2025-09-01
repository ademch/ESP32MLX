
#include "httpd_capture_stream.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "sdkconfig.h"
#include "board_config.h"
#include "esp32-hal-log.h"
#include "esp32-hal-ledc.h"
#include "MLX90640_API.h"

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
	log_i("Set LED intensity to %d", duty);
}


// GET /bmp
// Input: req- valid request
esp_err_t bmp_handler(httpd_req_t *req)
{
	uint64_t fr_start = esp_timer_get_time();

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

	uint64_t fr_end = esp_timer_get_time();

	log_i("BMP: %llu ms, %ubytes", (uint64_t)((fr_end - fr_start) >> 10), buf_len);

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


// GET /capture
// Get image in JPEG format
//
// Input: req- valid request
esp_err_t ov2640_capture_handler(httpd_req_t *req)
{
	camera_fb_t *fb = NULL;
	esp_err_t res = ESP_OK;

	int64_t fr_start = esp_timer_get_time();

	log_i("/capture received");

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

		size_t fb_len = 0;

		if (fb->format == PIXFORMAT_JPEG)
		{
			log_i("PIXFORMAT == JPEG");

			fb_len = fb->len;

			res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
		}
		else
		{
			log_i("PIXFORMAT != JPEG");

			jpg_chunking_t jchunk = { req, 0 };
			res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;

			// marks the end of chunk stream
			httpd_resp_send_chunk(req, NULL, 0);

			fb_len = jchunk.len;
		}

	// unlock dma memory buffer
	esp_camera_fb_return(fb);

	int64_t fr_end = esp_timer_get_time();

	log_i("JPG: %ubytes %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) >> 10));

	return res;
}

// GET /capture90640
// Get image in BMP format
//
// Input: req- valid request
esp_err_t mlx90640_capture_handler(httpd_req_t *req)
{
	esp_err_t res;

	int64_t fr_start = esp_timer_get_time();

	log_i("/capture90640 received");

	mlx_fb_t fb = {};

	fb = MLX90640_fb_get();

		httpd_resp_set_type(req, "image/bmp");
		httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.bmp");
		httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

		char ts[32];
		snprintf(ts, 32, "%lld.%06ld", fb.timestamp.tv_sec, fb.timestamp.tv_usec);
		httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

		res = httpd_resp_send(req, (const char *)fb.values, fb.nBytes);

	MLX90640_fb_return(fb);

	int64_t fr_end = esp_timer_get_time();
	log_i("RAW: %ubytes %ums", (uint32_t)(fb.nBytes), (uint32_t)((fr_end - fr_start) >> 10));

	return res;
}


// GET /stream
//
// Input: req- valid request
esp_err_t stream2640_handler(httpd_req_t *req)
{
	struct timeval _timestamp;
	size_t   buffer_jpg_len = 0;
	uint8_t *buffer_jpg = NULL;

	static int64_t last_frame = 0;
	if (!last_frame) last_frame = esp_timer_get_time();

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
			char bufferHeader[256];
			size_t hlen = snprintf(bufferHeader, 256,
								   "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %lld.%06ld\r\n\r\n",
								   buffer_jpg_len, _timestamp.tv_sec, _timestamp.tv_usec);

			// Content-Type: type
			// Content-Length: len
			// X-Timestamp:
			// new line
			res = httpd_resp_send_chunk(req, bufferHeader, hlen);
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
		int64_t frame_time = (fr_end - last_frame) / 1000;
		last_frame = fr_end;

		log_i("MJPG: %ubytes %ums (%.1ffps)", (uint32_t)(buffer_jpg_len),
			                                  (uint32_t)frame_time,
			                                  1000.0 / (uint32_t)frame_time );
	}

	isStreaming = false;
	enable_LED(false);

	return res;
}

// GET /stream:82
//
// Input: req- valid request
esp_err_t stream90640_handler(httpd_req_t *req)
{
	static int64_t last_frame = 0;
	if (!last_frame) last_frame = esp_timer_get_time();

	esp_err_t res;
	res = httpd_resp_set_type(req, _STREAM_MULTIPART_CONTENT_TYPE);
	if (res != ESP_OK) return res;

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

	mlx_fb_t fb = {};
	while (true)
	{
		fb = MLX90640_fb_get();

			res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
			if (res == ESP_OK)
			{
				char bufferHeader[236];
				size_t hlen = snprintf(bufferHeader, 256,
									   "Content-Type: application/octet-stream\r\nContent-Length: %u\r\nX-Timestamp: %lld.%06ld\r\n\r\n",
									   fb.nBytes, fb.timestamp.tv_sec, fb.timestamp.tv_usec);

				res = httpd_resp_send_chunk(req, bufferHeader, hlen);
			}

			// if calibration is in progress
			if (mlx90640calibration_frame > 0) {
				mlx90640calibration_frame++;

				for (uint16_t i = 0; i < MLX90640_pixelCOUNT; i++) {
					fb.offsets[i] += fb.values[i]/100.0f - fb.TambientReflected/100.0f;
				}

				if (mlx90640calibration_frame > 100)
					// disable calibration after 100 frames
					mlx90640calibration_frame = 0;
			}

			// apply user calibration offsets
			for (uint16_t i = 0; i < MLX90640_pixelCOUNT; i++) {
				fb.values[i] -= fb.offsets[i];
			}

			if (res == ESP_OK)
				res = httpd_resp_send_chunk(req, (const char *)fb.values, fb.nBytes);

		MLX90640_fb_return(fb);

		if (res != ESP_OK) {
			log_e("Send frame failed");
			break;
		}

		int64_t fr_end = esp_timer_get_time();
		int64_t frame_time = (fr_end - last_frame) / 1000;
		last_frame = fr_end;

		log_i("RAW: %ubytes %ums (%.1ffps)", (uint32_t)(fb.nBytes),
										     (uint32_t)frame_time,
										     1000.0 / (uint32_t)frame_time );
	}

	return res;
}
