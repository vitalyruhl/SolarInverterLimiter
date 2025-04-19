#include "helpers.h"


/** info
 * @param x float -> the value to be converted
 * @param in_min the minimum value of the input range
 * @param in_max the maximum value of the input range
 * @param out_min the minimum value of the output range
 * @param out_max the maximum value of the output range
 * @return the converted value
 * @brief mapFloat() converts a float value from one range to another.
 * @example: mapFloat(512, 0, 1023, 0, 100) = 50.0 // Input 0-1023 to output 0-100
 */
float Helpers::mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


/** info
 * @param BlinkCount int -> number of blinks
 * @param blinkRate int -> blink rate in ms
 * @brief blinkBuidInLED() blinks the built-in LED a specified number of times at a specified rate.
 * @example: blinkBuidInLED(3, 1000) // Blink 3 times with a 1000ms delay
 */
void  Helpers::blinkBuidInLED(int BlinkCount, int blinkRate)
{
  // BlinkCount = 3; // number of blinks
  // blinkRate = 1000; // blink rate in ms

  for (int i = 0; i < BlinkCount; i++)
  {
    digitalWrite(LED_BUILTIN, HIGH); // turn the LED on (HIGH is the voltage level)
    delay(blinkRate);                // wait for a second
    digitalWrite(LED_BUILTIN, LOW);  // turn the LED off (LOW is the voltage level)
    delay(blinkRate);                // wait for a second
  }
}

/** info
 * @brief blinkBuidInLEDsetpinMode() initializes the built-in LED pin mode as an output.
 * @example: blinkBuidInLEDsetpinMode() // Initialize the built-in LED pin mode
 */
void  Helpers::blinkBuidInLEDsetpinMode() {
  pinMode(LED_BUILTIN, OUTPUT); // initialize GPIO pin 2 LED_BUILTIN as an output.
}
