#include <HCMAX7219.h>
#include "SPI.h"

#define icpPin                8        // timecode input pin
#define MAX7219_DIN           11       // MAX7219 DIN pin
#define MAX7219_CS            10       // MAX7219 CS pin 
#define MAX7219_CLK           12       // MAX7219 CLK pin

//enumerate registers of MAX7219 7-segment display
enum {  MAX7219_REG_DECODE    = 0x09,  
        MAX7219_REG_INTENSITY = 0x0A,
        MAX7219_REG_SCANLIMIT = 0x0B,
        MAX7219_REG_SHUTDOWN  = 0x0C,
        MAX7219_REG_DISPTEST  = 0x0F };

// enumerate the SHUTDOWN modes
// See MAX7219 Datasheet, Table 3, page 7
enum  { OFF = 0,  
        ON  = 1 };

const byte DP = 0b10000000;           // Define value of decimal point

// these values are setup for 25fps video
#define one_time_max          600     // Defines a time window of 400-600 for a LTC "1" 
#define one_time_min          400     
#define zero_time_max         1200    // Defines a time window of 800-1200 for a LTC "0"
#define zero_time_min         800     
                                  
#define end_data_position      63
#define end_sync_position      77
#define end_smpte_position     80

volatile unsigned int bit_time;      // Measured time bewtween rising edges of input waveform at ICP pin
volatile boolean valid_tc_word;     // Flags that valid tc word has been read
volatile boolean ones_bit_count;    // Two short pulses signifies a binary 1 in SMPTE. This variable keeps count (up to 1)
volatile boolean tc_sync;           // Flags that SMPTE sync word has been read
volatile boolean write_tc_out;      // Flags to main loop to print timecode to screen
volatile boolean drop_frame_flag;   // Incicates drop-frame tc

volatile byte total_bits;           // counts current position in SMPTE word
volatile byte current_bit;
volatile byte sync_count;           // Keeps count of consecutive "ones". 12 "ones" in a row signifies a SMPTE sync word

volatile byte tc[8];
volatile char timeCode[8];      // timecode value to be diplayed

/* ICR interrupt vector */
ISR(TIMER1_CAPT_vect)
{
 //toggleCaptureEdge
 TCCR1B ^= _BV(ICES1);          // interrupt called when rising edge detected on ICP pin. Timer contents written into ICR1

 bit_time = ICR1;               // writes contents of Input Compare Register to bit_time
 
 //resetTimer1 
 TCNT1 = 0;

 if ((bit_time < one_time_min) || (bit_time > zero_time_max)) // get rid of anything way outside the norm
 {
   total_bits = 0;              // sack everything off and start again
 }
 else
 {
    if (ones_bit_count == true) // i.e. we've counted one "ones" pulse already - don't count the second half again as a "one"
        ones_bit_count = false;
    else
    {    
      if (bit_time > zero_time_min)  // then we must have a "zero"
      {
        current_bit = 0;
        sync_count = 0;             // if we have a "zero" then we can't be in the sync word, so reset the sync counter
      }
      else if (bit_time < one_time_max) //then we have a "one"
      {
        ones_bit_count = true;
        current_bit = 1;
        sync_count++;
        if (sync_count == 12) // check to see if we just received 12 consecutive "ones" (i.e. the SMPTE sync word)
        {
          sync_count = 0;     // reset sync counter
          tc_sync = true;     // flag that we just received a sync word
          total_bits = end_sync_position;   // we must be at the end of a sync word (i,e. bit 77 in the SMPTE word)
        }
      }
      // By this point, we have stored either 1 or 0 into current_bit
      if (total_bits <= end_data_position) // Run the following if we are in the data portion of the SMPTE word (i,e not in the sync portion)
      {                                   
        tc[0] = tc[0] >> 1;
        for(int n=1;n<8;n++)
        {
          if(tc[n] & 1)
            tc[n-1] |= 0x80;
            tc[n] = tc[n] >> 1;
        }
        if(current_bit == 1)
           tc[7] |= 0x80;           // Set byte tc[7] to 11111111
      }
      total_bits++;
    }


    if (total_bits == end_smpte_position) // we have the 80th bit
    {
      total_bits = 0;
      if (tc_sync)          // we read the sync word sucessfully 
      {
        tc_sync = false;
        valid_tc_word = true; // flag that we have a valid SMPTE word stored in tc byte array
      }
    }
   
    if (valid_tc_word)
    {
      valid_tc_word = false;

      // Decode the 80 bits of the SMPTE word into hours, mins, secs, frames
      timeCode[7] = (tc[0]&0x0F)+0x30;      // frames
      timeCode[6] = (tc[1]&0x03)+0x30;      // 10's of frames
      timeCode[5] = (tc[2]&0x0F)+0x30;      // seconds
      timeCode[4] = (tc[3]&0x07)+0x30;      // 10's of seconds
      timeCode[3] = (tc[4]&0x0F)+0x30;      // minutes
      timeCode[2] = (tc[5]&0x07)+0x30;      // 10's of minutes
      timeCode[1] = (tc[6]&0x0F)+0x30;      // hours
      timeCode[0] = (tc[7]&0x03)+0x30;      // 10's of hours
           
      drop_frame_flag = bit_is_set(tc[1], 2);  // Look for drop-frame bit in SMPTE word and set flag if present

      write_tc_out = true;      // Flags for main loop to display timecode
    }
  }
}

