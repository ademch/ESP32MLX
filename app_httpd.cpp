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
#include "esp_camera.h"
#include "sdkconfig.h"
#include "board_config.h"
#include "httpd_firmware.h"
#include "httpd_capture_stream.h"
#include "httpd_mlx.h"
#include "Arduino.h"


#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
	#include "esp32-hal-log.h"
#endif

extern bool isStreaming;

// LED FLASH setup

extern int led_duty;
extern void enable_LED(bool en);

httpd_handle_t stream_httpd  = NULL;
httpd_handle_t control_httpd = NULL;
httpd_handle_t mlxthc_httpd  = NULL;


// GET can have query string comming after ?, eg GET /search?query=esp32&lang=en
esp_err_t parse_get(httpd_req_t *req, char **obuf)
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
	  // `${baseHost}/control?var=${el.id}&val=${value}`;
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
  else if (!strcmp(variable, "led_intensity"))
  {
      led_duty = val;
      if (isStreaming) enable_LED(true);
  }
  else {
      log_i("Unknown command: %s", variable);
      res = -1;
  }

  if (res < 0)
	  return httpd_resp_send_500(req);

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
    p += sprintf(p, ",\"led_intensity\":%u", led_duty);
    
    *p++ = '}';
    *p++ = 0;		// end of the string
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    return httpd_resp_send(req, json_response, strlen(json_response));
}

// GET /xclk
static esp_err_t xclk_handler(httpd_req_t *req)
{
    char *buf = NULL;
    if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;

		// httpd_query_key_value is a helper function to obtain a URL query tag from
		// a query string of the format param1=val1&param2=val2
		char _xclk[16];
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

	  // httpd_query_key_value is a helper function to obtain a URL query tag from
	  // a query string of the format param1=val1&param2=val2
	  if (httpd_query_key_value(buf, "reg",  _reg,  sizeof(_reg))  != ESP_OK ||
		  httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK ||
		  httpd_query_key_value(buf, "val",  _val,  sizeof(_val))  != ESP_OK)
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
		int offsetX = parse_get_var(buf, "offx", 0);
		int offsetY = parse_get_var(buf, "offy", 0);
		int totalX = parse_get_var(buf, "tx", 0);
		int totalY = parse_get_var(buf, "ty", 0);
		int outputX = parse_get_var(buf, "ox", 0);
		int outputY = parse_get_var(buf, "oy", 0);

    free(buf);

    log_i("Set Window: Resolution: %d, Start: %d %d, End: %d %d, Output: %d %d",
	      startX, offsetX, offsetY, totalX, totalY, outputX, outputY);

    sensor_t *s = esp_camera_sensor_get();
    int res = s->set_res_raw(s,
	  					     startX, 0,			// startX encodes mode
						     0, 0,				// ignored
						     offsetX, offsetY,	// start column of a window described by mode (see ratio table in ov2640_ratio.h)
						     totalX, totalY,	// end column of a window described by mode (see ratio table in ov2640_ratio.h)
						     outputX, outputY,	// final resolution asked
						     false, false);		// ignored
 
    // internally calls
	// set_window(ov2640_sensor_mode_t)startX, offsetX, offsetY, totalX, totalY, outputX, outputY);

    if (res) return httpd_resp_send_500(req);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    return httpd_resp_send(req, NULL, 0);
}


esp_err_t reboot_handler(httpd_req_t *req)
{
	log_i("Reboot request received");

	httpd_resp_sendstr(req, "Rebooting...");

	ESP.restart();

	return ESP_OK;
}

//=============================================================================

void startControlAndStreamServers()
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.max_uri_handlers = 24;

	httpd_uri_t ctrl_index_uri = {
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

	httpd_uri_t ctrl_status_uri = {
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

	httpd_uri_t ctrl_control_uri = {
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

	httpd_uri_t capture2640_uri = {
		.uri = "/capture2640",
		.method = HTTP_GET,
		.handler = ov2640_capture_handler,
		.user_ctx = NULL
		#ifdef CONFIG_HTTPD_WS_SUPPORT
		,
		.is_websocket = true,
		.handle_ws_control_frames = false,
		.supported_subprotocol = NULL
		#endif
	};

	httpd_uri_t capture90640_uri = {
		.uri = "/capture90640",
		.method = HTTP_GET,
		.handler = mlx90640_capture_handler,
		.user_ctx = NULL
		#ifdef CONFIG_HTTPD_WS_SUPPORT
		,
		.is_websocket = true,
		.handle_ws_control_frames = false,
		.supported_subprotocol = NULL
		#endif
	};

	httpd_uri_t ctrl_bmp_uri = {
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

	httpd_uri_t ctrl_xclk_uri = {
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

	httpd_uri_t ctrl_reg_uri = {
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

	httpd_uri_t ctrl_greg_uri = {
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

	httpd_uri_t ctrl_pll_uri = {
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

	httpd_uri_t ctrl_win_uri = {
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

	httpd_uri_t ctrl_mlx_uri = {
		.uri = "/mlx",
		.method = HTTP_GET,
		.handler = mlx_handler,
		.user_ctx = NULL
		#ifdef CONFIG_HTTPD_WS_SUPPORT
		,
		.is_websocket = true,
		.handle_ws_control_frames = false,
		.supported_subprotocol = NULL
		#endif
	};

	httpd_uri_t ctrl_uploadserver_uri = {
		.uri = "/uploadserver",
		.method = HTTP_POST,
		.handler = uploadserver_handler,
		.user_ctx = NULL
		#ifdef CONFIG_HTTPD_WS_SUPPORT
		,
		.is_websocket = true,
		.handle_ws_control_frames = false,
		.supported_subprotocol = NULL
		#endif
	};

	httpd_uri_t ctrl_reboot_uri = {
		.uri = "/reboot",
		.method = HTTP_GET,
		.handler = reboot_handler,
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
        httpd_register_uri_handler(control_httpd, &ctrl_index_uri);
        httpd_register_uri_handler(control_httpd, &ctrl_control_uri);
        httpd_register_uri_handler(control_httpd, &ctrl_status_uri);
        httpd_register_uri_handler(control_httpd, &capture2640_uri);
        httpd_register_uri_handler(control_httpd, &ctrl_bmp_uri);
    
        httpd_register_uri_handler(control_httpd, &ctrl_xclk_uri);
        httpd_register_uri_handler(control_httpd, &ctrl_reg_uri);
        httpd_register_uri_handler(control_httpd, &ctrl_greg_uri);
        httpd_register_uri_handler(control_httpd, &ctrl_pll_uri);
        httpd_register_uri_handler(control_httpd, &ctrl_win_uri);

		httpd_register_uri_handler(control_httpd, &ctrl_mlx_uri);

		httpd_register_uri_handler(control_httpd, &ctrl_uploadserver_uri);
		httpd_register_uri_handler(control_httpd, &ctrl_reboot_uri);

		httpd_register_uri_handler(control_httpd, &capture90640_uri);
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
