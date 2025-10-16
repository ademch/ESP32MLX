#include "esp_camera.h"
#include <WiFi.h>

#include <Wire.h>
#include "MLX90640_API.h"
#include "MLX90640_I2C_Driver.h"
#include "SPIFFS.h"
#include "MLX90640_calibration.h"

const char* strBuildTimestamp = __TIMESTAMP__;

// Select camera model in board_config.h
#include "board_config.h"

// Enter your WiFi credentials
const char *ssid        = "mzorova";
const char *password    =
#include "password.d"
;

const char *ap_ssid     = "ESP32CAM_AP";
const char *ap_password = "12345678";

void startControlAndStreamServers();
void setupLedFlash();

// Callback when station connects
void onClientConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
	Serial.println(">> New client connected!");
	Serial.print("MAC: ");
	for (int i = 0; i < 6; i++) {
		Serial.printf("%02X", info.wifi_ap_staconnected.mac[i]);
		if (i < 5) Serial.print(":");
	}
	Serial.println();
}

// Callback when station disconnects
void onClientDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
	Serial.println(">> Client disconnected!");
	Serial.print("MAC: ");
	for (int i = 0; i < 6; i++) {
		Serial.printf("%02X", info.wifi_ap_stadisconnected.mac[i]);
		if (i < 5) Serial.print(":");
	}
	Serial.println();
}


void setup() {
  
	delay(2000);

	// Start UART
	Serial.begin(115200);
  
	// Enable diagnostic output for WiFi libraries
	Serial.setDebugOutput(true);

	Serial.print("OV2640 camera init...");

		camera_config_t config;
  
		config.ledc_channel   = LEDC_CHANNEL_0;
		config.ledc_timer     = LEDC_TIMER_0;
  							  
		config.pin_d0		  = Y2_GPIO_NUM;
		config.pin_d1		  = Y3_GPIO_NUM;
		config.pin_d2		  = Y4_GPIO_NUM;
		config.pin_d3		  = Y5_GPIO_NUM;
		config.pin_d4		  = Y6_GPIO_NUM;
		config.pin_d5		  = Y7_GPIO_NUM;
		config.pin_d6		  = Y8_GPIO_NUM;
		config.pin_d7		  = Y9_GPIO_NUM;
		config.pin_xclk		  = XCLK_GPIO_NUM;
		config.pin_pclk		  = PCLK_GPIO_NUM;
		config.pin_vsync	  = VSYNC_GPIO_NUM;
		config.pin_href		  = HREF_GPIO_NUM;
		config.pin_sccb_sda	  = SIOD_GPIO_NUM;
		config.pin_sccb_scl	  = SIOC_GPIO_NUM;
		config.pin_pwdn		  = PWDN_GPIO_NUM;
		config.pin_reset	  = RESET_GPIO_NUM;
  
		config.xclk_freq_hz   = 20000000;
		config.frame_size     = FRAMESIZE_UXGA;			// Specify Max resolution to allocate continuous block
		// PIXFORMAT_RGB565
		config.pixel_format   = PIXFORMAT_JPEG;

		// CAMERA_GRAB_LATEST, CAMERA_GRAB_WHEN_EMPTY
		config.grab_mode      = CAMERA_GRAB_WHEN_EMPTY;	// refreshes framebuffer only when there is a slot in fb_count of buffers

		// CAMERA_FB_IN_DRAM, CAMERA_FB_IN_PSRAM
		config.fb_location    = CAMERA_FB_IN_PSRAM;		// 4 MB connected with 80MHz SPI
		config.jpeg_quality   = 14;						// 0-63 (lower means higher quality)
		config.fb_count	      = 2;

		// camera init
		esp_err_t err = esp_camera_init(&config);
		if (err != ESP_OK) {
			Serial.printf("Camera init failed with error 0x%x", err);
			return;
		}

		// drop down frame size for higher initial frame rate
		sensor_t *s = esp_camera_sensor_get();
		//s->set_framesize(s, FRAMESIZE_VGA);
		s->set_hmirror(s, 1);

		// crop to match thermal camera
		s->set_res_raw(s,
					   0, 0,			// startX encodes mode
					   0, 0,			// ignored
					   150, 90,			// start column of a window described by mode (see ratio table in ov2640_ratio.h)
					   1350, 990,		// end column of a window described by mode (see ratio table in ov2640_ratio.h)
					   1200, 900,		// final resolution asked
					   false, false);	// ignored

		// Setup LED FLash if LED pin is defined in camera_pins.h
		ledcAttach(LED_GPIO_NUM, 5000, 8);	// pin, freq, resolution 8bit

		// wait the cam to reconfigure resolution, otherwise cam_task runs out of stack
		delay(2000);

		s->set_brightness(s, 0);   // sensor init does not correspond to zero

	Serial.println("success");

	Serial.print("MLX90640 thermal camera init...");

		// Initialize I2C with custom pins and frequency (default 100kHz)
		Wire.begin(I2C_SDA_GPIO_NUM, I2C_SCL_GPIO_NUM, 400000);	// SDA, SCL, frequency in Hz

		const uint8_t MLX90640_address = 0x33;  // Default 7-bit unshifted address of the MLX90640

		MLX90640_Init(MLX90640_address);
		
		MLX90640_SetRefreshRate(MLX90640_REFRESH_RATE_4HZ);

	Serial.println("success");


	if (true)
	{
		WiFi.softAP(ap_ssid, ap_password);
		Serial.print("WiFi AP started: ");
		Serial.println(ap_ssid);
		Serial.print("IP address: ");
		Serial.println(WiFi.softAPIP());

		// Register callback for AP client connection/disconnection
		WiFi.onEvent(onClientConnected,    ARDUINO_EVENT_WIFI_AP_STACONNECTED);
		WiFi.onEvent(onClientDisconnected, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
	}
	else // Station mode (connect to a router)
	{
		WiFi.begin(ssid, password);
		WiFi.setSleep(false);
				
		Serial.print("Connecting to WiFi access point ");
		Serial.print(ssid);
		while (WiFi.status() != WL_CONNECTED)
		{
			delay(250);			// wait for connection
			Serial.print(".");
		}
		Serial.println("success");
	}

	Serial.print("Mounting SPIFFS...");

	// Mount SPIFFS
	if (!SPIFFS.begin(true)) { // true = format if failed
		Serial.println("Failed to mount SPIFFS");
		return;
	}

	Serial.println("SPIFFS mounted successfully");

	// Get total and used bytes
	Serial.printf("* SPIFFS partition: %u kbytes\n", SPIFFS.totalBytes() >> 10);
	Serial.printf("* SPIFFS used: %u kbytes\n", SPIFFS.usedBytes() >> 10);


	Serial.println("Reading user calibration data from SPIFFS...");

		read_user_mlx_calibration_offsets();

	Serial.println("success");

  Serial.println("Launching http servers...");

	startControlAndStreamServers();

  Serial.println("success");

  Serial.print("All ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

void loop() {
  // Do nothing. Everything is done in another task by the web server
  //Serial.println(".");
  delay(1000);
}
