/*
Nintendo Switch Fightstick - Proof-of-Concept

Based on the LUFA library's Low-Level Joystick Demo
	(C) Dean Camera
Based on the HORI's Pokken Tournament Pro Pad design
	(C) HORI

This project implements a modified version of HORI's Pokken Tournament Pro Pad
USB descriptors to allow for the creation of custom controllers for the
Nintendo Switch. This also works to a limited degree on the PS3.

Since System Update v3.0.0, the Nintendo Switch recognizes the Pokken
Tournament Pro Pad as a Pro Controller. Physical design limitations prevent
the Pokken Controller from functioning at the same level as the Pro
Controller. However, by default most of the descriptors are there, with the
exception of Home and Capture. Descriptor modification allows us to unlock
these buttons for our use.
*/

/** \file
 *
 *  Main source file for the Joystick demo. This file contains the main tasks of the demo and
 *  is responsible for the initial application hardware configuration.
 */

#include "Joystick.h"
#include <avr/io.h>
#include <util/delay.h>

#include <string.h>

# define USART_BAUDRATE 9600
# define BAUD_PRESCALE ((( F_CPU / ( USART_BAUDRATE * 16UL))) - 1)

#define BUF_SIZE 256

/*
The following ButtonMap variable defines all possible buttons within the
original 13 bits of space, along with attempting to investigate the remaining
3 bits that are 'unused'. This is what led to finding that the 'Capture'
button was operational on the stick.
*/
uint16_t ButtonMap[16] = {
	0x01,
	0x02,
	0x04,
	0x08,
	0x10,
	0x20,
	0x40,
	0x80,
	0x100,
	0x200,
	0x400,
	0x800,
	0x1000,
	0x2000,
	0x4000,
	0x8000,
};

//simple round buffer
char buffer[BUF_SIZE]; // TODO: test buffer is enough
uint8_t buf_start_position = 0; 
uint8_t buf_end_position = 0;


uint8_t stick[2][2];
uint8_t hat;
uint16_t buf_button;

void uart_transmit( unsigned char data )
{
    // wait for empty transmit buffer
    while ( ! ( UCSR1A & ( 1 << UDRE1 ) ) )
        ;
    
    // put data into buffer, sends data
    UDR1 = data;
}

// read a char from uart
unsigned char uart_receive(void)
{
    while (!( UCSR1A & ( 1 << RXC1) ))
        ;
    
    return UDR1;
}

void reset_button() {
	stick[0][0] = STICK_CENTER;
	stick[0][1] = STICK_CENTER;
	stick[1][0] = STICK_CENTER;
	stick[1][1] = STICK_CENTER;
	hat = 0x08;
	buf_button = 0;
}

// init uart
void uart_init(void)
{
    // set baud rate   
    unsigned int baud = BAUD_PRESCALE;
    
    UBRR1H = (unsigned char) (baud >> 8 );
    UBRR1L = (unsigned char)baud;
    
    // enable received and transmitter
    UCSR1B = ( 1 << RXEN1 ) | ( 1 << TXEN1 );
    
    // set frame format ( 8data, 2stop )
    UCSR1C = ( 1 << USBS1 ) | ( 3 << UCSZ10 );
}

// check if there are any chars to be read
int uart_dataAvailable(void)
{
    if ( UCSR1A & ( 1 << RXC1) )
        return 1;
    
    return 0;
}

// write a string to the uart
void uart_print( char data[] )
{
    int c = 0;
    
    for ( c = 0; c < strlen(data); c++ )
        uart_transmit(data[c]);
}

void buf_UART() {
	while(uart_dataAvailable()) {
		buffer[buf_end_position] = uart_receive();
		buf_end_position = (buf_end_position + 1) % BUF_SIZE;
	}
}


