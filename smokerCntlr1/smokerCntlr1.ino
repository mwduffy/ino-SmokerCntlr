/*
 *  smokerCntlr1
 *  Copyright (C) 2021 Michael William Duffy
 *  
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version. 
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details. 
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *  
 */

/*
	smokerCntlr1
	2021-03-17 MWD
	The Smoker Controller No.1 is the first crack at replacing the OEM controller board
	for the Masterbuilt	Electric Smoker Model 20070512.
	
	This program implements a finite state machine (FSM). It creates a user interface
	consisting of a 16x2 LCD or OLED module, and a switchable potentiometer (5 pin).
	The pot switch is used to turn the interface on or off. When on, the only user
	action is to set the temperature via the pot dial. A small time delay is used to 
	reduce jitter and to ensure the dial has stopped moving before it decides 
	that the new setting is to take effect.

	Note: There is a distinction between powering up the smoker system, by means of 
	plugging the power cord to the mains, and powering on the display and controls, 
	by means of the potentiometer switch. The former is beyond the scope of software,
	obviously and is a precondition for the software to run. The latter is a user 
	interface event under control of this software.
	
	Note: This does not manage the smoker's light. That is a TODO item. Some options are 
	1. set the light pin HIGH in BEGIN state and leave it on or 2. add another physical 
	interface switching device or 3. add a sensor to detect when light should be on.
	Option 3 could be as simple as an ambient light detector to turn it on when it gets 
	dark, or more complex sensing such as motion detection to use a wave of a hand to 
	toggle the light state.
*/

// TODO 3rd thermistor for board temp to be used for warning when board temp > 50 C.
// maybe add buzzer and flash LEDs after stopping heating element

#include <LiquidCrystal.h>

#define PROGRAM_NAME "SmokerCntlr  V01"
#define EXTPOWER		// The smoker will provide external power to the Arduino
#define DEBUG 	// Serial output for debugging
#define DEBUG_LVL	1	// Extra output to the Serial interface
//#define DISPLAY_ADC_READING	// select ADC values (temperature is default)
//#define BREADBOARD		// select breadboard layout (soldered board is default)

/* 
	Declare types for things that vary by platform 
*/
typedef unsigned int t_adc_val;
typedef float t_temp;
typedef unsigned long t_time;

/*
	Declare Arduino pins used by the LiquidCrystal library.
	This constructor initializes 4-bit mode.
*/
//    OLED symbol RS E  D4 D5  D6  D7
//           pin# 4  6  11 12  13  14
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

/*
	States persist across iterations of the loop() by means of a global context structure.
	Once the setup() has executed the program will always be in one of the following states. 
*/
typedef enum t_state { 
	BEGIN,   	// Establish initial conditions or return to these conditions 
	STANDBY, 	// The user interface is active but no heating
	SETMODE, 	// The user is actively changing the temperature setting. No heating.
	SESSION  	// The system manages the heating element based on the setting.
} t_state;

/*
	Declare the information used for context management in one typed structure. 
*/
typedef struct {
	t_state state;
	t_time begin_time;			// last reset
	t_time alive_time;			// current time at entry to loop()
	t_time session_start_time;	// time first entered SESSION state
	t_time session_elapsed_time; // cooking time elapsed (for display)
	t_time target_set_time;		// time last change to target from pot
	t_temp target_pot_temp;		// last read from potentiometer
	t_temp stove_target_temp;	// target temp for comparison to stove temp
	t_temp stove_temp;			// current stove temperature
	t_temp meat_temp;           // current meat probe temp	
	t_temp board_temp;           // ambient temp of board environment
} t_context;

t_context ctx;

// TODO implement an ambient temp sensor for the board
//#define BOARD_THERM_PIN A5 	

