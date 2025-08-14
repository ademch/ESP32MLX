/**
   @copyright (C) 2017 Melexis N.V.
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
       http://www.apache.org/licenses/LICENSE-2.0
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <Arduino.h>				// Serial declaration
#include <Wire.h>
#include "MLX90640_I2C_Driver.h"

// Read a number of words from startAddress into Data array.
// Returns 0 if successful, -1 if error
int MLX90640_I2CRead(uint8_t _deviceAddress, unsigned int startAddress, unsigned int nWordsToRead, uint16_t *data)
{
	// Caller passes nWordsToRead convert to 'bytes to read'
	uint16_t bytesRemaining = nWordsToRead * 2;

	// It doesn't look like sequential read works. Do we need to re-issue the address command each time?

	uint16_t dataWordInd = 0; // Start at beginning of word array

	// Setup a series of chunked I2C_BUFFER_LENGTH byte reads
	while (bytesRemaining > 0)
	{
		// communication is done in words by the sensor
		Wire.beginTransmission(_deviceAddress);
			Wire.write(startAddress >> 8);			// MSB
			Wire.write(startAddress & 0xFF);		// LSB

		if (Wire.endTransmission(false) != 0)		// Do not release bus
		{
			Serial.println("No ACK read");
			return -1;								// Sensor did not ACK
		}

		uint16_t nBytesToRead = bytesRemaining;
		if (nBytesToRead > I2C_BUFFER_LENGTH)
			nBytesToRead = I2C_BUFFER_LENGTH;

		Wire.requestFrom(_deviceAddress, nBytesToRead);
		if (Wire.available())
		{
			// Store data into array
			for (uint16_t x=0; x < nBytesToRead/2; x++)
			{
				data[dataWordInd]  = Wire.read() << 8;   // MSB
				data[dataWordInd] |= Wire.read();		 // LSB

				dataWordInd++;
			}
		}

		bytesRemaining -= nBytesToRead;

		startAddress += nBytesToRead / 2;
	}

	return 0; // Success
}

//Write two bytes to a two byte address
int MLX90640_I2CWrite(uint8_t _deviceAddress, unsigned int writeAddress, uint16_t data)
{
	Wire.beginTransmission(_deviceAddress);

		Wire.write(writeAddress >> 8);   // MSB
		Wire.write(writeAddress & 0xFF); // LSB
		Wire.write(data >> 8);			 // MSB
		Wire.write(data & 0xFF);		 // LSB
	
	if (Wire.endTransmission() != 0)
	{
		// Sensor did not ACK
		Serial.println("Error: Sensor did not ack");
		return -1;
	}

	uint16_t dataCheck;
	MLX90640_I2CRead(_deviceAddress, writeAddress, 1, &dataCheck);
	if (dataCheck != data)
	{
		//Serial.println("The write request didn't stick");
		return -2;
	}

	// Success
	return 0; 
}

