// SMPTE Timecode Reader
// Uses INT0 (Pin 2)
/*
  PAL = 25FPS = 40ms per frame
  40ms / 80bits = 500us per bit
  250us per half bit
  125us per quarter bit
  0 bit max = 625us
  1 bit max = 375us (also 0 bit min)
  1 bit min = 125us

  NTSC = 30FPS (29.97) = 33.333ms per frame
  40ms / 80bits = 417us per bit
  208us per half bit
  104us per quarter bit
  0 bit max = 521us
  1 bit max = 312us (also 0 bit min)
  1 bit min = 104us
*/

#define uMax0 625 // Any time greater than this is to big
#define uMax1 375 // Midpoint timing between a 1 and 0 bit length
#define uMin1 125 // Any time less than this is to short

const word sync = 0xBFFC; // Sync word to expect when running tape forward

enum flagBits {
  tcValid,        // TC copied to xtc is valid (Used by main loop to determing if a timecode has arrived)
  tcFrameError,   // ISR edge out of timing bounds (only gets reset after next valid TC read)
  tcOverrun,      // TC was not read from xtc by main loop before next value was ready (so main loop can tell if timecodes have been lost)
  tcForceUpdate,  // Valid TC will always be copied to buffer even if last value not read (if set by main code then TC will always be copied)
  tcHalfOne       // ISR is reading a 1 bit so ignore next edge (Internal to ISR)
};

uint8_t tc[10] = {0};                                     // ISR Buffer to store incoming bits
volatile uint8_t xtc[8] = {0};                            // Buffer to store valid TC data - sync bytes
volatile uint8_t tcFlags = 0;                             // Various flags used by ISR and main code
uint32_t uSeconds;                                        // ISR store of last edge change time

char timeCode[12];                                        // For example code another buffer to write decoded timecode
char userBits[12];                                        // For example code another buffer to write decoded user bits


void setup(){
  Serial.begin(9600);
  pinMode(2,INPUT);
  attachInterrupt(0, int0ISR, CHANGE);
  Serial.println(F("Waiting For TC"));
}

void loop(){
  while(bitRead(tcFlags, tcValid) == 0){};                // Wait for valid timecode
  
  timeCode[0] = (xtc[0] & 0x03) + '0';                    // 10's of hours
  timeCode[1] = (xtc[1] & 0x0F) + '0';                    // hours
  timeCode[2] = ':';                
  timeCode[3] = (xtc[2] & 0x07) + '0';                    // 10's of minutes
  timeCode[4] = (xtc[3] & 0x0F) + '0';                    // minutes
  timeCode[5] =  ':';               
  timeCode[6] = (xtc[4] & 0x07) + '0';                    // 10's of seconds
  timeCode[7] = (xtc[5] & 0x0F) + '0';                    // seconds
  timeCode[8] =  '.';               
  timeCode[9] = (xtc[6] & 0x03) + '0';                    // 10's of frames
  timeCode[10] = (xtc[7] & 0x0F) + '0';                   // frames
  
  userBits[0] = ((xtc[7] & 0xF0) >> 4) + '0';             // user bits 1 
  userBits[1] = ((xtc[6] & 0xF0) >> 4) + '0';             // user bits 2  
  userBits[2] = '-';            
  userBits[3] = ((xtc[5] & 0xF0) >> 4) + '0';             // user bits 3
  userBits[4] = ((xtc[4] & 0xF0) >> 4) + '0';             // user bits 4
  userBits[5] = '-';            
  userBits[6] = ((xtc[3] & 0xF0) >> 4) + '0';             // user bits 5
  userBits[7] = ((xtc[2] & 0xF0) >> 4) + '0';             // user bits 6
  userBits[8] = '-';            
  userBits[9] = ((xtc[1] & 0xF0) >> 4) + '0';             // user bits 7
  userBits[10] = ((xtc[0] & 0xF0) >> 4) + '0';            // user bits 8
  
  bitClear(tcFlags, tcValid);                             // Finished with TC so signal to ISR it can overwrite it with next TC
  
  Serial.print(timeCode);
  Serial.print("\t");
  Serial.println(userBits);
}

void int0ISR(){
  uint32_t edgeTimeDiff = micros() - uSeconds;            // Get time difference between this and last edge
  uSeconds = micros();                                    // Store time of this edge
  
  if ((edgeTimeDiff < uMin1) or (edgeTimeDiff > uMax0)) { // Drop out now if edge time not withing bounds
    bitSet(tcFlags, tcFrameError);
    return;
  }
  
  if (edgeTimeDiff > uMax1)                               // A zero bit arrived
  {
    if (bitRead(tcFlags, tcHalfOne) == 1){                // But we are expecting a 1 edge
      bitClear(tcFlags, tcHalfOne);
      clearBuffer(tc, sizeof(tc));
      return;
    }
    // 0 bit
    shiftRight(tc, sizeof(tc));                           // Rotate buffer right
    // Shift replaces top bit with zero so nothing else to do
    //bitClear(tc[0], 7);                                   // Reset the 1 bit in the buffer
  }
  else                                                    // Not zero so must be a 1 bit
  { // 1 bit
    if (bitRead(tcFlags, tcHalfOne) == 0){                // First edge of a 1 bit
      bitSet(tcFlags, tcHalfOne);                         // Flag we have the first half
      return;
    }
    // Second edge of a 1 bit
    bitClear(tcFlags, tcHalfOne);                         // Clear half 1 flag
    shiftRight(tc, sizeof(tc));                           // Rotate buffer right
    bitSet(tc[0], 7);                                     // Set the 1 bit in the buffer
  }
  // Congratulations, we have managed to read a valid 0 or 1 bit into buffer
  if (word(tc[0], tc[1]) == sync){                        // Last 2 bytes read = sync?
    bitClear(tcFlags, tcFrameError);                      // Clear framing error
    bitClear(tcFlags, tcOverrun);                         // Clear overrun error
    if (bitRead(tcFlags, tcForceUpdate) == 1){
      bitClear(tcFlags, tcValid);                         // Signal last TC read
    }
    if (bitRead(tcFlags, tcValid) == 1){                  // Last TC not read
      bitSet(tcFlags, tcOverrun);                         // Flag overrun error
      return;                                             // Do nothing else
    }
    for (uint8_t x = 0; x < sizeof(xtc); x++){            // Copy buffer without sync word
      xtc[x] = tc[x + 2];
    }
    bitSet(tcFlags, tcValid);                             // Signal valid TC
  }
}

void clearBuffer(uint8_t theArray[], uint8_t theArraySize){
  for (uint8_t x = 0; x < theArraySize - 1; x++){
    theArray[x] = 0;
  }
}

void shiftRight(uint8_t theArray[], uint8_t theArraySize){
  uint8_t x;
  for (x = theArraySize; x > 0; x--){
    uint8_t xBit = bitRead(theArray[x - 1], 0);
    theArray[x] = theArray[x] >> 1;
    theArray[x] = theArray[x] | (xBit << 7);
  }
  theArray[x] = theArray[x] >> 1;
}