#ifdef BREADBOARD
#define STOVE_THERM_PIN A7 	// oven temp sensor 
#define MEAT_THERM_PIN  A0 	// meat temp sensor
#define SET_POT_PIN 	A3 	// set target temp
#define STOVE_RELAY_PIN 5 	// relay switch for stove heating element
#define STOVE_LED_PIN 	4   	// LED indicator lamp for stove heating element
#else  
// See Nano Interface Board wire graph
#define STOVE_THERM_PIN A0 	// oven temp sensor 
#define STOVE_RELAY_PIN A1 	// relay switch for stove heating element
#define SET_POT_PIN     A2 	// set target temp
#define MAIN_SWITCH_PIN	A3 	// main switch (used as digital input)
#define LIGHT_SWITCH_PIN A4 	// light switch
#define MEAT_THERM_PIN  A5	// meat temp sensor
#define STOVE_LED_PIN   A6   	// LED indicator lamp (red) for stove heating element
#define MAIN_LED_PIN    A7   	// LED indicator lamp (blue)for main switch
#define OLED_RS  4   		// OLED Register Select
#define OLED_E   6   		// OLED Enable
#define OLED_DB4 7   		// OLED Data Bus 4
#define OLED_DB5 8   		// OLED Data Bus 5
#define OLED_DB6 9   		// OLED Data Bus 6
#define OLED_DB7 10   		// OLED Data Bus 7
#endif



/* mwd - replaced following constants with above defines from ThermoSetter.ino
	Define global constants for pins. keep this in sync with the board design.

const int STOVE_THERM_PIN = A0;  // thermistor to get oven temp
const int MEAT_THERM_PIN  = A2;  // thermistor to get meat temp
const int board_sensor_pin = A4;  // thermistor to get board environment temp
const int SET_POT_PIN   = A6;  // potentiometer to set target temp
const int MAIN_SWITCH_PIN = A7;  // switch to indicate controller is active
const int power_led_pin    = D1;  // indicator LED for controller is active
const int stove_led_pin    = D2;  // indicator LED for oven relay state
const int light_led_pin    = D3;  // indicator LED light relay state
const int meat_bias_pin    = D4;  // power to meat thermistor when HIGH
const int stove_relay_pin  = D5;  // oven relay state on(HIGH)/off(LOW)
const int light_relay_pin  = D6;  // light relay state on(HIGH)/off(LOW)
// LCD module library uses pins D7-D12 inclusive
const int display_power_pin = D13: // OLED display module power pin
*/

/*
	Define the thermistor sensors and their parameters.
	I don't have definitive information on the devices used in the smoker so these 
	are heuristics taken from a commonly used thermistor.
*/
typedef struct {
	float res;   		// resistance class of the thermistor
	float cA, cB, cC; 	// Steinhart-Hart coefficients for this device
	int pin;			// pin assignment on the board
} t_thermistor;

const t_thermistor stove_therm = {
	.res = 10000, 
	.cA = 1.009249522e-03, 
	.cB = 2.378405444e-04, 
	.cC = 2.019202697e-07, 
	.pin = STOVE_THERM_PIN
};

const t_thermistor meat_therm = {
	.res = 10000, 
	.cA = 1.009249522e-03, 
	.cB = 2.378405444e-04, 
	.cC = 2.019202697e-07, 
	.pin = MEAT_THERM_PIN
};

#ifdef BOARD_THERM_PIN
const t_thermistor board_therm = {
	.res = 10000, 
	.cA = 1.009249522e-03, 
	.cB = 2.378405444e-04, 
	.cC = 2.019202697e-07, 
	.pin = BOARD_THERM_PIN
};
#endif

//
// loop context
//
unsigned long time_last_measured = 0;
unsigned long time_last_displayed = 0;
unsigned long time_last_heater_update = 0;
unsigned long time_last_checkpoint = 0;
unsigned long last_minute = 0;
unsigned long read_cnt = 0;
float chkpt_band = 0;
float last_chkpt_temp = 0; 
unsigned int circular_inx = 0;
bool stoveOn = false;
bool displayActive = false;
char hhmm_str[9]; 


/* 
	float2int()
	This takes a float, rounds to nearest integer *with respect for sign) and 
	returns an int.
*/
int float2int(float x) {
	return x >= 0 ? (int)(x + 0.5) : (int)(x - 0.5);
}

/*
	initContext creates the global conditions for BEGIN state. 
*/
void initContext() {
	memset(&ctx, 0, sizeof(t_context));
	ctx.state = BEGIN;
	ctx.begin_time = millis();
}

