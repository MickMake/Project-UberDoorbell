/*
* Mick Hellstrom
*/

#include <SimbleeForMobile.h>
#include <RingBuf.h>
#include <Wire.h>
#include <string.h>
#include <TimeLib.h>
#include <stdio.h>
#include "notes.h"


// Debug mode.
//#define		DEBUG

// debounce time (in ms).
#define DEBOUNCE_TIME		200

// maximum debounce timeout (in ms).
#define DEBOUNCE_TIMEOUT	500

// Maximum logfile display entries.
#define MAX_DISP_ENTRIES	20

// Maximum log entries.
#define MAX_LOG_ENTRIES		400


/*
** ULPwakeOut and ULPwakeIn pins are used to wake the Simblee up from a deep sleep.
** This is how it works:
** 1. ULPwakeIn is set to INPUT.
** 2. Simblee_pinWakeCallback(ULPwakeIn, HIGH, cbWake) is called
**	which will execute cbWake() when ULPwakeIn goes HIGH.
** 3. When we connect from our iPhone via BLE, then SimbleeForMobile_onConnect will set ULPwakeOut HIGH.
** 4. cbWake() will be called which exist with 0, which causes Simblee_ULPDelay() to exit.
** 5. loop() then continues to run freely which updates the iPhone app.
** 6. If we disconnect, then we set ULPwakeOut to LOW, and push it back into deep sleep.
**
** There are two other methods of waking from ULP:
** 1. Pressing the Doorbell.
** 2. Closing the door.
*/
int ULPwakeOut = 12;
int ULPwakeIn = 15;

int ledPin = 14;
int Amplifier = 13;	/* This is the blue LED on the Lilypad Simblee - now Amplifier enable/disable. */
int Speaker = 9;	/* Attach a piezo or amplifier + speaker for the ding dong. */
int Doorbell = 3;	/* Is the Doorbell button pin. */
int DoorMove = 11;	/* Is the vibration switch pin, to detect the door closing. */ 


uint8_t uiDoorbell;
uint8_t uiDoorbellMute;
bool DoorbellMute = LOW;	/* For now unmute the Doorbell. */
uint8_t uiDoorbellState;
uint8_t uiDoorMoveState;
uint8_t uiLogfile[MAX_DISP_ENTRIES];
uint8_t uiLog;
uint8_t uiLogBack;
uint8_t uiSet;
uint8_t uiSetTime;
uint8_t uiSetBack;

int setYear = 2016;
uint8_t uiTFyear;
uint8_t uiSyear;
int setMonth = 7;
uint8_t uiTFmonth;
uint8_t uiSmonth;
int setDay = 31;
uint8_t uiTFday;
uint8_t uiSday;

int setHour = 12;
uint8_t uiTFhour;
uint8_t uiShour;
int setMin = 30;
uint8_t uiTFmin;
uint8_t uiSmin;
int setSec = 00;
uint8_t uiTFsec;
uint8_t uiSsec;

int currentScreen;

// Notes in the melody:
int melody[] =
{
	// NOTE_D8, NOTE_D7, NOTE_D6, NOTE_D5, NOTE_D4, NOTE_D3, NOTE_D2, NOTE_D1
	NOTE_C5, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_G4, 0, NOTE_B4, NOTE_C5
};

// Note durations: 4 = quarter note, 8 = eighth note, etc.:
int noteDurations[] =
{
	8, 16, 16, 8, 8, 8, 8, 8
};

struct EntryStruct
{
	int index;
	bool BellPush;
	time_t TimeStamp;
};
RingBuf *Logger = RingBuf_new(sizeof(struct EntryStruct), MAX_LOG_ENTRIES);


