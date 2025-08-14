// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "esp32-hal-ledc.h"
#include "sdkconfig.h"
#include "camera_index.h"
#include "board_config.h"
#include "MLX90640_API.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
	#include "esp32-hal-log.h"
#endif

// LED FLASH setup
#if defined(LED_GPIO_NUM)
	#define CONFIG_LED_MAX_INTENSITY 255

	int led_duty = 0;
	bool isStreaming = false;
#endif

httpd_handle_t stream_httpd = NULL;
httpd_handle_t control_httpd = NULL;
httpd_handle_t mlxthc_httpd = NULL;

typedef struct
{
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_JPG_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";
static const char *_STREAM_BMP_PART = "Content-Type: image/bmp\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";


// ARDUHAL_LOG_LEVEL_NON-> ARDUHAL_LOG_LEVEL_ERROR -> ARDUHAL_LOG_LEVEL_WARN ->
// ARDUHAL_LOG_LEVEL_INFO -> ARDUHAL_LOG_LEVEL_DEBUG -> ARDUHAL_LOG_LEVEL_VERBOSE
//
// ARDUHAL_LOG_LEVEL is the current log level set for ESP32 project in Arduino
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO

	// running average number of samples
	#define ra_nSamples 20
	typedef struct {
		size_t index;  // next value index

		int sum;
		int values[ra_nSamples];
	} RunningAverage_t;

	static RunningAverage_t runningAverage = {};

	static int RunningAverage_run(RunningAverage_t *ra, int value)
	{
	  // remove overwritten value from the sum
	  ra->sum -= ra->values[ra->index];
	  // update the value
	  ra->values[ra->index] = value;
	  // add to the sum
	  ra->sum += ra->values[ra->index];
	  // move the pointer to the next value
	  ra->index++;
	  ra->index = ra->index % ra_nSamples;

	  return ra->sum / ra_nSamples;
	}
#endif

#if defined(LED_GPIO_NUM)
	// Turn LED On/Off
	void enable_led(bool en)
	{
		int duty = en ? led_duty : 0;
	
		if (en && isStreaming && (led_duty > CONFIG_LED_MAX_INTENSITY))
			duty = CONFIG_LED_MAX_INTENSITY;
	  	
		ledcWrite(LED_GPIO_NUM, duty);
	    //ledc_set_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_LED_LEDC_CHANNEL, duty);
	    //ledc_update_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_esp_err_tED_LEDC_CHANNEL);
	    log_i("Set LED intensity to %d", duty);
	}
#endif

// GET /bmp
// Input: req- valid request
static esp_err_t bmp_handler(httpd_req_t *req)
{
  #if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    uint64_t fr_start = esp_timer_get_time();
  #endif
  
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
	  // calls fmt2bmp to convert camera supported types to BMP type (JPEG is supported)
	  // allocates buf internally
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

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  uint64_t fr_end = esp_timer_get_time();
  log_i("BMP: %llums, %uB", (uint64_t)((fr_end - fr_start) / 1000), buf_len); 
#endif

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
static esp_err_t capture_handler(httpd_req_t *req)
{
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  int64_t fr_start = esp_timer_get_time();
#endif

  log_i("/capture received");

#if defined(LED_GPIO_NUM)
  enable_led(true);
  // The LED needs to be turned on ~150ms before the call to esp_camera_fb_get()
  // or it won't be visible in the frame. A better way to do this is needed.
  vTaskDelay(150 / portTICK_PERIOD_MS);  
  fb = esp_camera_fb_get();
  enable_led(false);
#else
  fb = esp_camera_fb_get();
#endif

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

  #if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    size_t fb_len = 0;
  #endif

  if (fb->format == PIXFORMAT_JPEG)
  {
    log_i("PIXFORMAT == JPEG");

	#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
      fb_len = fb->len;
    #endif
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  }
  else
  {
	log_i("PIXFORMAT != JPEG");
	  
	jpg_chunking_t jchunk = {req, 0};
    res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;

    // marks the end of chunk stream
	httpd_resp_send_chunk(req, NULL, 0);

	#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
		fb_len = jchunk.len;
	#endif
  }

  // unlock dma memory buffer
  esp_camera_fb_return(fb);

  #if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    int64_t fr_end = esp_timer_get_time();
	log_i("JPG: %u kb %ums", (uint32_t)(fb_len) >> 10, (uint32_t)((fr_end - fr_start) / 1000));
  #endif

  return res;
}

// GET /stream
//
// Input: req- valid request
static esp_err_t stream2640_handler(httpd_req_t *req)
{
    struct timeval _timestamp;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;

    #if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
        static int64_t last_frame = 0;
	    if (!last_frame) last_frame = esp_timer_get_time();
    #endif

    esp_err_t res;
    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    #if defined(LED_GPIO_NUM)
        isStreaming = true;
        enable_led(true);
    #endif

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
            bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
    
            esp_camera_fb_return(fb);
            fb = NULL;
    
            if (!jpeg_converted)
	  	    {
                log_e("JPEG compression failed");
                res = ESP_FAIL;
            }
        }
	    else {
	  	    _jpg_buf = fb->buf;
	  	    _jpg_buf_len = fb->len;
        }
    
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    
        if (res == ESP_OK)
	    {
	  	    char *part_buf[128];
    
	  	    size_t hlen = snprintf((char *)part_buf, 128,
									_STREAM_JPG_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
	  	    res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
    
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    
        if (fb)
	    {
	  	    esp_camera_fb_return(fb);
	  	    fb = NULL;
	  	    _jpg_buf = NULL;
        }
	    else if (_jpg_buf)
	    {
	  	    free(_jpg_buf);
	  	    _jpg_buf = NULL;
        }
    
        if (res != ESP_OK) {
            log_e("Send frame failed");
            break;
        }
    
	    #if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
	  		int64_t fr_end = esp_timer_get_time();
	  		int64_t frame_time = (fr_end - last_frame) / 1000;
	  		last_frame = fr_end;
    
			uint32_t avg_frame_time = RunningAverage_run(&runningAverage, frame_time);
    
	  		log_i("MJPG: %u kb %ums (%.1ffps), AVG: %ums (%.1ffps)", (uint32_t)(_jpg_buf_len) >> 10,
	  			  (uint32_t)frame_time,
	  			  1000.0 / (uint32_t)frame_time,
	  			  avg_frame_time,
	  			  1000.0 / avg_frame_time
	  			 );
	    #endif
    }

	#if defined(LED_GPIO_NUM)
		isStreaming = false;
		enable_led(false);
	#endif

	return res;
}

// GET /stream:82
//
// Input: req- valid request
static esp_err_t stream90640_handler(httpd_req_t *req)
{
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
	static int64_t last_frame = 0;
	if (!last_frame) last_frame = esp_timer_get_time();
#endif

	esp_err_t res;
	res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
	if (res != ESP_OK) return res;

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_hdr(req, "X-Framerate", "60");

	mlx_fb_t fb = {};
	while (true)
	{
		fb = MLX90640_fb_get();

			res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
			if (res == ESP_OK)
			{
				char *part_buf[128];

				size_t hlen = snprintf((char *)part_buf, 128,
										_STREAM_BMP_PART, fb.len, fb.timestamp.tv_sec, fb.timestamp.tv_usec);
				res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
			}

			if (res == ESP_OK)
				res = httpd_resp_send_chunk(req, (const char *)fb.buf, fb.len);

		MLX90640_fb_return(fb);

		if (res != ESP_OK) {
			log_e("Send frame failed");
			break;
		}

		#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
			int64_t fr_end = esp_timer_get_time();
			int64_t frame_time = (fr_end - last_frame) / 1000;
			last_frame = fr_end;

			uint32_t avg_frame_time = RunningAverage_run(&runningAverage, frame_time);

			log_i("MJPG: %u kb %ums (%.1ffps), AVG: %ums (%.1ffps)", (uint32_t)(fb.len) >> 10,
				  (uint32_t)frame_time,
				  1000.0 / (uint32_t)frame_time,
				  avg_frame_time,
				  1000.0 / avg_frame_time
			     );
		#endif
	}

	return res;
}

// GET can have query string comming after ?, eg GET /search?query=esp32&lang=en
static esp_err_t parse_get(httpd_req_t *req, char **obuf)
{
  char *buf = NULL;

  size_t buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1)
  {
    buf = (char *)malloc(buf_len);
    if (!buf)
	{
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
	{
      *obuf = buf;
      return ESP_OK;
    }

	// fail
	free(buf);
  }
  
  httpd_resp_send_404(req);
  
  return ESP_FAIL;
}

static esp_err_t control_handler(httpd_req_t *req)
{
  char variable[32];
  char value[32];

  char *buf = NULL;
  if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;

  // httpd_query_key_value is a helper function to obtain a URL query tag from a query string
  // of the format param1=val1&param2=val2
  if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK ||
	  httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK)
  {
    free(buf);
    httpd_resp_send_404(req);
  
	return ESP_FAIL;
  }
  
  free(buf);

  int val = atoi(value);
  log_i("%s = %d", variable, val);
  
  sensor_t *s = esp_camera_sensor_get();
  
  int res = 0;
  if (!strcmp(variable, "framesize"))
  {
    if (s->pixformat == PIXFORMAT_JPEG)
      res = s->set_framesize(s, (framesize_t)val);	// resolution enum
  }
  else if (!strcmp(variable, "quality"))
    res = s->set_quality(s, val);
  else if (!strcmp(variable, "contrast"))
    res = s->set_contrast(s, val);
  else if (!strcmp(variable, "brightness"))
    res = s->set_brightness(s, val);
  else if (!strcmp(variable, "saturation"))
    res = s->set_saturation(s, val);
  else if (!strcmp(variable, "gainceiling"))
    res = s->set_gainceiling(s, (gainceiling_t)val);
  else if (!strcmp(variable, "colorbar"))
    res = s->set_colorbar(s, val);
  else if (!strcmp(variable, "awb"))
    res = s->set_whitebal(s, val);
  else if (!strcmp(variable, "agc"))
    res = s->set_gain_ctrl(s, val);
  else if (!strcmp(variable, "aec"))
    res = s->set_exposure_ctrl(s, val);
  else if (!strcmp(variable, "hmirror"))
    res = s->set_hmirror(s, val);
  else if (!strcmp(variable, "vflip"))
    res = s->set_vflip(s, val);
  else if (!strcmp(variable, "awb_gain"))
    res = s->set_awb_gain(s, val);
  else if (!strcmp(variable, "agc_gain"))
    res = s->set_agc_gain(s, val);
  else if (!strcmp(variable, "aec_value"))
    res = s->set_aec_value(s, val);
  else if (!strcmp(variable, "aec2"))
    res = s->set_aec2(s, val);
  else if (!strcmp(variable, "dcw"))
    res = s->set_dcw(s, val);
  else if (!strcmp(variable, "bpc"))
    res = s->set_bpc(s, val);
  else if (!strcmp(variable, "wpc"))
    res = s->set_wpc(s, val);
  else if (!strcmp(variable, "raw_gma"))
    res = s->set_raw_gma(s, val);
  else if (!strcmp(variable, "lenc"))
    res = s->set_lenc(s, val);
  else if (!strcmp(variable, "special_effect"))
    res = s->set_special_effect(s, val);
  else if (!strcmp(variable, "wb_mode"))
    res = s->set_wb_mode(s, val);
  else if (!strcmp(variable, "ae_level"))
    res = s->set_ae_level(s, val);
  #if defined(LED_GPIO_NUM)
  else if (!strcmp(variable, "led_intensity"))
  {
    led_duty = val;
    if (isStreaming) enable_led(true);
  }
  #endif
  else {
    log_i("Unknown command: %s", variable);
    res = -1;
  }

  if (res < 0) return httpd_resp_send_500(req);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  return httpd_resp_send(req, NULL, 0);
}


static int print_reg(char *p, sensor_t *s, uint16_t reg, uint32_t mask)
{
  return sprintf(p, "\"0x%x\":%u,", reg, s->get_reg(s, reg, mask));
}


static esp_err_t status_handler(httpd_req_t *req)
{
  static char json_response[1024];

  sensor_t *s = esp_camera_sensor_get();
  char *p = json_response;
  *p++ = '{';

  if (s->id.PID == OV5640_PID || s->id.PID == OV3660_PID)
  {
    for (int reg = 0x3400; reg < 0x3406; reg += 2)
      p += print_reg(p, s, reg, 0xFFF);     //12 bit
 
    p += print_reg(p, s, 0x3406, 0xFF);

    p += print_reg(p, s, 0x3500, 0xFFFF0);  //16 bit
    p += print_reg(p, s, 0x3503, 0xFF);
    p += print_reg(p, s, 0x350a, 0x3FF);    //10 bit
    p += print_reg(p, s, 0x350c, 0xFFFF);   //16 bit

    for (int reg = 0x5480; reg <= 0x5490; reg++)
      p += print_reg(p, s, reg, 0xFF);

    for (int reg = 0x5380; reg <= 0x538b; reg++)
      p += print_reg(p, s, reg, 0xFF);

    for (int reg = 0x5580; reg < 0x558a; reg++)
      p += print_reg(p, s, reg, 0xFF);
 
    p += print_reg(p, s, 0x558a, 0x1FF);    //9 bit
  }
  else if (s->id.PID == OV2640_PID)
  {
    p += print_reg(p, s, 0xd3, 0xFF);
    p += print_reg(p, s, 0x111, 0xFF);
    p += print_reg(p, s, 0x132, 0xFF);
  }

  p += sprintf(p, "\"xclk\":%u,", s->xclk_freq_hz / 1000000);
  p += sprintf(p, "\"pixformat\":%u,", s->pixformat);
  p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
  p += sprintf(p, "\"quality\":%u,", s->status.quality);
  p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
  p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
  p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
  p += sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
  p += sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
  p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
  p += sprintf(p, "\"awb\":%u,", s->status.awb);
  p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
  p += sprintf(p, "\"aec\":%u,", s->status.aec);
  p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
  p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
  p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
  p += sprintf(p, "\"agc\":%u,", s->status.agc);
  p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
  p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
  p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
  p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
  p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
  p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
  p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
  p += sprintf(p, "\"vflip\":%u,", s->status.vflip);
  p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
  p += sprintf(p, "\"colorbar\":%u", s->status.colorbar);
#if defined(LED_GPIO_NUM)
  p += sprintf(p, ",\"led_intensity\":%u", led_duty);
#else
  p += sprintf(p, ",\"led_intensity\":%d", -1);
#endif
  *p++ = '}';
  *p++ = 0;		// end of the string

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  return httpd_resp_send(req, json_response, strlen(json_response));
}

// GET /xclk
static esp_err_t xclk_handler(httpd_req_t *req)
{
  char _xclk[32];

  char *buf = NULL;
  if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;

  // httpd_query_key_value is a helper function to obtain a URL query tag from a query string
  // of the format param1=val1&param2=val2
  if (httpd_query_key_value(buf, "xclk", _xclk, sizeof(_xclk)) != ESP_OK)
  {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  free(buf);

  int xclk = atoi(_xclk);
  log_i("Set XCLK: %d MHz", xclk);

  sensor_t *s = esp_camera_sensor_get();

  int res = s->set_xclk(s, LEDC_TIMER_0, xclk);
  
  if (res) return httpd_resp_send_500(req);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  return httpd_resp_send(req, NULL, 0);
}

// GET /reg
static esp_err_t reg_handler(httpd_req_t *req)
{
  char _reg[32];
  char _mask[32];
  char _val[32];

  char *buf = NULL;
  if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;

  // httpd_query_key_value is a helper function to obtain a URL query tag from a query string
  // of the format param1=val1&param2=val2
  if (httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK ||
	  httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK ||
      httpd_query_key_value(buf, "val", _val, sizeof(_val)) != ESP_OK)
  {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int reg = atoi(_reg);
  int mask = atoi(_mask);
  int val = atoi(_val);
  log_i("Set Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, val);

  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_reg(s, reg, mask, val);
  
  if (res) return httpd_resp_send_500(req);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  return httpd_resp_send(req, NULL, 0);
}

// GET /greg
// Get register handler
static esp_err_t greg_handler(httpd_req_t *req)
{
  char _reg[32];
  char _mask[32];

  char *buf = NULL;
  if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;

  if (httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK ||
	  httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK)
  {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  free(buf);

  int reg = atoi(_reg);
  int mask = atoi(_mask);

  sensor_t *s = esp_camera_sensor_get();

  int res = s->get_reg(s, reg, mask);
  if (res < 0) return httpd_resp_send_500(req);

  log_i("Get Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, res);

  char buffer[20];
  const char *val = itoa(res, buffer, 10);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  return httpd_resp_send(req, val, strlen(val));
}

static int parse_get_var(char *buf, const char *key, int deflt)
{
  char _int[16];
  if (httpd_query_key_value(buf, key, _int, sizeof(_int)) != ESP_OK)
    return deflt;

  return atoi(_int);
}

static esp_err_t pll_handler(httpd_req_t *req)
{
  char *buf = NULL;
  if (parse_get(req, &buf) != ESP_OK)
    return ESP_FAIL;

  int bypass = parse_get_var(buf, "bypass", 0);
  int mul    = parse_get_var(buf, "mul", 0);
  int sys    = parse_get_var(buf, "sys", 0);
  int root   = parse_get_var(buf, "root", 0);
  int pre    = parse_get_var(buf, "pre", 0);
  int seld5  = parse_get_var(buf, "seld5", 0);
  int pclken = parse_get_var(buf, "pclken", 0);
  int pclk   = parse_get_var(buf, "pclk", 0);
  free(buf);

  log_i("Set Pll: bypass: %d, mul: %d, sys: %d, root: %d, pre: %d, seld5: %d, pclken: %d, pclk: %d", bypass, mul, sys, root, pre, seld5, pclken, pclk);
  
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_pll(s, bypass, mul, sys, root, pre, seld5, pclken, pclk);
  if (res) return httpd_resp_send_500(req);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  return httpd_resp_send(req, NULL, 0);
}

// GET /resolution
static esp_err_t win_handler(httpd_req_t *req)
{
  char *buf = NULL;
  if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;

	  int startX = parse_get_var(buf, "sx", 0);
	  int startY = parse_get_var(buf, "sy", 0);
	  int endX = parse_get_var(buf, "ex", 0);
	  int endY = parse_get_var(buf, "ey", 0);
	  int offsetX = parse_get_var(buf, "offx", 0);
	  int offsetY = parse_get_var(buf, "offy", 0);
	  int totalX = parse_get_var(buf, "tx", 0);
	  int totalY = parse_get_var(buf, "ty", 0);  // codespell:ignore totaly
	  int outputX = parse_get_var(buf, "ox", 0);
	  int outputY = parse_get_var(buf, "oy", 0);
	  bool scale = parse_get_var(buf, "scale", 0) == 1;
	  bool binning = parse_get_var(buf, "binning", 0) == 1;

  free(buf);

  log_i("Set Window: Start: %d %d, End: %d %d, Offset: %d %d, Total: %d %d, Output: %d %d, Scale: %u, Binning: %u", startX, startY, endX, endY, offsetX, offsetY,
         totalX, totalY, outputX, outputY, scale, binning );

  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_res_raw(s, startX, startY,
							endX, endY,
							offsetX, offsetY,
							totalX, totalY,
							outputX, outputY,
							scale, binning);  // codespell:ignore totaly
  if (res) return httpd_resp_send_500(req);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  return httpd_resp_send(req, NULL, 0);
}

// GET /
static esp_err_t index_handler(httpd_req_t *req)
{
  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL)
  {
	  log_e("Camera sensor not found");
	  return httpd_resp_send_500(req);
  }

  httpd_resp_set_type(req, "text/html");
  //httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

  if	  (s->id.PID == OV3660_PID)
      return httpd_resp_send(req, (const char *)index_ov3660_html_gz, index_ov3660_html_gz_len);
  else if (s->id.PID == OV5640_PID)
      return httpd_resp_send(req, (const char *)index_ov5640_html_gz, index_ov5640_html_gz_len);
  else
      return httpd_resp_send(req, (const char *)index_ov2640_html_gz, sizeof(index_ov2640_html_gz)-1);
}

void startControlAndStreamServers()
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 16;

  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
	#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
	#endif
  };

  httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_handler,
    .user_ctx = NULL
	#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
	#endif
  };

  httpd_uri_t control_uri = {
    .uri = "/control",
    .method = HTTP_GET,
    .handler = control_handler,
    .user_ctx = NULL
	#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
	#endif
  };

  httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL
	#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
	#endif
  };

  httpd_uri_t bmp_uri = {
    .uri = "/bmp",
    .method = HTTP_GET,
    .handler = bmp_handler,
    .user_ctx = NULL
	#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
	#endif
  };

  httpd_uri_t xclk_uri = {
    .uri = "/xclk",
    .method = HTTP_GET,
    .handler = xclk_handler,
    .user_ctx = NULL
	#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
	#endif
  };

  httpd_uri_t reg_uri = {
    .uri = "/reg",
    .method = HTTP_GET,
    .handler = reg_handler,
    .user_ctx = NULL
	#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
	#endif
  };

  httpd_uri_t greg_uri = {
    .uri = "/greg",
    .method = HTTP_GET,
    .handler = greg_handler,
    .user_ctx = NULL
	#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
	#endif
  };

  httpd_uri_t pll_uri = {
    .uri = "/pll",
    .method = HTTP_GET,
    .handler = pll_handler,
    .user_ctx = NULL
    #ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
    #endif
  };

	httpd_uri_t win_uri = {
		.uri = "/resolution",
		.method = HTTP_GET,
		.handler = win_handler,
		.user_ctx = NULL
		#ifdef CONFIG_HTTPD_WS_SUPPORT
		,
		.is_websocket = true,
		.handle_ws_control_frames = false,
		.supported_subprotocol = NULL
		#endif
	};

	httpd_uri_t stream2640_uri = {
		.uri = "/stream",
		.method = HTTP_GET,
		.handler = stream2640_handler,
		.user_ctx = NULL
		#ifdef CONFIG_HTTPD_WS_SUPPORT
		,
		.is_websocket = true,
		.handle_ws_control_frames = false,
		.supported_subprotocol = NULL
		#endif
	};

	httpd_uri_t stream90640_uri = {
		.uri = "/stream",
		.method = HTTP_GET,
		.handler = stream90640_handler,
		.user_ctx = NULL
		#ifdef CONFIG_HTTPD_WS_SUPPORT
		,
		.is_websocket = true,
		.handle_ws_control_frames = false,
		.supported_subprotocol = NULL
		#endif
	};


    log_i("Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&control_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(control_httpd, &index_uri);
        httpd_register_uri_handler(control_httpd, &control_uri);
        httpd_register_uri_handler(control_httpd, &status_uri);
        httpd_register_uri_handler(control_httpd, &capture_uri);
        httpd_register_uri_handler(control_httpd, &bmp_uri);
    
        httpd_register_uri_handler(control_httpd, &xclk_uri);
        httpd_register_uri_handler(control_httpd, &reg_uri);
        httpd_register_uri_handler(control_httpd, &greg_uri);
        httpd_register_uri_handler(control_httpd, &pll_uri);
        httpd_register_uri_handler(control_httpd, &win_uri);
    }
    
    config.server_port += 1;
    config.ctrl_port   += 1;
    
    log_i("Starting ov2640 stream server on port: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream2640_uri);
    }
    
    config.server_port += 1;
    config.ctrl_port   += 1;
    
    log_i("Starting mlx90640 stream server on port: '%d'", config.server_port);
    if (httpd_start(&mlxthc_httpd, &config) == ESP_OK)
    {
	    httpd_register_uri_handler(mlxthc_httpd, &stream90640_uri);
    }
}

void setupLedFlash()
{
	#if defined(LED_GPIO_NUM)
	    ledcAttach(LED_GPIO_NUM, 5000, 8);	// pin, freq, resolution 8bit
	#else
	    log_i("LED flash is disabled -> LED_GPIO_NUM undefined");
	#endif
}