/*
	readTemp encapsulates the details of determining the temperature using the 
	Steinhart-Hart Equation. Pass in the structure defining the thermistor.
	It returns the Fahrenheit temperature. 
*/
t_temp readTemp(t_thermistor *th) {
	t_adc_val pinval = analogRead(th->pin);
	float res2 = th->res * (1024.0 / (float)pinval - 1.0);
	t_temp temp = (1.0 / (th->cA + th->cB * log(res2) + th->cC * log(res2) * log(res2) * log(res2)));
	temp = temp - 273.15;
	temp = (temp * 9.0)/ 5.0 + 32.0; 
	#if (DEBUG_LVL >= 1)
	char strbuf[100];
	int temp_rnd = float2int(temp);	
	Serial.write(strbuf, snprintf(strbuf, sizeof(strbuf), 
		"A%d:%d  %d \n", th->pin, pinval, temp_rnd) );
	#endif
	
	return temp;
}

/*
	readTargetTemp determines the target temperature by reading the potentiometer 
	from an ADC pin. The raw value, 0-1023, is scaled to a fixed temperature range. 
	The range goes from the minimum standby temp through the minimum cooking temp
	to the maximum target temp.
	In order to deal with possible jitter and to make manual movement of the dial easier,
	the temperature returned is rounded to the nearest 5 degrees Fahrenheit. 
*/
t_temp readTargetTemp() {

#define SET_POT_ERROR 12	// Pot value actually reads as 10-1021 leaving (1024-10-2) increments
#define MAX_SET_TEMP  350	// allow target temp to be set up to this
#define TEMP_SESSION  150  	// minimum cooking temp defines threshold of STANDBY/SESSION states
#define TEMP_STANDBY  TEMP_SESSION - 10  	// target temp cannot be set below this
#define TEMP_RANGE    MAX_SET_TEMP - TEMP_STANDBY  // permitted range to set temp

	t_temp increment = (float) TEMP_RANGE / (float) (1024 - SET_POT_ERROR);
	int pot_value = analogRead(SET_POT_PIN);
	t_temp scaled_temp = (increment * pot_value) + TEMP_STANDBY;
	scaled_temp = (((int) (scaled_temp + 2.5)) / 5) * 5; // round to 5 deg F
	return scaled_temp;
}

/*
	isTargetValid() reads the Potentiometer pin, translated to a rounded temperature, 
	and compares this to the previously set target temp. The target is valid when 
	the two temps are equal. Return the boolean result.
*/
boolean isTargetValid() {
	ctx.target_pot_temp = readTargetTemp();
	if (ctx.target_pot_temp == ctx.stove_target_temp) {
		return true;
	} else {
		return false;
	}
}

/*
	isPowerSwitchOn() reads the power switch pin. Return the boolean result.
*/
boolean isPowerSwitchOn() {
	if (HIGH == analogRead(MAIN_SWITCH_PIN)) {
		return true;
	} else {
		return false;
	}
}

/*  mwd copied the display handling from ThermoSetter
void setDisplayOn() {
	digitalWrite(display_power_pin, HIGH);
}

void setDisplayOff() {
	digitalWrite(display_power_pin, LOW);
}
*/

/*
	quiesceDisplay()
	This gives the appearance of turning the display off. The temperature setting
	potentiometer has a switch which when turned off (passing through the STANDBY
	low temperature setting) will cause the sceen to go blank. For an OLED this
	looks like the power to the module is off. 
*/
void quiesceDisplay() {	
	#if (DEBUG_LVL >= 1)
	Serial.print("==> Quiesce Display\n");
	#endif	
	
	lcd.noDisplay();
	digitalWrite(MAIN_LED_PIN, LOW);
	displayActive = false;
}