// Main entry point.
int main(void) {
	reset_button();
	uart_init();
	uart_print( "\n\rReady :)\n\r" );
	// We'll start by performing hardware and peripheral setup.
	SetupHardware();
	// We'll then enable global interrupts for our use.
	GlobalInterruptEnable();
	// Once that's done, we'll enter an infinite loop.
	for (;;)
	{
		// We need to run our task to process and deliver data for our IN and OUT endpoints.
		HID_Task();
		// We also need to run the main USB management task.
		USB_USBTask();
		buf_UART();
	}
}

// Configures hardware and peripherals, such as the USB peripherals.
void SetupHardware(void) {
	// We need to disable watchdog if enabled by bootloader/fuses.
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	// We need to disable clock division before initializing the USB hardware.
	clock_prescale_set(clock_div_1);
	// We can then initialize our hardware and peripherals, including the USB stack.
	// The USB stack should be initialized last.
	USB_Init();
}

// Fired to indicate that the device is enumerating.
void EVENT_USB_Device_Connect(void) {
	// We can indicate that we're enumerating here (via status LEDs, sound, etc.).
}

// Fired to indicate that the device is no longer connected to a host.
void EVENT_USB_Device_Disconnect(void) {
	// We can indicate that our device is not ready (via status LEDs, sound, etc.).
}

// Fired when the host set the current configuration of the USB device after enumeration.
void EVENT_USB_Device_ConfigurationChanged(void) {
	bool ConfigSuccess = true;

	// We setup the HID report endpoints.
	ConfigSuccess &= Endpoint_ConfigureEndpoint(JOYSTICK_OUT_EPADDR, EP_TYPE_INTERRUPT, JOYSTICK_EPSIZE, 1);
	ConfigSuccess &= Endpoint_ConfigureEndpoint(JOYSTICK_IN_EPADDR, EP_TYPE_INTERRUPT, JOYSTICK_EPSIZE, 1);

	// We can read ConfigSuccess to indicate a success or failure at this point.
}