// sets a value into one of the MAX7219 display's registers
void set_register(byte reg, byte value)
{
    digitalWrite(MAX7219_CS, LOW);
    shiftOut(MAX7219_DIN, MAX7219_CLK, MSBFIRST, reg);
    shiftOut(MAX7219_DIN, MAX7219_CLK, MSBFIRST, value);
    digitalWrite(MAX7219_CS, HIGH);
}

// resets the MAX7219 display
void resetDisplay()
{
  set_register(MAX7219_REG_SHUTDOWN, OFF);
  set_register(MAX7219_REG_DISPTEST, OFF);
  set_register(MAX7219_REG_INTENSITY, 0x0D);
}

// puts MAX7219 into test pattern mode
void testDisplay()
{
  set_register(MAX7219_REG_SHUTDOWN, OFF);
  set_register(MAX7219_REG_DISPTEST, ON);
  set_register(MAX7219_REG_SHUTDOWN, ON);
}

//displays an 8-character string, formatted with decimal points between value pairs
void displayTimecode(String timecode)
{
 set_register(MAX7219_REG_SHUTDOWN, OFF);
 set_register(MAX7219_REG_SCANLIMIT, 7);
 set_register(MAX7219_REG_DECODE, 0b11111111);

 set_register(1, timecode.charAt(7));
 set_register(2, timecode.charAt(6));
 set_register(3, timecode.charAt(5) | DP);
 set_register(4, timecode.charAt(4));
 set_register(5, timecode.charAt(3) | DP);
 set_register(6, timecode.charAt(2));
 set_register(7, timecode.charAt(1) | DP);
 set_register(8, timecode.charAt(0));

 set_register(MAX7219_REG_SHUTDOWN, ON);
}

void setup()
{
  Serial.begin(9600);
  pinMode(icpPin, INPUT);                  // ICP pin (digital pin 8 on arduino) as audio input

  pinMode(MAX7219_DIN, OUTPUT);
  pinMode(MAX7219_CS, OUTPUT);
  pinMode(MAX7219_CLK, OUTPUT);

  digitalWrite(MAX7219_CS, HIGH);

  bit_time = 0;
  valid_tc_word = false;
  ones_bit_count = false;
  tc_sync = false;
  write_tc_out = false;
  drop_frame_flag = false;
  total_bits =  0;
  current_bit =  0;
  sync_count =  0;

  resetDisplay();                         // Initialises 7-segment display
  testDisplay();                          // Flashes test pattern
  delay(1000);
  resetDisplay();
  delay (1000);
  
  TCCR1A = B00000000; // clear all
  TCCR1B = B11000010; // ICNC1 noise reduction + ICES1 start on rising edge + CS11 divide by 8
  TCCR1C = B00000000; // clear all
  TIMSK1 = B00100000; // ICIE1 enable the icp

  TCNT1 = 0; // clear timer1
  Serial.print("Finished Setup!");
}

void loop()
{
   if (write_tc_out)
   {
     delay(40);
     write_tc_out = false;
     if (drop_frame_flag)
     {
        displayTimecode(timeCode);    // displays timecode on 7-segment display    
        //delay(30);
//      call future method that lights DF LED
     }
     else {
        displayTimecode(timeCode);    // displays timecode on 7-segment display    
        Serial.println((char*)timeCode);
        //delay(30);
//      call future method that lights non-DF LED
     }


   }
}
