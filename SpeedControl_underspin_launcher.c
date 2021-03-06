#pragma config(I2C_Usage, I2C1, i2cSensors)
#pragma config(Sensor, dgtl1,  CanopySensorLimit, sensorTouch)
#pragma config(Sensor, dgtl3,  LCDin,          sensorDigitalIn)
#pragma config(Sensor, dgtl4,  LCDout,         sensorDigitalOut)
#pragma config(Sensor, I2C_1,  FlywheelSpeedIME, sensorQuadEncoderOnI2CPort,    , AutoAssign)
#pragma config(Sensor, I2C_2,  CanopyAngleSensor, sensorQuadEncoderOnI2CPort,    , AutoAssign)
#pragma config(Motor,  port2,           Canopy_servo,  tmotorServoStandard, openLoop)
#pragma config(Motor,  port4,           Motor_FW1,     tmotorVex393TurboSpeed_MC29, openLoop, encoderPort, I2C_1)
#pragma config(Motor,  port5,           Motor_FW2,     tmotorVex393TurboSpeed_MC29, openLoop, reversed)
#pragma config(Motor,  port6,           Motor_FW3,     tmotorVex393TurboSpeed_MC29, openLoop, reversed)
#pragma config(Motor,  port7,           Motor_FW4,     tmotorVex393TurboSpeed_MC29, openLoop)
//*!!Code automatically generated by 'ROBOTC' configuration wizard               !!*//


#define FLYWHEEL_SPEED_DELTA_STEP 50
#define FLYWHEEL_MAX_SPEED 2900
#define FLYWHEEL_MIN_SPEED 1000
#define FLYWHEEL_INIT_SPEED 2200

#define BUTTON_DEBOUNCE_TIME 200 // milliseconds

#define CANOPY_SERVO_MAX 127
#define CANOPY_SERVO_MIN -127

int deadzone = 0;
int canopy_joy_scale = -25;
float canopy_servo_value = 0;

/*-----------------------------------------------------------------------------*/
/*                                                                             */
/*                        Copyright (c) James Pearman                          */
/*                                   2015                                      */
/*                            All Rights Reserved                              */
/*                                                                             */
/*-----------------------------------------------------------------------------*/
/*                                                                             */
/*    Module:     flywheel.c                                                   */
/*    Author:     James Pearman                                                */
/*    Created:    28 June 2015                                                 */
/*                                                                             */
/*    Revisions:                                                               */
/*                V1.00  28 June 2015 - Initial release                        */
/*                                                                             */
/*-----------------------------------------------------------------------------*/
/*                                                                             */
/*    The author is supplying this software for use with the VEX cortex        */
/*    control system. This file can be freely distributed and teams are        */
/*    authorized to freely use this program , however, it is requested that    */
/*    improvements or additions be shared with the Vex community via the vex   */
/*    forum.  Please acknowledge the work of the authors when appropriate.     */
/*    Thanks.                                                                  */
/*                                                                             */
/*    Licensed under the Apache License, Version 2.0 (the "License");          */
/*    you may not use this file except in compliance with the License.         */
/*    You may obtain a copy of the License at                                  */
/*                                                                             */
/*      http://www.apache.org/licenses/LICENSE-2.0                             */
/*                                                                             */
/*    Unless required by applicable law or agreed to in writing, software      */
/*    distributed under the License is distributed on an "AS IS" BASIS,        */
/*    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. */
/*    See the License for the specific language governing permissions and      */
/*    limitations under the License.                                           */
/*                                                                             */
/*    The author can be contacted on the vex forums as jpearman                */
/*    or electronic mail using jbpearman_at_mac_dot_com                        */
/*    Mentor for team 8888 RoboLancers, Pasadena CA.                           */
/*                                                                             */
/*-----------------------------------------------------------------------------*/
/*                                                                             */
/*    An example of flywheel/shooter velocity control using the TBH algorithm  */
/*    Test system uses three motors with 25:2 gearing to the flywheel.         */
/*                                                                             */
/*-----------------------------------------------------------------------------*/

// Update inteval (in mS) for the flywheel control loop
#define FW_LOOP_SPEED              25

// Maximum power we want to send to the flywheel motors
#define FW_MAX_POWER              127

// encoder counts per revolution depending on motor
#define MOTOR_TPR_269           240.448
#define MOTOR_TPR_393R          261.333
#define MOTOR_TPR_393S          392
#define MOTOR_TPR_393T          627.2
#define MOTOR_TPR_QUAD          261.333