// ################################################################################
void setup()
{
	Serial.begin(9600);
#ifdef DEBUG
	Serial.println("\nUber Doorbell by MickMake");
#endif

	// Un-mute the doorbell.
	DoorbellMute = LOW;

	// ledPin turned on/off from the iPhone app
	pinMode(ledPin, OUTPUT);
	digitalWrite(ledPin, LOW);

	// Amplifier enable/disable.
	pinMode(Amplifier, OUTPUT);
	digitalWrite(Amplifier, LOW);	// Disable amplifier until doorbell press.

	// Output to amplifier.
	pinMode(Speaker, OUTPUT);
	digitalWrite(Speaker, LOW);

	// Used to pull out of ULP mode for SimbleeForMobile.
	pinMode(ULPwakeOut, OUTPUT);
	digitalWrite(ULPwakeOut, LOW);
	pinMode(ULPwakeIn, INPUT);
	Simblee_pinWakeCallback(ULPwakeIn, HIGH, cbWake);
	//Serial.print("ULPwakeIn: "); Serial.println(digitalRead(ULPwakeIn));

	// Doorbell press will be shown on the iPhone app)
	pinMode(Doorbell, INPUT_PULLUP);
	// Simblee_pinWake(Doorbell, HIGH);
	// Simblee_pinWakeCallback(Doorbell, HIGH, cbDoorbell);

	// DoorMove press will be shown on the iPhone app)
	pinMode(DoorMove, INPUT_PULLUP);
	// Simblee_pinWake(DoorMove, LOW);
	// Simblee_pinWakeCallback(DoorMove, LOW, cbDoorMove);

	// Do we have enough memory for the ring buffer?
	if (!Logger)
	{
		Serial.println("Not enough memory for RingBuf.");
		while(1);
	}

	Wire.begin();

	// Check the current date/time and set it to something sane.
	checkTime();

	// this is the data we want to appear in the advertisement
	// (if the deviceName and advertisementData are too long to fix into the 31 byte
	// ble advertisement packet, then the advertisementData is truncated first down to
	// a single byte, then it will truncate the deviceName)
	SimbleeForMobile.advertisementData = "Doorbell";

	// Use a shared cache
	SimbleeForMobile.domain = "mickmake.com";

	SimbleeForMobile.begin();

#ifdef DEBUG
	Serial.println("\nBegin");
#endif
}


void loop()
{
	if (SimbleeForMobile.updatable)
	{
		if (currentScreen == 1)
		{
			SimbleeForMobile.updateColor(uiDoorbellState, digitalRead(Doorbell) ? WHITE : GREEN);
			SimbleeForMobile.updateColor(uiDoorMoveState, digitalRead(DoorMove) ? WHITE : GREEN);
			SimbleeForMobile.updateValue(uiDoorbellMute, DoorbellMute);
		}
		else if (currentScreen == 3)
		{
			// updateTimeSliders();
		}
	}

	SimbleeForMobile.process();

	// Once we are connected, 
	if (!digitalRead(ULPwakeIn))
	{
		WaitUntilConnect();
	}
}


void AddLog(bool BellPush)
{
	time_t DateTime;

	DateTime = getTime();

	// Create the entry structure and clear it out.
	struct EntryStruct Entry;
	memset(&Entry, 0, sizeof(struct EntryStruct));

	Entry.BellPush = BellPush;
	Entry.TimeStamp = DateTime;

#ifdef DEBUG
	char strDate[20];
	sprintTime(strDate, Entry.TimeStamp);
	Serial.print(strDate);
	if (Entry.BellPush)
	{
		Serial.println(" Doorbell rang.");
	}
	else
	{
		Serial.println(" Door moved.");
	}
#endif

	Logger->add(Logger, &Entry);
}


void getLogEntry(String &strLog)
{
	unsigned int Elements = Logger->numElements(Logger);

	if (Elements == 0)
	{
		return;
	}

	// Create the entry structure.
	struct EntryStruct Entry;

	char strDate[20];
	memset(&Entry, 0, sizeof(struct EntryStruct));
	Logger->pull(Logger, &Entry);

	sprintTime(strDate, Entry.TimeStamp);
	strLog = strDate;
	if (Entry.BellPush)
	{
		strLog += " Doorbell rang.";
	}
	else
	{
		strLog += " Door moved.";
	}
}


void getFullLog(String &strLog)
{
	unsigned int Elements = Logger->numElements(Logger);
	String Temp;

	// Create the entry structure.
	struct EntryStruct Entry;

#ifdef DEBUG
	Serial.print("Size: ");
	Serial.println(Elements);
#endif

	char strDate[20];
	for(; (Elements > 0); Elements--)
	{
		getLogEntry(Temp);
		strLog += Temp;
	}

#ifdef DEBUG
	Serial.println(strLog);
#endif
}


