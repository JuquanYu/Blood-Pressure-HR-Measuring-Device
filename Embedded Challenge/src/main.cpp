/**********
__________________________________________________________________________________________
***********

Writer: Xuan Zhou, Juquan Yu
NetID: xz3264, jy3873

Discription of the project:
A semi-automated blood pressure/HR measuring device.

Usage:
1. Put on the cuff correctly around the arm, run the program to start the test.
2. Increase the pressure to above 150mmHg(max measurable pressure is 300mmHg).
   After pressure passes 150mmHg, the device will remind the user to release the valve.
3. Slowly release the valve, make sure the deflation rate is less than 4mmHg/sec.
   If the pressure decreases too fast, the screen will show a warning message in red color.
4. After pressure of the cuff decreases to below 30mmHg, the final result will be displayed.

Result interpretation:
While measuring, the first line on the screen shows the current pressure of the cuff.
After pressure passes 150mmHg, the reminder message "Release valve slowly" will be shown in line 2.
During the deflation phase, the warning message "Too fast" may show in line 3;
  otherwise, shows "Measuring..".
After measurement is finished, BPM, DBP, SBP will be printed on screen in turn.

************
__________________________________________________________________________________________
***********/


#include <mbed.h>
#include <cstdio>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "drivers/LCD_DISCO_F429ZI.h"

// declare lcd
LCD_DISCO_F429ZI lcd;

// the sensor's 7-bit address
#define SENSOR_ADDRESS 0b0011000

// I2C_SDA I2C_SCL
I2C Wire(PC_9,PA_8);

// the sensor's address for read transactions
static constexpr uint8_t read_addr = ((SENSOR_ADDRESS<<1u)|1U);

// the sensor's address for write transactions
static constexpr uint8_t write_addr = (SENSOR_ADDRESS<<1U); 

// a buffer used to store data received from the remote sensor over i2c
static uint8_t data_buf[20];

// 3 bytes to send (after sending the sensor address) to ask for the data
static const uint8_t com_window_start_buf[3] = {0xAA,0x00,0x00};

// output at Max/ Min pressure
const float max_output = 3774873.6;  //3774873.6  :  22.5 %
const float min_output = 419425;   //419430.4    :  2.5  %

// get the data from honeywell sensor and store the 24 bits data
uint32_t Get_Data(){
  uint32_t Data = 0;

  Wire.write(write_addr,(const char *)com_window_start_buf,3,true);
  Wire.read(read_addr,(char *)&data_buf[0],4,false);

  //Data |= ((uint8_t)data_buf[0]<<24);
  Data |= ((uint32_t)data_buf[1]<<16);
  Data |= ((uint32_t)data_buf[2]<<8);
  Data |= ((uint32_t)data_buf[3]);

  return Data;
}

// buffer to display result on screen
char pressure_buf[2];

// flag shows whether is in inflating phase
bool inflate_flag = true;

// record previous pressure
float pre_pressure = 0;

// raw data from sensor
uint32_t sensor_data;

// stores tranformed pressure
float pressure[1500];

// stores pressure flactuation between two sampling
float fluactuation[1500];

// min negative fluactuation 
float global_min = 100000;

// index of min negative fluactuation 
int argmin = 0;

// corresponding pressure of argmin
float arg_pressure = 0;

// stores dbp, sbp
float dp = 0;
float sp = 0;