// Structure to gather all the flywheel ralated data
typedef struct _fw_controller {
	long            counter;                ///< loop counter used for debug

	// encoder tick per revolution
	float           ticks_per_rev;          ///< encoder ticks per revolution

	// Encoder
	long            e_current;              ///< current encoder count
	long            e_last;                 ///< current encoder count

	// velocity measurement
	float           v_current;              ///< current velocity in rpm
	long            v_time;                 ///< Time of last velocity calculation

	// TBH control algorithm variables
	long            target;                 ///< target velocity
	long            current;                ///< current velocity
	long            last;                   ///< last velocity
	float           error;                  ///< error between actual and target velocities
	float           last_error;             ///< error last time update called
	float           gain;                   ///< gain
	float           drive;                  ///< final drive out of TBH (0.0 to 1.0)
	float           drive_at_zero;          ///< drive at last zero crossing
	long            first_cross;            ///< flag indicating first zero crossing
	float           drive_approx;           ///< estimated open loop drive

	// final motor drive
	long            motor_drive;            ///< final motor control value
} fw_controller;

// Make the controller global for easy debugging
static  fw_controller   flywheel;

/*-----------------------------------------------------------------------------*/
/** @brief      Set the flywheen motors                                        */
/** @param[in]  value motor control value                                      */
/*-----------------------------------------------------------------------------*/
void
FwMotorSet( int value )
{
	motor[ Motor_FW1 ] = value;
	motor[ Motor_FW2 ] = value;
	motor[ Motor_FW3 ] = value;
	motor[ Motor_FW4 ] = value;
}

/*-----------------------------------------------------------------------------*/
/** @brief      Get the flywheen motor encoder count                           */
/*-----------------------------------------------------------------------------*/
long
FwMotorEncoderGet()
{
	return( nMotorEncoder[ Motor_FW1 ] );


}

/*-----------------------------------------------------------------------------*/
/** @brief      Set the controller position                                    */
/** @param[in]  fw pointer to flywheel controller structure                    */
/** @param[in]  desired velocity                                               */
/** @param[in]  predicted_drive estimated open loop motor drive                */
/*-----------------------------------------------------------------------------*/
void
FwVelocitySet( fw_controller *fw, int velocity, float predicted_drive )
{
	// set target velocity (motor rpm)
	fw->target        = velocity;

	// Set error so zero crossing is correctly detected
	fw->error         = fw->target - fw->current;
	fw->last_error    = fw->error;

	// Set predicted open loop drive value
	fw->drive_approx  = predicted_drive;
	// Set flag to detect first zero crossing
	fw->first_cross   = 1;
	// clear tbh variable
	fw->drive_at_zero = 0;
}

/*-----------------------------------------------------------------------------*/
/** @brief      Calculate the current flywheel motor velocity                  */
/** @param[in]  fw pointer to flywheel controller structure                    */
/*-----------------------------------------------------------------------------*/
void
FwCalculateSpeed( fw_controller *fw )
{
	int     delta_ms;
	int     delta_enc;

	// Added by SK to smooth velocity calculations
	float alpha = 0.2;

	// Get current encoder value
	fw->e_current = FwMotorEncoderGet();

	// This is just used so we don't need to know how often we are called
	// how many mS since we were last here
	delta_ms   = nSysTime - fw->v_time;
	fw->v_time = nSysTime;

	// Change in encoder count
	delta_enc = (fw->e_current - fw->e_last);

	// save last position
	fw->e_last = fw->e_current;

	// Calculate velocity in rpm
	fw->v_current = fw->v_current * (1-alpha) + (1000.0 / delta_ms) * delta_enc * 60.0 / fw->ticks_per_rev * 11.66666 * alpha; //SK HACK




	//writeDebugStreamLine("Current Speed is %f", fw->v_current);
}

/*-----------------------------------------------------------------------------*/
/** @brief      Update the velocity tbh controller variables                   */
/** @param[in]  fw pointer to flywheel controller structure                    */
/*-----------------------------------------------------------------------------*/
void
FwControlUpdateVelocityTbh( fw_controller *fw )
{
	// calculate error in velocity
	// target is desired velocity
	// current is measured velocity
	fw->error = fw->target - fw->current;

	// Use Kp as gain
	fw->drive =  fw->drive + (fw->error * fw->gain);

	// Clip - we are only going forwards
	if( fw->drive > 1 )
		fw->drive = 1;
	if( fw->drive < 0 )
		fw->drive = 0;

	// Check for zero crossing
	if( sgn(fw->error) != sgn(fw->last_error) ) {
		// First zero crossing after a new set velocity command
		if( fw->first_cross ) {
			// Set drive to the open loop approximation
			fw->drive = fw->drive_approx;
			fw->first_cross = 0;
		}
		else
			fw->drive = 0.5 * ( fw->drive + fw->drive_at_zero );

		// Save this drive value in the "tbh" variable
		fw->drive_at_zero = fw->drive;
	}

	// Save last error
	fw->last_error = fw->error;
}

