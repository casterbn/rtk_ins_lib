/** ***************************************************************************
 * @file   taskGnssDataAcq.h
 *
 * THIS CODE AND INFORMATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
 * KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
 * PARTICULAR PURPOSE.
 *
 * sensor data acquisition task runs at 100Hz, gets the data for each sensor
 * and applies available calibration
 ******************************************************************************/
/*******************************************************************************
Copyright 2018 ACEINNA, INC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*******************************************************************************
*/

#ifndef _TASK_DATA_ACQ_H_
#define _TASK_DATA_ACQ_H_


// #define fifo_user_uart_base                 ((fifo_type *) 0x280cf500)
// #define fifo_user_uart_base_buf             ((uint8_t *) 0x280cf600)
// #define fifo_gps_uart_rover                 ((fifo_type *) 0x280d1600)
// #define fifo_gps_uart_rover_buf             ((uint8_t *) 0x280d1700)

#define fifo_user_uart_base                 ((fifo_type *) 0x28021400)
#define fifo_user_uart_base_buf             ((uint8_t *) 0x28021500)

#define fifo_gps_uart_rover                 ((fifo_type *) 0x28023500)
#define fifo_gps_uart_rover_buf             ((uint8_t *) 0x28023600)

#define gnss_data_to_ins              ((gnss_solution_t *) 0x28021000)

void process_gnss_data(void);
#endif