// Process control requests sent to the device from the USB host.
void EVENT_USB_Device_ControlRequest(void) {
	// We can handle two control requests: a GetReport and a SetReport.
	switch (USB_ControlRequest.bRequest)
	{
		// GetReport is a request for data from the device.
		case HID_REQ_GetReport:
			if (USB_ControlRequest.bmRequestType == (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				// We'll create an empty report.
				USB_JoystickReport_Input_t JoystickInputData;
				// We'll then populate this report with what we want to send to the host.
				GetNextReport(&JoystickInputData);
				// Since this is a control endpoint, we need to clear up the SETUP packet on this endpoint.
				Endpoint_ClearSETUP();
				// Once populated, we can output this data to the host. We do this by first writing the data to the control stream.
				Endpoint_Write_Control_Stream_LE(&JoystickInputData, sizeof(JoystickInputData));
				// We then acknowledge an OUT packet on this endpoint.
				Endpoint_ClearOUT();
			}

			break;
		case HID_REQ_SetReport:
			if (USB_ControlRequest.bmRequestType == (REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				// We'll create a place to store our data received from the host.
				USB_JoystickReport_Output_t JoystickOutputData;
				// Since this is a control endpoint, we need to clear up the SETUP packet on this endpoint.
				Endpoint_ClearSETUP();
				// With our report available, we read data from the control stream.
				Endpoint_Read_Control_Stream_LE(&JoystickOutputData, sizeof(JoystickOutputData));
				// We then send an IN packet on this endpoint.
				Endpoint_ClearIN();
			}

			break;
	}
}

// Process and deliver data from IN and OUT endpoints.
void HID_Task(void) {
	// If the device isn't connected and properly configured, we can't do anything here.
	if (USB_DeviceState != DEVICE_STATE_Configured)
	  return;

	// We'll start with the OUT endpoint.
	Endpoint_SelectEndpoint(JOYSTICK_OUT_EPADDR);
	// We'll check to see if we received something on the OUT endpoint.
	if (Endpoint_IsOUTReceived())
	{
		// If we did, and the packet has data, we'll react to it.
		if (Endpoint_IsReadWriteAllowed())
		{
			// We'll create a place to store our data received from the host.
			USB_JoystickReport_Output_t JoystickOutputData;
			// We'll then take in that data, setting it up in our storage.
			Endpoint_Read_Stream_LE(&JoystickOutputData, sizeof(JoystickOutputData), NULL);
			// At this point, we can react to this data.
			// However, since we're not doing anything with this data, we abandon it.
		}
		// Regardless of whether we reacted to the data, we acknowledge an OUT packet on this endpoint.
		Endpoint_ClearOUT();
	}

	// We'll then move on to the IN endpoint.
	Endpoint_SelectEndpoint(JOYSTICK_IN_EPADDR);
	// We first check to see if the host is ready to accept data.
	if (Endpoint_IsINReady())
	{
		// We'll create an empty report.
		USB_JoystickReport_Input_t JoystickInputData;
		// We'll then populate this report with what we want to send to the host.
		GetNextReport(&JoystickInputData);
		// Once populated, we can output this data to the host. We do this by first writing the data to the control stream.
		Endpoint_Write_Stream_LE(&JoystickInputData, sizeof(JoystickInputData), NULL);
		// We then send an IN packet on this endpoint.
		Endpoint_ClearIN();

		/* Clear the report data afterwards */
		// memset(&JoystickInputData, 0, sizeof(JoystickInputData));
	}
}

// Prepare the next report for the host.
void GetNextReport(USB_JoystickReport_Input_t* const ReportData) {
	// All of this code here is handled -really poorly-, and should be replaced with something a bit more production-worthy.
	unsigned char receivedChar;

	/* Clear the report contents */
	memset(ReportData, 0, sizeof(USB_JoystickReport_Input_t));

	while(buf_start_position != buf_end_position) {
		receivedChar = buffer[buf_start_position];
		uart_transmit(receivedChar);
		buf_start_position = (buf_start_position + 1) % BUF_SIZE;
		uint8_t type = (receivedChar & 0b11100000) >> 5;
		if(type == 1) {
			uart_transmit('A');
			uint8_t btn = receivedChar & 0b00001111;
			uint16_t press = (receivedChar & 0b00010000) >> 4 << btn;
			uint16_t mask = buf_button = !(0b1 << btn);
			buf_button = buf_button & mask | press;
		} else if(type == 2) {
			uart_transmit('H');
			hat = (receivedChar & 0b00011111);
		} else if(type & 0b100) {
			uint8_t lr = (type & 0b10) >> 1;
			uint8_t xy = type & 0b1;
			stick[lr][xy] = (receivedChar & 0b00011111) << 3;
			if(stick[lr][xy] > 0b10000000) {
				stick[lr][xy] = stick[lr][xy] + 7;
			}
			/*
			if(receivedChar & 0b00010000) {
				stick[lr][xy] = stick[lr][xy] + (receivedChar & 0b00001111) >> 1;
			}
			*/
			uart_transmit('T');
		}
	}
	/*
			switch(receivedChar) {
				case 'a':
					buf_button = 0x04;
					break;
				case 'b':
					buf_button = 0x02;
					break;
				case 'y':
					buf_button = 0x01;
					break;
				case 'x':
					buf_button = 0x08;
					break;
			}
			case 'k':
				ReportData->LY = 0;
				break;
			case 'j':
				ReportData->LY = 255;
				break;
			case 'h':
				ReportData->LX = 0;
				break;
			case 'l':
				ReportData->LX = 255;
				break;
			case '8':
				ReportData->HAT = 0x00;
				break;
			case '7':
				ReportData->HAT = 0x02;
				break;
			case '6':
				ReportData->HAT = 0x04;
				break;
			case '5': // Top
				ReportData->HAT = 0x06;
				break;
	*/

	ReportData->LX = stick[0][0];
	ReportData->LY = stick[0][1];
	ReportData->RX = stick[1][0];
	ReportData->RY = stick[1][1];
	ReportData->HAT = hat;

	for (int i = 0; i < 16; i++) {
		if (buf_button & (1 << i))
			ReportData->Button |= ButtonMap[i];
	}
}