void uiShowLogfile()
{
	unsigned int Elements = Logger->numElements(Logger);

	// Create the entry structure.
	struct EntryStruct Entry;

#ifdef DEBUG
	Serial.print("Size: ");
	Serial.println(Elements);
#endif

	for(int Index = 0; ((Index < Elements) && (Index < MAX_DISP_ENTRIES)); Index++)
	{
		String strLog;
		getLogEntry(strLog);
		char foo[40];
		strLog.toCharArray(foo, 40);

#ifdef DEBUG
		Serial.print("strLog: ");
		Serial.println(strLog);
#endif

		SimbleeForMobile.updateText(uiLogfile[Index], foo);
		//uiLogfile[Index] = SimbleeForMobile.drawText(10, 130+(Index*10), "");
	}
}


/* This function will create the ding dong sound via PWM. */
int DingDong()
{
	int MelodyLength = sizeof(melody) / 4;		// int is 4 bytes wide.
#ifdef DEBUG
	Serial.print("Melody(");
	Serial.print(MelodyLength);
	Serial.print("): ");
#endif

	// If we don't want to hear the doorbell then don't activate the amplifier.
	// We're using the DingDong() function as "keybounce" as well, so we just don't
	// activate the amplifier, instead of returning.
	if (!DoorbellMute)
	{
#ifdef DEBUG
		Serial.print("Enable Amp: ");
#endif
		digitalWrite(Amplifier, HIGH);	// Enable amplifier.
		delay(300);			// Seems it takes a while for the amp to power up.
	}

	// iterate over the notes of the melody:
	for (int thisNote = 0; (thisNote < MelodyLength); thisNote++)
	{
		// to calculate the note duration, take one second
		// divided by the note type.
		//e.g. quarter note = 1000 / 4, eighth note = 1000/8, etc.
		int noteDuration = 1000 / noteDurations[thisNote];

		// tone(Speaker, melody[thisNote], noteDuration);
#ifdef DEBUG
		Serial.print(melody[thisNote]);
		Serial.print("/");
#endif
		tone(Speaker, melody[thisNote]);
		delay(noteDuration);

		// to distinguish the notes, set a minimum time between them.
		// the note's duration + 30% seems to work well:
		int pauseBetweenNotes = noteDuration * 1.30;
#ifdef DEBUG
		Serial.print(pauseBetweenNotes);
#endif
		delay(pauseBetweenNotes);

		// stop the tone playing:
#ifdef DEBUG
		Serial.print(" ");
#endif
		noTone(Speaker);
	}
#ifdef DEBUG
	Serial.println(".");
#endif

	digitalWrite(Amplifier, LOW);	// Disable amplifier.

	return(0);
}


int debounce(int pin, int state, int debounce_time, int debounce_timeout)
{
	int start = millis();
	int debounce_start = start;

	while (millis() - start < debounce_timeout)
	{
		Serial.print("pin: "); Serial.println(digitalRead(pin));
		if (digitalRead(pin) == state)
		{
			if (millis() - debounce_start >= debounce_time)
			return 1;
		}
		else
			debounce_start = millis();
	}

	return 0;
}


int WaitUntilConnect()
{
	// set ULPwakeIn edge to wake up on
	Simblee_pinWake(Doorbell, LOW);		// NO button.
	Simblee_pinWake(DoorMove, LOW);		// NO button.

#ifdef DEBUG
	Serial.println("Sleep...");
#endif

	// Switch to ULP mode until a button edge wakes us up.
	Simblee_ULPDelay(INFINITE);
	//Serial.print("ULPwakeIn: "); Serial.println(digitalRead(ULPwakeIn)); Serial.print("DoorMove: "); Serial.println(digitalRead(DoorMove)); Serial.print("Doorbell: "); Serial.println(digitalRead(Doorbell));

	if (Simblee_pinWoke(Doorbell))
	{
		// We don't need to worry about debounce. Just DingDong once.
		// while(!debounce(Doorbell, HIGH));
		AddLog(HIGH);
		DingDong();
		Simblee_resetPinWake(Doorbell);
	}

	else if (Simblee_pinWoke(DoorMove))
	{
		//Serial.print("ULPwakeIn: "); Serial.println(digitalRead(ULPwakeIn)); Serial.print("DoorMove: "); Serial.println(digitalRead(DoorMove)); Serial.print("Doorbell: "); Serial.println(digitalRead(Doorbell));
		// while(!debounce(DoorMove, LOW, DEBOUNCE_TIME, DEBOUNCE_TIMEOUT));
		delay(500);
		AddLog(LOW);
		Simblee_resetPinWake(DoorMove);
	}
}