/*-----------------------------------------------------------------------------*/
/** @brief     Task to control the velocity of the flywheel                    */
/*-----------------------------------------------------------------------------*/
task
FwControlTask()
{
	fw_controller *fw = &flywheel;

	// Set the gain
	fw->gain = 0.00025;

	// We are using Speed geared motors
	// Set the encoder ticks per revolution
	fw->ticks_per_rev = MOTOR_TPR_393R;

	while(1)
	{
		// debug counter
		fw->counter++;

		// Calculate velocity
		FwCalculateSpeed( fw );

		// Set current speed for the tbh calculation code
		fw->current = fw->v_current;

		// Do the velocity TBH calculations
		FwControlUpdateVelocityTbh( fw ) ;

		// Scale drive into the range the motors need
		fw->motor_drive  = (fw->drive * FW_MAX_POWER) + 0.5;

		// Final Limit of motor values - don't really need this
		if( fw->motor_drive >  127 ) fw->motor_drive =  127;
		if( fw->motor_drive < -127 ) fw->motor_drive = -127;

		// and finally set the motor control value
		FwMotorSet( fw->motor_drive );

		// Run at somewhere between 20 and 50mS
		wait1Msec( FW_LOOP_SPEED );
	}
}


// Main user task
task main()
{
	char  str[32];
	float start_speed = FLYWHEEL_INIT_SPEED;
	float start_drive = 0.47;

	bLCDBacklight = true;

	// Start the flywheel control task
	startTask( FwControlTask );

	// Set initial value of Canopy Servo
	motor[Canopy_servo] = 0;

	// Main user control loop
	while(1)
	{
		// Different speeds set by buttons
		if( vexRT[ Btn6U ] == 1 ) {
			wait1Msec(BUTTON_DEBOUNCE_TIME);

			FwVelocitySet( &flywheel, FLYWHEEL_INIT_SPEED, start_drive );
			//writeDebugStreamLine("started Flywheel");
		}
		else if( vexRT[ Btn8U ] == 1 )
		{
			if( flywheel.target + FLYWHEEL_SPEED_DELTA_STEP <= FLYWHEEL_MAX_SPEED)
			{
				wait1Msec(BUTTON_DEBOUNCE_TIME);
				FwVelocitySet( &flywheel, flywheel.target+FLYWHEEL_SPEED_DELTA_STEP, flywheel.drive+0.02 );
			}
		}
		else if( vexRT[ Btn8D ] == 1 )
		{
			if( flywheel.target - FLYWHEEL_SPEED_DELTA_STEP >= FLYWHEEL_MIN_SPEED)
			{
				wait1Msec(BUTTON_DEBOUNCE_TIME);
				FwVelocitySet( &flywheel, flywheel.target-FLYWHEEL_SPEED_DELTA_STEP, flywheel.drive-0.02 );
			}

		}
		else if(vexRT[ Btn6D ] == 1)
		{
			wait1Msec(BUTTON_DEBOUNCE_TIME);
			start_speed = flywheel.target;
			start_drive = flywheel.drive;
			FwVelocitySet( &flywheel, 0.0, 0.0 );

		}
		else if(vexRT[ Btn8L ] == 1)
		{
			wait1Msec(BUTTON_DEBOUNCE_TIME);
			writeDebugStreamLine("Current Flywheel target %f", flywheel.target);
			writeDebugStreamLine("Current Flywheel actual %f", flywheel.v_current);

			writeDebugStreamLine("Canopy Servo Value %f",canopy_servo_value);
		}

		// Canopy Controls

		if (abs(vexRT[ Ch3]) > deadzone) {
			canopy_servo_value += vexRT[ Ch3]/canopy_joy_scale;
			if (canopy_servo_value > CANOPY_SERVO_MAX)
		    canopy_servo_value = CANOPY_SERVO_MAX;
		  if (canopy_servo_value < CANOPY_SERVO_MIN)
		    canopy_servo_value = CANOPY_SERVO_MIN;
		  motor[Canopy_servo] = canopy_servo_value;
	  }


		// Display useful things on the LCD
		sprintf( str, "%4d %4d  %5.2f", flywheel.target,  flywheel.current, nImmediateBatteryLevel/1000.0 );
		displayLCDString(0, 0, str );
		sprintf( str, "%3d %3.2f %3.2f ", flywheel.motor_drive, flywheel.drive, canopy_servo_value );
		displayLCDString(1, 0, str );

		// Don't hog the cpu :)
		wait1Msec(10);
	}
}
