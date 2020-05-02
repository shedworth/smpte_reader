#include <LiquidCrystal.h>
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

#define icpPin 8        // ICP input pin on arduino
#define one_time_max          600 // these values are setup for NTSC video
#define one_time_min          400 // PAL would be around 1000 for 0 and 500 for 1
#define zero_time_max         1200 // 80bits times 29.97 frames per sec
#define zero_time_min         800 // equals 833 (divide by 8 clock pulses)
                                  
#define end_data_position      63
#define end_sync_position      77
#define end_smpte_position     80

volatile unsigned int bit_time;
volatile boolean valid_tc_word;
volatile boolean ones_bit_count;
volatile boolean tc_sync;
volatile boolean write_tc_out;
volatile boolean drop_frame_flag;

volatile byte total_bits;
volatile byte current_bit;
volatile byte sync_count;

volatile byte tc[8];
volatile char timeCode[11];

/* ICR interrupt vector */
ISR(TIMER1_CAPT_vect)
{
 //toggleCaptureEdge
 TCCR1B ^= _BV(ICES1);

 bit_time = ICR1;
 
 //resetTimer1 
 TCNT1 = 0;

 if ((bit_time < one_time_min) || (bit_time > zero_time_max)) // get rid of anything way outside the norm
 {
   //Serial.println(bit_time, DEC);
   total_bits = 0;
 }
 else
 {
   if (ones_bit_count == true) // only count the second ones pluse
     ones_bit_count = false;
   else
   {    
     if (bit_time > zero_time_min)
     {
       current_bit = 0;
       sync_count = 0;
     }
     else //if (bit_time < one_time_max)
     {
       ones_bit_count = true;
       current_bit = 1;
       sync_count++;
       if (sync_count == 12) // part of the last two bytes of a timecode word
       {
         sync_count = 0;
         tc_sync = true;
         total_bits = end_sync_position;
       }
     }

     if (total_bits <= end_data_position) // timecode runs least to most so we need
     {                                    // to shift things around
       tc[0] = tc[0] >> 1;
 
       for(int n=1;n<8;n++)
       {
         if(tc[n] & 1)
           tc[n-1] |= 0x80;
       
         tc[n] = tc[n] >> 1;
       }
 
       if(current_bit == 1)
         tc[7] |= 0x80;
     }
     total_bits++;
   }

   if (total_bits == end_smpte_position) // we have the 80th bit
   {
     total_bits = 0;
     if (tc_sync)
     {
       tc_sync = false;
       valid_tc_word = true;
     }
   }
   
   if (valid_tc_word)
   {
     valid_tc_word = false;

     timeCode[10] = (tc[0]&0x0F)+0x30;      // frames
     timeCode[9] = (tc[1]&0x03)+0x30;      // 10's of frames
     timeCode[8] =  '.';
     timeCode[7] = (tc[2]&0x0F)+0x30;      // seconds
     timeCode[6] = (tc[3]&0x07)+0x30;      // 10's of seconds
     timeCode[5] =  ':';
     timeCode[4] = (tc[4]&0x0F)+0x30;      // minutes
     timeCode[3] = (tc[5]&0x07)+0x30;      // 10's of minutes
     timeCode[2] = ':';
     timeCode[1] = (tc[6]&0x0F)+0x30;      // hours
     timeCode[0] = (tc[7]&0x03)+0x30;      // 10's of hours
     
     drop_frame_flag = bit_is_set(tc[1], 2);

     write_tc_out = true;
   }
 }
}


void setup()
{
 Serial.begin(115200);
 lcd.begin(16, 2);
 pinMode(icpPin, INPUT);                  // ICP pin (digital pin 8 on arduino) as input

 bit_time = 0;
 valid_tc_word = false;
 ones_bit_count = false;
 tc_sync = false;
 write_tc_out = false;
 drop_frame_flag = false;
 total_bits =  0;
 current_bit =  0;
 sync_count =  0;

 lcd.print("FINISHED SETUP");
 Serial.println("Finished setup ");
 delay (1000);
 lcd.clear();

 lcd.setCursor(0, 0);
 lcd.print("                ");
 lcd.setCursor(0, 1);
 lcd.print("WAITING FOR TC");

 TCCR1A = B00000000; // clear all
 TCCR1B = B11000010; // ICNC1 noise reduction + ICES1 start on rising edge + CS11 divide by 8
 TCCR1C = B00000000; // clear all
 TIMSK1 = B00100000; // ICIE1 enable the icp

 TCNT1 = 0; // clear timer1
}

void loop()
{
   if (write_tc_out)
   {
     write_tc_out = false;
     if (drop_frame_flag)
     {
       lcd.setCursor(0, 0);
       lcd.print("TC-DROP FRAME");
       Serial.print("TC-[df] ");
     }
     else {
       lcd.setCursor(0, 0);
       lcd.print("TC-NO DROP FRAME");
       Serial.print("TC-[nd] ");
     }
     lcd.setCursor(0, 1);
     lcd.print((char*)timeCode);
     Serial.print((char*)timeCode);
     lcd.print("\r");
     Serial.println("\r");
     lcd.setCursor(11, 1);
     lcd.print("......");
     
     delay(30);
   }
}