/*
** Callback function used to wakeup the Simblee when connecting via BLE.
** This will cause an exit out of the WaitUntilConnect() function which will
** continue on with the loop, and therefore run SimbleeForMobile.process() again.
*/
int cbWake(uint32_t ulPin)
{
	//Serial.print("ULPwakeIn: "); Serial.println(digitalRead(ULPwakeIn));
	return(1);	/* exit of TRUE will cause Simblee_ULPDelay() to exit. */
}


void SimbleeForMobile_onConnect()
{
#ifdef DEBUG
	Serial.println("BLE: Connect");
	//Serial.print("ULPwakeIn: "); Serial.println(digitalRead(ULPwakeIn));
#endif

	// ledPin now controls amplifier - digitalWrite(ledPin, HIGH);
	digitalWrite(ULPwakeOut, HIGH);

	currentScreen = -1;
}


void SimbleeForMobile_onDisconnect()
{
#ifdef DEBUG
	Serial.println("BLE: Disconnect");
#endif

	// ledPin now controls amplifier - digitalWrite(ledPin, LOW);
	digitalWrite(ULPwakeOut, LOW);
	Simblee_pinWakeCallback(ULPwakeIn, HIGH, cbWake);
}


// ################################################################################
void ui()
{
	String strLog;

	if (SimbleeForMobile.screen == currentScreen) return;

	currentScreen = SimbleeForMobile.screen;
	switch(currentScreen)
	{
		case 1:
			createScreen1();
			break;

		case 2:
			createScreen2();

			uiShowLogfile();
			break;

		case 3:
			createScreen3();

			updateTimeSliders();
			break;

		default:
			Serial.print("ui: Uknown screen requested: ");
			Serial.println(currentScreen);
	}
}


void createScreen1()
{
	// SimbleeForMobile.beginScreen(WHITE);
	color_t darkgray = rgb(85,85,85);
	SimbleeForMobile.beginScreen(darkgray);

	int textID = SimbleeForMobile.drawText(80, 100, "Info", WHITE, 40);
	uiSet = SimbleeForMobile.drawButton(90, 60, 100, "Set");
	SimbleeForMobile.setEvents(uiSet, EVENT_PRESS | EVENT_RELEASE);
	uiLog = SimbleeForMobile.drawButton(200, 60, 100, "Log");
	SimbleeForMobile.setEvents(uiLog, EVENT_PRESS | EVENT_RELEASE);

	//SimbleeForMobile.drawText(60, 90, "Remote Doorbell\n");

	//uiDoorbell = SimbleeForMobile.drawButton(120, 180, 80, "Ring Doorbell", BLUE, TEXT_TYPE);
	uiDoorbell = SimbleeForMobile.drawButton(90, 180, 130, "Ring Doorbell");
	SimbleeForMobile.setEvents(uiDoorbell, EVENT_PRESS | EVENT_RELEASE);

	// uiDoorbellMute = SimbleeForMobile.drawButton(90, 280, 130, "Mute Doorbell");
	SimbleeForMobile.drawText(110, 235, "Mute");
	uiDoorbellMute = SimbleeForMobile.drawSwitch(150, 230);
	SimbleeForMobile.updateValue(uiDoorbellMute, DoorbellMute);
	SimbleeForMobile.setEvents(uiDoorbellMute, EVENT_RELEASE);

	SimbleeForMobile.drawText(60, 310, "Press the Doorbell\n" "or close the door\n" "to change the images below.");

	SimbleeForMobile.drawText(44, 380, "Doorbell");
	SimbleeForMobile.drawRect(42, 400, 80, 80, BLACK);
	uiDoorbellState = SimbleeForMobile.drawRect(44, 402, 76, 76, WHITE);

	SimbleeForMobile.drawText(200, 380, "Door close");
	SimbleeForMobile.drawRect(198, 400, 80, 80, BLACK);
	uiDoorMoveState = SimbleeForMobile.drawRect(200, 402, 76, 76, WHITE);

	SimbleeForMobile.endScreen();
}