/*
	resetDisplay()
	This gives the appearance of turning the screen on.  
	The user can address screen corruption caused by transients in 
	the circuits by turning the switch off then on without losing the elapsed time.
*/
void resetDisplay() {
	#if (DEBUG_LVL >= 1)
	Serial.print("==> Reset Display\n");
	#endif	
	digitalWrite(MAIN_LED_PIN, HIGH);

	lcd.display();
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print(PROGRAM_NAME);
	delay(1000);
	lcd.setCursor(0, 1);
	if (time_last_measured == 0) {
		lcd.print("Set temp");
	} else {
		lcd.print("Display reset");
	}
	delay(5000);
	lcd.clear();
	displayActive = true;
}

/* mwd copied stove handling from ThermoSetter
void setStoveOn() {
	digitalWrite(stove_relay_pin, HIGH);
	digitalWrite(stove_led_pin, HIGH);
}
void setStoveOff() {
	digitalWrite(stove_relay_pin, LOW);
	digitalWrite(stove_led_pin, LOW);
}
*/

bool isStoveOn() {
	return stoveOn;
}

void setStoveOn() {
	stoveOn = true;
	digitalWrite(STOVE_RELAY_PIN, HIGH);
	digitalWrite(STOVE_LED_PIN, HIGH);
	#if (DEBUG_LVL >= 2)
	Serial.print("==> setStoveOn() \n");
	#endif	
}

void setStoveOff() {
	stoveOn = false;
	digitalWrite(STOVE_RELAY_PIN, LOW);
	digitalWrite(STOVE_LED_PIN, LOW);
	#if (DEBUG_LVL >= 2)
	Serial.print("==> setStoveOff() \n");
	#endif	
}

void setLightOn() {
	digitalWrite(LIGHT_SWITCH_PIN, HIGH);
}

void setLightOff() {
	digitalWrite(LIGHT_SWITCH_PIN, LOW);
}

/*
	displayStatus gives feedback to the user via a 16x2 display module.
	
	0123456789012345
	----------------
	 HH:MM   T:999 
	 O:999   M:999
	----------------
*/
void displayStatus(){

	// Format time as HH:MM with trailing blank and null terminator 
	t_time seconds = ctx.alive_time/1000;
	t_time hours = seconds/3600;
	t_time minutes = (seconds%3600)/60;
	char hhmm_str[9]; // account for null terminator
	// use modular division to truncate when > 100
	snprintf(hhmm_str,sizeof(hhmm_str),"%02lu:%02lu",hours%100,minutes%100);

	// Format temp as a prefix plus a 3-digit integer w/ trailing blank and null
	int temp_rnd;
	char target_temp_str[9];
	char stove_temp_str[9];
	char meat_temp_str[9];

	// if target is not a cooking temperature (i.e.STANDBY), display zero.
	if (ctx.stove_target_temp < TEMP_SESSION) {
		temp_rnd = 0;
	} else {
		temp_rnd = float2int(ctx.stove_target_temp);
	}
	snprintf(target_temp_str,sizeof(target_temp_str),"T:%03d ",temp_rnd);
	
	temp_rnd = float2int(ctx.stove_temp);
	snprintf(stove_temp_str,sizeof(stove_temp_str),"O:%03d ",temp_rnd);
	temp_rnd = float2int(ctx.meat_temp);
	snprintf(meat_temp_str,sizeof(meat_temp_str),"M:%03d ",temp_rnd);
  
	// Display
	lcd.setCursor(0, 0);
	lcd.print(hhmm_str);
	lcd.setCursor(8, 0);
	lcd.print(target_temp_str);

	lcd.setCursor(0, 1);
	lcd.print(stove_temp_str);
	lcd.setCursor(8, 1);
	lcd.print(meat_temp_str);
	
	// indicate current state
	lcd.setCursor(15,0);
	lcd.print((int) ctx.state);
	

// TODO consider adding an LED pin for STANDBY
// TODO Consider adding status characters for each state to the right side of the display

	#if (DEBUG_LVL >= 1)
	char strbuf[100];
	size_t n = snprintf(strbuf, sizeof(strbuf), 
		"HH:MM %s  %s  %s  %s \n", 
		hhmm_str, stove_temp_str, meat_temp_str, target_temp_str);
	Serial.write(strbuf, (n >= sizeof(strbuf))? sizeof(strbuf) : n);
	#endif
	
}