int main() {
  Wire.frequency(400000);
  int i = 0;
  
  while (1) {
    // get current cuff pressure
    sensor_data = Get_Data();
    pressure[i] = ((float)sensor_data - min_output) * 300 / (max_output - min_output);

    //printf("%fmmHg\n",pressure[i]);

    // if pressure passes 150mmHg, remind user to deflate
    // when the pressure we get is over 180, the screen shows "Release valve slowly"
    if (pressure[i] > 150) {
      inflate_flag = false;
      lcd.DisplayStringAt(0, LINE(3), (uint8_t *)"Release valve slowly", CENTER_MODE);
    }

    // if is in deflate phase
    if (!inflate_flag) {
      // capture the min negative fluctuation
      // this is to get the max pulse, which is used to calculate the sbp and dbp and bpm
      if (pressure[i] < 170 && pre_pressure - pressure[i] < global_min) {
        global_min = pre_pressure - pressure[i];
        argmin = i;
        arg_pressure = pressure[i];
      }
      
      // reminder of user if deflate rate is too high
      // if we release too fast, the screen will show "Too fast" with red color
      if (25 * (pressure[i] - pre_pressure) > 4) {  // 25 * (pressure - pre_pressure) > 4
        lcd.SetTextColor(LCD_COLOR_RED);
        lcd.DisplayStringAt(0, LINE(5), (uint8_t *)"  Too fast  ", CENTER_MODE);
      } else { //when the speed is normal, the screen shows "Measuring.."
        lcd.DisplayStringAt(0, LINE(5), (uint8_t *)"  Measuring..  ", CENTER_MODE);
      }

      lcd.SetTextColor(LCD_COLOR_BLACK);

      // when pressure decline to 30mmHg, we stop measuring and try to get test result
      if (pressure[i] < 30) {
        // the position of dbp is 0.75 of max pulse after it, and the sbp is 0.45 of max pulse before it
        float dp_flc = global_min * 0.75;
        float sp_flc = global_min * 0.45;
        printf("Max Pulse: \n");
        printf("%3.2fmmHg   %3.2fmmHg\n", global_min, arg_pressure);

        // calculate and display test result
        lcd.DisplayStringAt(0, LINE(8), (uint8_t *)"     Final Result:     ", CENTER_MODE);

        // get BPM
        // we choose the steady part of the data to calculate the bpm
        // when there is a heart beat, the data we get at that moment should 
        // be bigger than the data before it a little bit, so the fluctuation will
        // be negative, we calculate the bpm according to how many times have the
        // fluctuation turn negative and then ture positive during the specific time period
        int m = argmin;
        int T_number = 0;
        int start_index = -1;
        int end_index = -1;

        while (T_number < 10) {
          if (fluactuation[m] < 0) {
            if (start_index == -1) {
              start_index = m;
            }

            T_number++;

            while (fluactuation[m] < 0) {
              m++;
            }
          }
          m++;
        }

        end_index = m;

        float time = (end_index - start_index) * 0.04;
        int bpm = (10 / time) * 60 * 0.9; // beats/min

        // here we let the output shown in the screen
        printf("BPM = %d /min\n", bpm);
        sprintf(pressure_buf, "  BPM = %d/min  ", bpm); 
        lcd.SetTextColor(LCD_COLOR_BLUE);
        lcd.DisplayStringAt(0, LINE(9), (uint8_t *)pressure_buf, CENTER_MODE);

        // get dp pressure
        // as we know, human's normal dbp is between 60-80, so we capture the data
        // in between the specific part of the data to ignore some other ridiculus data
        // here is how we get the data

        // |              MAX Pulse(P)->
        // |                            |\        <-DBP(0.8*P)
        // |                            | \      |\
        // |                            |  \     | \
        // |  (0.5*P)SBP->    |\        |   \    |  \
        // |             |\   | \       |    \   |   \
        // |         |\  | \  |  \      |     \  |    \
        // ||\ |\ |\ | \ |  \ |   \     |      \ |     \ |\
        // || \| \| \|  \|   \|    \ ...|       \|      \| \.....
        // |__________________________________________________________

        int j;
        for (j = argmin; pressure[j] > 50; j++) {
          if (fluactuation[j] < 0 && fluactuation[j] > dp_flc * 1.2 &&
              fluactuation[j] < dp_flc * 0.8 && pressure[j]>55 && pressure[j]<85) {
            dp = pressure[j];

            sprintf(pressure_buf, "  DBP = %3.2fmmHg  ", dp); 
            lcd.SetTextColor(LCD_COLOR_BLUE);
            lcd.DisplayStringAt(0, LINE(10), (uint8_t *)pressure_buf, CENTER_MODE);

            break; 
          }
        }

        // if the data captured cannot get proper result, it will show "Test Fail!"
        if (pressure[j] <= 50) {
            lcd.DisplayStringAt(0, LINE(10), (uint8_t *)"  Test Fail!  ", CENTER_MODE);
            break;
        }

        // get sp pressure, the same as dp part
        for (j = argmin; pressure[j] < 170; j--) {
          if (fluactuation[j] < 0 && fluactuation[j] > sp_flc * 1.2 &&
              fluactuation[j] < sp_flc * 0.8 && pressure[j]> 85 && pressure[j]<125) {
            sp = pressure[j];

            sprintf(pressure_buf, "  SBP = %3.2fmmHg  ", sp); 
            lcd.SetTextColor(LCD_COLOR_BLUE);
            lcd.DisplayStringAt(0, LINE(11), (uint8_t *)pressure_buf, CENTER_MODE);

            break;  
          }
        }

        if (pressure[j] >= 170) {
            lcd.DisplayStringAt(0, LINE(11), (uint8_t *)"  Test Fail!  ", CENTER_MODE);
            break;
        }
 
        printf("Test Finished.\n");
        break;
      }
    }

    // display the final result on the screen
    sprintf(pressure_buf, "  Pressure= %3.2fmmHg  ", pressure[i]); 
    lcd.DisplayStringAt(0, LINE(1), (uint8_t *)pressure_buf, CENTER_MODE);

    printf("%3.2fmmHg    %3.2fmmHg\n", pre_pressure - pressure[i], pressure[i]);

    fluactuation[i] = pre_pressure - pressure[i];
    pre_pressure = pressure[i];

    if (i++ > 1500) {
      lcd.DisplayStringAt(0, LINE(11), (uint8_t *)"  Test Fail!  ", CENTER_MODE);
      break;
    }

    thread_sleep_for(30);
    //printf("%fmmHg\n",pressure[i] - pre_pressure);
  }
}