void createScreen2()
{
	SimbleeForMobile.beginScreen(WHITE);

	int textID = SimbleeForMobile.drawText(80, 100, "Log file", WHITE, 40);
	uiLogBack = SimbleeForMobile.drawButton(20, 60, 100, "< Back");
	//uiLogBack = SimbleeForMobile.drawButton(200, 60, 100, "Info");
	SimbleeForMobile.setEvents(uiLogBack, EVENT_RELEASE);

	// Frame up the log entries.
	SimbleeForMobile.drawRect(5, 125, 290, 410, BLACK);
	SimbleeForMobile.drawRect(6, 126, 288, 408, WHITE);
	for(int Index = 0; (Index < MAX_DISP_ENTRIES); Index++)
	{
		uiLogfile[Index] = SimbleeForMobile.drawText(10, 130+(Index*20), "....");
	}

	SimbleeForMobile.endScreen();
}


void createScreen3()
{
	color_t darkgray = rgb(85,85,85);
	SimbleeForMobile.beginScreen(darkgray);

	int textID = SimbleeForMobile.drawText(95, 100, "Time Set", WHITE, 40);
	uiSetBack = SimbleeForMobile.drawButton(20, 60, 100, "< Back");
	SimbleeForMobile.setEvents(uiSetBack, EVENT_RELEASE);

	SimbleeForMobile.drawText(20, 171, "Year:", WHITE);
	uiSyear = SimbleeForMobile.drawSlider(70, 165, 175, 2000, 2100);
	uiTFyear = SimbleeForMobile.drawTextField(245, 165, 60, 255, "", WHITE, darkgray);

	SimbleeForMobile.drawText(20, 216, "Month:", WHITE);
	uiSmonth = SimbleeForMobile.drawSlider(70, 210, 175, 1, 12);
	uiTFmonth = SimbleeForMobile.drawTextField(245, 210, 60, 255, "", WHITE, darkgray);

	SimbleeForMobile.drawText(20, 261, "Day:", WHITE);
	uiSday = SimbleeForMobile.drawSlider(70, 255, 175, 1, 31);
	uiTFday = SimbleeForMobile.drawTextField(245, 255, 60, 255, "", WHITE, darkgray);


	SimbleeForMobile.drawText(20, 351, "Hour:", WHITE);
	uiShour = SimbleeForMobile.drawSlider(70, 345, 175, 0, 23);
	uiTFhour = SimbleeForMobile.drawTextField(245, 345, 60, 255, "", WHITE, darkgray);

	SimbleeForMobile.drawText(20, 396, "Min:", WHITE);
	uiSmin = SimbleeForMobile.drawSlider(70, 390, 175, 0, 59);
	uiTFmin = SimbleeForMobile.drawTextField(245, 390, 60, 255, "", WHITE, darkgray);

	SimbleeForMobile.drawText(20, 441, "Sec:", WHITE);
	uiSsec = SimbleeForMobile.drawSlider(70, 435, 175, 0, 59);
	uiTFsec = SimbleeForMobile.drawTextField(245, 435, 60, 255, "", WHITE, darkgray);

	uiSetTime = SimbleeForMobile.drawButton(90, 500, 130, "Set Time");
	SimbleeForMobile.setEvents(uiSetTime, EVENT_PRESS | EVENT_RELEASE);

	SimbleeForMobile.endScreen();

	time_t DateTime;
	DateTime = getTime();
	setYear = year(DateTime);
	setMonth = month(DateTime);
	setDay = day(DateTime);
	setHour = hour(DateTime);
	setMin = minute(DateTime);
	setSec = second(DateTime);

	updateTimeSliders();
}