/*************************************
*
*	Standard Arduino Sketch setup()
*
**************************************/

void setup() {

#ifdef EXTPOWER
  analogReference(EXTERNAL);  // tell the board that an external power source is applied to the AREF pin
  // discard a few reads as per spec.
  for (int i=0; i>3; i++) {
    analogRead(STOVE_THERM_PIN );
    analogRead(MEAT_THERM_PIN  );
    analogRead(SET_POT_PIN   );
    analogRead(MAIN_SWITCH_PIN );
	#ifdef BOARD_THERM_PIN
    analogRead(BOARD_THERM_PIN );
	#endif
  }
#endif
	
	#if (DEBUG_LVL >= 1)
	Serial.begin(9600);
	while (!Serial) {}  // wait for serial port to initialize
	Serial.println("Starting Smoker Controller");
	#endif

	initContext();

	// Declare I/O status of digital pins
	pinMode(MAIN_LED_PIN , OUTPUT);
	pinMode(STOVE_LED_PIN, OUTPUT);
	pinMode(STOVE_RELAY_PIN, OUTPUT);
	pinMode(LIGHT_SWITCH_PIN, OUTPUT);
/* mwd: the display is always at Vss. Display off now via LiquidCrystal API
	pinMode(display_power_pin, OUTPUT);
*/
/* mwd: board revision - now wired to Vss
	pinMode(meat_bias_pin, OUTPUT);  
*/
/* mwd: was used on breadboard but don't need an indicator for the light
	pinMode(light_led_pin, OUTPUT);
*/
	lcd.begin(16, 2);
	delay(500);
	lcd.setCursor(0, 0);
	lcd.print("Starting Smoker");
	lcd.setCursor(0, 1);
	lcd.print(" V1.0");
	delay(3000);
	
}

/************************************
*	Standard Arduino sketch loop().
*
*	Each iteration takes sensor readings, updating the FSM context. 
*	The current state is dispatched, returning the next current state. 
*	Each state applies a few simple rules to make changes to the device and to select 
*	the next state while performing minimal processing of the shared context.
*	There is no end state. Turning the dial switch to off sets the BEGIN state, 
*	turning the UI display off. Pulling the plug shuts the smoker off 
*
***************************************/
void loop() {

	ctx.alive_time = millis();
	#ifdef BOARD_THERM_PIN
	ctx.board_temp = readTemp(&board_therm);
	#endif
	ctx.stove_temp = readTemp(&stove_therm);

	/* TODO Determine whether or not to continuously power the meat thermistor.
		The following code assumes the meat sensor is constantly powered.
		The OEM controller powered the meat sensor from a data pin, likely tied 
		to a button (labeled "Meat"). Did they do this to reduce current draw?
		Maybe. But more likely there is a single 4-digit display that is multiplexed 
		for time, target temp, oven temp, and meat temp. Hence the button.
		So alternatively, I can tie the power to the sensor to a data pin
		then every X milli seconds set it HIGH to power the sensor and read it.
		If so, there is a start up period before the reading will stabilize.
		Let's just KISS. 
	*/
	ctx.meat_temp = readTemp(&meat_therm);
	
	displayStatus();
	
	switch (ctx.state) {
		case BEGIN :
			ctx.state = beginState();
			break;
		case STANDBY:
			ctx.state = standbyState();
			break;
		case SETMODE:
			ctx.state = setmodeState();
			break;
		case SESSION:
			ctx.state = sessionState();
			break;
		default:
			ctx.state = BEGIN;
			#if (DEBUG_LVL >= 1)
			Serial.println("Invalid state. Reverting to BEGIN "); 
			#endif
	}
	/* TODO determine if a delay is needed
		An argument against delay is that the system responds much faster than the user
		can turn the dial. So going from a valid target temp (SESSION) to power off 
		(BEGIN) without entering SETMODE is not likely. 
		Adding a delay makes it more likely.
	*/
	//delay(500);
}

//
// State handlers
//