void updateTimeSliders()
{
	SimbleeForMobile.updateValue(uiSyear, setYear);
	SimbleeForMobile.updateValue(uiTFyear, setYear);
	SimbleeForMobile.updateValue(uiSmonth, setMonth);
	SimbleeForMobile.updateValue(uiTFmonth, setMonth);
	SimbleeForMobile.updateValue(uiSday, setDay);
	SimbleeForMobile.updateValue(uiTFday, setDay);

	SimbleeForMobile.updateValue(uiShour, setHour);
	SimbleeForMobile.updateValue(uiTFhour, setHour);
	SimbleeForMobile.updateValue(uiSmin, setMin);
	SimbleeForMobile.updateValue(uiTFmin, setMin);
	SimbleeForMobile.updateValue(uiSsec, setSec);
	SimbleeForMobile.updateValue(uiTFsec, setSec);
}


void ui_event(event_t &event)
{
	if (event.id == uiDoorbell)
	{
		if (event.type == EVENT_PRESS)
		{
#ifdef DEBUG
			Serial.println("UI: Doorbell Press");
#endif
			DingDong();
		}
		else if (event.type == EVENT_RELEASE)
		{
#ifdef DEBUG
			Serial.println("UI: Doorbell Release");
#endif
		}
	}

	if (event.id == uiLog && event.type == EVENT_RELEASE && currentScreen == 1)
	{
#ifdef DEBUG
		Serial.println("UI: Goto log screen.");
#endif
		SimbleeForMobile.showScreen(2);
	}

	if (event.id == uiSet && event.type == EVENT_RELEASE && currentScreen == 1)
	{
#ifdef DEBUG
		Serial.println("UI: Goto set screen.");
#endif
		SimbleeForMobile.showScreen(3);
	}

	if (event.id == uiLogBack && event.type == EVENT_RELEASE && currentScreen == 2)
	{
#ifdef DEBUG
		Serial.println("UI: Goto info screen.");
#endif
		SimbleeForMobile.showScreen(1);
	}

	if (event.id == uiSetBack && event.type == EVENT_RELEASE && currentScreen == 3)
	{
#ifdef DEBUG
		Serial.println("UI: Goto info screen.");
#endif
		SimbleeForMobile.showScreen(1);
	}

	if (event.id == uiDoorbellMute && event.type == EVENT_RELEASE)
	{
		// Toggle the DoorbellMute state.
		if (DoorbellMute)
		{
#ifdef DEBUG
			Serial.println("UI: Doorbell Un-mute");
#endif
			DoorbellMute = LOW;
		}
		else
		{
#ifdef DEBUG
			Serial.println("UI: Doorbell Mute");
#endif
			DoorbellMute = HIGH;
		}
	}

	if (event.id == uiSyear || event.id == uiTFyear)
	{
		setYear = event.value;
		SimbleeForMobile.updateValue(uiSyear, setYear);
		SimbleeForMobile.updateValue(uiTFyear, setYear);
	}
	if (event.id == uiSmonth || event.id == uiTFmonth)
	{
		setMonth = event.value;
		SimbleeForMobile.updateValue(uiSmonth, setMonth);
		SimbleeForMobile.updateValue(uiTFmonth, setMonth);
	}
	if (event.id == uiSday || event.id == uiTFday)
	{
		setDay = event.value;
		SimbleeForMobile.updateValue(uiSday, setDay);
		SimbleeForMobile.updateValue(uiTFday, setDay);
	}

	if (event.id == uiShour || event.id == uiTFhour)
	{
		setHour = event.value;
		SimbleeForMobile.updateValue(uiShour, setHour);
		SimbleeForMobile.updateValue(uiTFhour, setHour);
	}
	if (event.id == uiSmin || event.id == uiTFmin)
	{
		setMin = event.value;
		SimbleeForMobile.updateValue(uiSmin, setMin);
		SimbleeForMobile.updateValue(uiTFmin, setMin);
	}
	if (event.id == uiSsec || event.id == uiTFsec)
	{
		setSec = event.value;
		SimbleeForMobile.updateValue(uiSsec, setSec);
		SimbleeForMobile.updateValue(uiTFsec, setSec);
	}

	if (event.id == uiSetTime && event.type == EVENT_RELEASE)
	{
		Serial.print("Year: ");
		Serial.println(setYear);
		Serial.print("Year: ");
		Serial.println(setYear - 2000);
		setDS3231time(setSec, setMin, setHour, 1, setDay, setMonth, setYear - 2000);
	}
}


// ################################################################################
#define DS3231_I2C_ADDRESS 0x68
// Convert normal decimal numbers to binary coded decimal
byte decToBcd(byte val)
{
	return( (val/10*16) + (val%10) );
}

// Convert binary coded decimal to normal decimal numbers
byte bcdToDec(byte val)
{
	return( (val/16*10) + (val%16) );
}

void setDS3231time(byte rtc_second, byte rtc_minute, byte rtc_hour, byte rtc_dayOfWeek, byte rtc_day, byte rtc_month, byte rtc_year)
{
	// sets time and date data to DS3231
	Wire.beginTransmission(DS3231_I2C_ADDRESS);

	Wire.write(0); // set next input to start at the seconds register
	Wire.write(decToBcd(rtc_second)); // set seconds
	Wire.write(decToBcd(rtc_minute)); // set minutes
	Wire.write(decToBcd(rtc_hour)); // set hours
	Wire.write(decToBcd(rtc_dayOfWeek)); // set day of week (1=Sunday, 7=Saturday)
	Wire.write(decToBcd(rtc_day)); // set date (1 to 31)
	Wire.write(decToBcd(rtc_month)); // set month
	Wire.write(decToBcd(rtc_year)); // set year (0 to 99)

	Wire.endTransmission();
}


void readDS3231time(byte *rtc_second, byte *rtc_minute, byte *rtc_hour, byte *rtc_dayOfWeek, byte *rtc_day, byte *rtc_month, byte *rtc_year)
{
	Wire.beginTransmission(DS3231_I2C_ADDRESS);
	Wire.write(0); // set DS3231 register pointer to 00h
	Wire.endTransmission();
	Wire.requestFrom(DS3231_I2C_ADDRESS, 7);

	// request seven bytes of data from DS3231 starting from register 00h
	*rtc_second = bcdToDec(Wire.read() & 0x7f);
	*rtc_minute = bcdToDec(Wire.read());
	*rtc_hour = bcdToDec(Wire.read() & 0x3f);
	*rtc_dayOfWeek = bcdToDec(Wire.read());
	*rtc_day = bcdToDec(Wire.read());
	*rtc_month = bcdToDec(Wire.read());
	*rtc_year = bcdToDec(Wire.read());
}


time_t getTime()
{
	byte rtc_second, rtc_minute, rtc_hour, rtc_dayOfWeek, rtc_day, rtc_month, rtc_year;
	//char Temp[20];
	time_t DateTime;

	// retrieve data from DS3231
	readDS3231time(&rtc_second, &rtc_minute, &rtc_hour, &rtc_dayOfWeek, &rtc_day, &rtc_month, &rtc_year);

	tmElements_t tmet;
	tmet.Year = rtc_year + 30;
	tmet.Month = rtc_month;
	tmet.Day = rtc_day;
	tmet.Hour = rtc_hour;
	tmet.Minute = rtc_minute;
	tmet.Second = rtc_second;

	DateTime = makeTime(tmet);

	//sprintf(Temp, "DT: %.2d/%.2d/%.2d %.2d:%.2d:%.2d", year(DateTime), month(DateTime), day(DateTime), hour(DateTime), minute(DateTime), second(DateTime));
	//Serial.println(Temp);
	//sprintf(Temp, "RTC:%.2d/%.2d/%.2d %.2d:%.2d:%.2d", rtc_year, rtc_month, rtc_day, rtc_hour, rtc_minute, rtc_second);
	//Serial.println(Temp);

	return(DateTime);
}


void sprintTime(char strDateTime[], time_t DateTime)
{
	sprintf(strDateTime, "%.2d/%.2d/%.2d %.2d:%.2d:%.2d", year(DateTime), month(DateTime), day(DateTime), hour(DateTime), minute(DateTime), second(DateTime));
}


void printTime()
{
	char strDateTime[20];
	time_t DateTime;

	DateTime = getTime();

	sprintTime(strDateTime, DateTime);
	Serial.print(strDateTime);
}


void checkTime()
{
	char strDateTime[20];
	time_t DateTime;

	DateTime = getTime();

	if (year(DateTime) == 2000 && month(DateTime) == 1 && day(DateTime) == 1)
	{
		// DS3231 seconds, minutes, hours, day, date, month, year
		setDS3231time(00,30,12,4,1,6,16);
	}

#ifdef DEBUG
	printTime();
#endif
}


// ################################################################################