/*
	BEGIN
	This state establishes initial conditions for the smoker. It is entered at the start
	of operation and can also be returned to at a later time if the "power" switch is
	turned off. The smoker will remain in this state until the power switch on 
	condition is detected. If the whole system is plugged in while the power switch 
	is in the on position this state executes once then transitions to STANDBY. 
*/
t_state beginState() {
	initContext();
	#if (DEBUG_LVL >= 1)
	Serial.println("state: BEGIN "); 
	#endif
	quiesceDisplay();
	setStoveOff();
	setLightOff();
/* mwd: the meat thermistor is now wired to be HIGH at all times
	// TODO decide whether the meat sensor is always powered, by setting the bias pin HiGH, or only when being read.
	digitalWrite(meat_bias_pin, LOW);
*/
	if (isPowerSwitchOn()){
		return STANDBY;
	}
	return BEGIN;
}

/*
	STANDBY
	This state shuts off the oven and remains in STANDBY with the display on until a 
	target temp setting event occurs. Then it transitions to SETMODE.
*/
t_state standbyState() {

	#if (DEBUG_LVL >= 1)
	Serial.println("state: STANDBY "); 
	#endif

	resetDisplay();
	setStoveOff();

	if (! isPowerSwitchOn() ) {
		return BEGIN;
	}

	if(! isTargetValid()) {
		return SETMODE;
	}
	return STANDBY;
}

/*
	SETMODE
	This manages the setting of the target temperature. It is active when the value
	of the potentiometer no longer agrees with the previously set target temperature. 
	This will occur repeatedly while the user moves the dial.
	The state remains active until the two values agree for a short period of time. 
	This time period is interpreted to be the user no longer turning the dial.
	
	When a new target is established, SETMODE transitions to either STANDBY or SESSION 
	depending on the new value being less (or not) than the minimum cooking session 
	temperature.
*/
t_state setmodeState() {

#define SETMODE_TIME_TO_WAIT 1000

	#if (DEBUG_LVL >= 1)
	Serial.println("state: SETMODE "); 
	#endif

	// Don't know how long we'll be in setmode so turn off the heating element to be safe
	setStoveOff();

	// going quickly from a dial setting to power off without waitng...
	if (! isPowerSwitchOn()) {
		return BEGIN;
	}

	if (! isTargetValid()) {
		ctx.stove_target_temp = ctx.target_pot_temp; 
		ctx.target_set_time = ctx.alive_time;
		return SETMODE;
	}
	// The two temps are in agreement but for a long enough time?
	if ((ctx.alive_time - ctx.target_set_time) < SETMODE_TIME_TO_WAIT) {
		return SETMODE;	
	}	
	// Now that the two temps have been in agreement for the wait period, 
	// is the new target hot enough to start a cooking session?
	if (ctx.stove_target_temp > TEMP_SESSION) {
		return SESSION;
	} 
	return STANDBY;
}

/*
	SESSION
	This state manages the heating element to maintain the sensed temperature to be 
	close to the target temperature. The controller remains in SESSION state until
	a target setting event occurs, causing a transition to SETMODE.

	TODO Code for hysteresis, over/under shooting the target. PID comes to mind,
	but perhaps something simpler. The following code will over/under shoot but 
	by how much? If we learn that this particular box lags by a predictable 
	value, then we can fuzzy the comparison test. That's what a PID algorithm does.
*/
t_state sessionState() {
	#if (DEBUG_LVL >= 1)
	Serial.println("state: SESSION "); 
	#endif
	// TODO Is it possible to turn off switch while isTargetValid() is true?
	if (! isPowerSwitchOn()) {
		return BEGIN;
	}
	
	if (! isTargetValid()) {
		return SETMODE;
	} 
	
	// TODO following code may be moot as this test is required to enter SESSION state
	//if (ctx.stove_target_temp < TEMP_SESSION) {
	//	return STANDBY;
	//}
	
	// Elapsed time is from the first SESSION following a BEGIN state
	if (ctx.session_start_time == 0) {
		ctx.session_start_time = ctx.alive_time;
	}
	
	if (ctx.stove_temp > ctx.stove_target_temp) {
		setStoveOff();
	} else {
		setStoveOn();
	}
		
	return SESSION;
}
