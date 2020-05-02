   1 /*
   2    LTC/SMPTE parser - ArduinoAudio
   3    
   4    Copyright (C) 2008 Robin Gareus <robin@gareus.org>
   5     
   6    This program is free software; you can redistribute it and/or modify
   7    it under the terms of the GNU General Public License as published by
   8    the Free Software Foundation; either version 2, or (at your option)
   9    any later version.
  10 
  11    This program is distributed in the hope that it will be useful,
  12    but WITHOUT ANY WARRANTY; without even the implied warranty of
  13    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  14    GNU General Public License for more details.
  15 
  16    You should have received a copy of the GNU General Public License
  17    along with this program; if not, write to the Free Software Foundation,
  18    Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
  19 
  20 */
  21 
  22 #include <FrequencyTimer2.h>
  23 
  24 //int fps =25;
  25 //int sampleRate = 6000; // arduino-period of 333
  26 
  27 volatile int bufperiod=305;  // current sampling freq. ~ 2000000 / fps / 80 / (soundToBiphaseLimit+1)
  28 int dotstate=0;  // bit0: LTC-decoded ,  bit1: bufffer-overrun
  29 int blink=0; 
  30 int mode=0;
  31 #define DEBUG
  32 #ifdef DEBUG
  33 const int mode_max = 11;
  34 #else
  35 const int mode_max = 4;
  36 #endif
  37 boolean have_ltc = false;
  38 int  quality = 0;
  39 
  40 ///////////////////////////////
  41 
  42 // Little Endian version
  43 typedef struct SMPTEFrame {
  44   unsigned int frameUnits:4;
  45   unsigned int user1:4;
  46 
  47   unsigned int frameTens:2;
  48   unsigned int dfbit:1;
  49   unsigned int colFrm:1;
  50   unsigned int user2:4;
  51 
  52   unsigned int secsUnits:4;
  53   unsigned int user3:4;
  54 
  55   unsigned int secsTens:3;
  56   unsigned int biphaseMarkPhaseCorrection:1;
  57   unsigned int user4:4;
  58 
  59   unsigned int minsUnits:4;
  60   unsigned int user5:4;
  61 
  62   unsigned int minsTens:3;
  63   unsigned int binaryGroupFlagBit1:1;
  64   unsigned int user6:4;
  65 
  66   unsigned int hoursUnits:4;
  67   unsigned int user7:4;
  68 
  69   unsigned int hoursTens:2;
  70   unsigned int reserved:1;
  71   unsigned int binaryGroupFlagBit2:1;
  72   unsigned int user8:4;
  73 
  74   unsigned int syncWord:16;
  75 } SMPTEFrame;
  76 
  77 
  78 struct SMPTEDecoder {
  79   boolean biphaseToBinaryState;
  80   boolean biphaseToBinaryPrev;
  81   boolean soundToBiphaseState;
  82   int soundToBiphaseCnt;
  83 //int soundToBiphasePeriod;
  84   int soundResyncCnt;
  85   int soundToBiphaseLimit;
  86   unsigned char soundToBiphaseMin;
  87   unsigned char soundToBiphaseMax;
  88   unsigned long decodeSyncWord;
  89   SMPTEFrame decodeFrame;
  90   int decodeBitCnt;
  91 } SMPTEDecoder ;
  92 
  93 
  94 int audio_to_biphase(struct SMPTEDecoder *d, unsigned char *sound, boolean *biphase, int size) {
  95   int i;
  96   int j = 0;
  97   int max_threshold, min_threshold;
  98 
  99   for (i = 0 ; i < size ; i++) {
 100     /* track minimum and maximum values */
 101     d->soundToBiphaseMin = 128 - (((128 - d->soundToBiphaseMin) * 15) >> 4);
 102     d->soundToBiphaseMax = 128 + (((d->soundToBiphaseMax - 128) * 15) >> 4);
 103 
 104     if (sound[i]<d->soundToBiphaseMin) d->soundToBiphaseMin = sound[i];
 105     if (sound[i]>d->soundToBiphaseMax) d->soundToBiphaseMax = sound[i];
 106 
 107     /* set the tresholds for hi/lo state tracking */
 108     min_threshold = 128 - (((128 - d->soundToBiphaseMin) * 8) >> 4);
 109     max_threshold = 128 + (((d->soundToBiphaseMax - 128) * 8) >> 4);
 110 
 111     if ((d->soundToBiphaseState && sound[i] > max_threshold) ||
 112         (!d->soundToBiphaseState && sound[i] < min_threshold)) {
 113       if (d->soundToBiphaseCnt > d->soundToBiphaseLimit) {                   
 114         biphase[j++] = d->soundToBiphaseState;
 115         biphase[j++] = d->soundToBiphaseState;
 116       } else {
 117         biphase[j++] = d->soundToBiphaseState;
 118         d->soundToBiphaseCnt<<=1;  // *=2;
 119       }
 120 
 121 #if 0  // resync - by count
 122       /* track speed variations */
 123       d->soundToBiphasePeriod = (d->soundToBiphasePeriod*3 + d->soundToBiphaseCnt)>>2;
 124       /* This limit specifies when a state-change is
 125        * considered bihapse-clock or 2*biphase-clock.
 126        * The relation with period has been determined
 127        * with trial-and-error */
 128       d->soundToBiphaseLimit = (d->soundToBiphasePeriod * 14)>>4;
 129 #endif
 130 
 131 #if 1 // resync by freq
 132 // TODO: tune to maximize quality ..
 133 #define LORAIL 292
 134 #define HIRAIL 370
 135       if (quality>0 && d->soundResyncCnt>0) d->soundResyncCnt--;
 136       else if ((dotstate&1)==0 && quality < 12){
 137         if (d->soundToBiphaseCnt > d->soundToBiphaseLimit+1) {
 138           if(bufperiod <  HIRAIL) {
 139             bufperiod++; d->soundResyncCnt=(quality>7)?6000:(quality>3)?450:30;
 140             if (bufperiod==HIRAIL) bufperiod=LORAIL+1;
 141           }
 142         } else {
 143           if(bufperiod > LORAIL) {
 144             bufperiod--; d->soundResyncCnt=(quality>7)?16000:(quality>3)?1200:80;
 145             if (bufperiod==LORAIL) bufperiod=HIRAIL-1;
 146           }
 147         }
 148        }
 149        if (mode==2) blink=d->soundResyncCnt>0?7:0;
 150 #endif
 151 
 152       d->soundToBiphaseCnt = 0;
 153       d->soundToBiphaseState = !d->soundToBiphaseState;
 154     }
 155     d->soundToBiphaseCnt++;
 156   }
 157   return j;
 158 }
 159 
 160 int biphase_decode(struct SMPTEDecoder *d, boolean *biphase, boolean *bits, int size) {
 161   int i;
 162   int j = 0;
 163   for (i = 0 ; i < size ; i++) {
 164     if (biphase[i] == d->biphaseToBinaryPrev) {
 165       d->biphaseToBinaryState = true;
 166       bits[j++] = false;
 167     } else {
 168       d->biphaseToBinaryState = !d->biphaseToBinaryState;
 169       if (d->biphaseToBinaryState)
 170         bits[j++] = true;
 171     }
 172     d->biphaseToBinaryPrev = biphase[i];
 173   }
 174   return j;
 175 }
 176 
 177 int ltc_decode(struct SMPTEDecoder *d, boolean *bits, int size) {
 178   int i;
 179   int bitNum, bitSet, bytePos;
 180 
 181   for (i = 0 ; i < size ; i++) {
 182     if (d->decodeBitCnt==0)
 183       memset(&d->decodeFrame,0,sizeof(SMPTEFrame));
 184 
 185     d->decodeSyncWord<<=1;
 186     if (bits[i]) {
 187       d->decodeSyncWord|=1;
 188 
 189       if (d->decodeBitCnt<80)
 190         ((unsigned char*)&d->decodeFrame)[d->decodeBitCnt>>3]|=
 191           1<<(d->decodeBitCnt&7);
 192     }
 193 
 194     if (d->decodeBitCnt>80) { dotstate&=~1; /* have_ltc=false; */ }
 195 
 196     d->decodeBitCnt++;
 197     if ((d->decodeSyncWord&0xffff)==0x3ffd) {
 198       if (d->decodeBitCnt == 80) {             
 199         display_buffer(d);
 200       }
 201       d->decodeBitCnt = 0;
 202     }
 203   }
 204   return 1;
 205 }
 206 
 207 ///////////////////////////////
 208 
 209 struct SMPTEDecoder ltcdec;
 210 int analogPin = 0;
 211 #define BUFSIZ 64
 212 
 213 int minv = BUFSIZ;
 214 volatile long burpCount = 0;
 215 volatile unsigned char audiobuf0[BUFSIZ];
 216 volatile unsigned char audiobuf1[BUFSIZ];
 217 volatile int audioptr = 0;
 218 int buf0cnt =0;
 219 int buf1cnt =0;
 220 volatile boolean bufready = false;
 221 int lastperiod = bufperiod;
 222 volatile boolean buf_low = false;
 223 
 224 void display_buffer(struct SMPTEDecoder *d) {
 225   dotstate|=1; 
 226   have_ltc=true;
 227   
 228   int hr=d->decodeFrame.hoursTens*10 + d->decodeFrame.hoursUnits;
 229   int mn=d->decodeFrame.minsTens*10 + d->decodeFrame.minsUnits;
 230   int sc=d->decodeFrame.secsTens*10 + d->decodeFrame.secsUnits;
 231   int fr=d->decodeFrame.frameTens*10 + d->decodeFrame.frameUnits;
 232   
 233 #if 0
 234   int frame =
 235               fps *   (60*60*(
 236                               d->decodeFrame.hoursTens*10 +
 237                               d->decodeFrame.hoursUnits) +
 238                               60*
 239                               (d->decodeFrame.minsTens*10 +
 240                                d->decodeFrame.minsUnits) +
 241 
 242                               (d->decodeFrame.secsTens*10 +
 243                               d->decodeFrame.secsUnits)
 244                       ) +
 245               d->decodeFrame.frameTens*10 +
 246               d->decodeFrame.frameUnits;       
 247 #endif
 248   if (mode==0) {
 249     format_time(mn,sc,fr);
 250   } else if (mode == 1) {
 251     format_time(hr,mn,sc);
 252     blink=3; // 3<<4;
 253   } 
 254 }
 255 
 256 int process_audio_buffer(unsigned char *buf, int size) {
 257   struct SMPTEDecoder *d = &ltcdec;
 258   boolean code[BUFSIZ]; 
 259   boolean *bits=  buf; //re-use snd buffer
 260   size = audio_to_biphase(d, buf, code, size);
 261 #ifdef DEBUG
 262  if (mode==10)     { format_hex((buf[2]<<16)|(buf[1]<<8)|buf[0]); }
 263  else if (mode==7) { format_value(size); format_char(1,'S'); }
 264 #endif
 265   size = biphase_decode(d, code, bits, size);
 266   
 267 #ifdef DEBUG  // DEBUG 
 268   #if 1
 269   static int bimax = 0x00;
 270   static int bimin = 0xff;
 271   static unsigned long prev_ts6 = 0;
 272   unsigned long now = millis();
 273   if (now < prev_ts6 || now >= (prev_ts6 + 1000)) {
 274     bimax=0;bimin=0xff;
 275     prev_ts6=now;
 276   }
 277   bimax=max(d->soundToBiphaseMax,bimax);
 278   bimin=min(d->soundToBiphaseMin,bimin);
 279   if (mode==5)      { format_value (bimax); format_char(1,'u'); }
 280   else if (mode==6) { format_value (bimin);format_char(1,'N'); }
 281   #else
 282   if (mode==5)      { format_value (d->soundToBiphaseMax); format_char(1,'u'); }
 283   else if (mode==6) { format_value (d->soundToBiphaseMin);format_char(1,'N'); }
 284   #endif
 285   else if (mode==8) { format_value(size); format_char(1,'s'); }
 286   else if (mode==9) { format_value(d->decodeBitCnt);format_char(1,'C'); }
 287 #endif
 288   return ltc_decode(d, bits, size);
 289 }
 290 
 291 /* triggered by timer IRQ - sample audio */
 292 void Burpme(void) {  
 293   burpCount++;
 294   int snd = analogRead(analogPin) >> 2;
 295   //if (snd<70)snd=0; else snd=snd-70;
 296   
 297   if (audioptr==0) {
 298     audiobuf0[buf0cnt]= (unsigned char)snd;
 299     if (++buf0cnt>=BUFSIZ) buf0cnt=0;
 300     if (buf0cnt + 4 > BUFSIZ) buf_low=true; else buf_low=false;
 301 
 302     if (buf0cnt==0 && !bufready) { audioptr=1; bufready=true; 
 303       if (bufperiod!=lastperiod) { // resync 
 304         FrequencyTimer2::setPeriod(bufperiod);
 305         lastperiod=bufperiod;
 306       }
 307     } else if (buf0cnt==0 && bufready) { dotstate|=2;}
 308 
 309   } else {
 310     audiobuf1[buf1cnt]= (unsigned char)snd;
 311     if (++buf1cnt>=BUFSIZ) buf1cnt=0;
 312     if (buf1cnt + 4 > BUFSIZ) buf_low=true; else buf_low=false;
 313     
 314     if (buf1cnt==0 && !bufready) { audioptr=0; bufready=true;}
 315     else if (buf1cnt==0 && bufready) { dotstate|=2;}
 316   }
 317 }
 318 
 319 void setup () {
 320   taster_setup();
 321   display_setup();
 322   memset(&ltcdec,0,sizeof(struct SMPTEDecoder));
 323   ltcdec.biphaseToBinaryState = true;
 324 //ltcdec.soundToBiphasePeriod = sampleRate / fps / 80;
 325   ltcdec.soundToBiphaseLimit = 2; // (ltcdec.soundToBiphasePeriod * 14)>>4;
 326   FrequencyTimer2::setPeriod(bufperiod); // 250 @ 8kHz 
 327   FrequencyTimer2::enable();
 328   FrequencyTimer2::setOnOverflow(Burpme);
 329 }
 330 
 331 void loop () {
 332  if (bufready) {
 333    if (audioptr==1) {
 334      process_audio_buffer((unsigned char*)audiobuf0, BUFSIZ);
 335    }else{
 336      process_audio_buffer((unsigned char*)audiobuf1, BUFSIZ);
 337    }
 338    
 339    noInterrupts();
 340    minv = min(minv,((BUFSIZ<<1) - burpCount));
 341    burpCount=0;
 342    bufready=false;
 343    interrupts();   
 344    
 345    if (!buf_low) {
 346      if (mode==2 && audioptr==1) {
 347        format_value(bufperiod + 1000*quality);
 348        dotstate|=4;
 349      }
 350    //else if (mode==4) format_value(availableMemory());
 351    //else if (mode==4) format_value(quality);
 352      else if (mode==4) format_text ("DEBUG");
 353      else if (mode==3) { 
 354        format_value(minv);
 355        format_blank_zero();
 356        format_char(1,'b');
 357        //format_value((int)(burpCount));
 358      }
 359    }
 360  } else if (!buf_low) { // unless buffer low !
 361   //either one of those: 
 362   if      (display_loop()) ; // every 2ms 
 363   else if (taster_loop()) ; // every 15ms
 364   else if (fps_timeout()) ; // every 40ms - fps
 365  }
 366  
 367 #if 0 // measure freq
 368   static unsigned long prev_ts3 = 0;
 369   unsigned long now = millis();
 370   if (now < prev_ts3 || now >= (prev_ts3 + 1000)) {
 371     noInterrupts();
 372     format_value((int)(burpCount/10)); in 10 Hz
 373     burpCount=0;
 374     interrupts();
 375     prev_ts3=now;
 376   }
 377 #endif
 378 }
 379 
 380 
 381 int fps_timeout() {
 382   static unsigned long prev_ts5 = 0;
 383   static int fcnt = 0;
 384   static int good = 0;
 385   static char prev_ltc = 0;
 386   unsigned long now = millis();
 387   if (now < prev_ts5 || now >= (prev_ts5 + fcnt)) {
 388     fcnt+=40+6; // XXX hardcoded 25 fps + 15%
 389     // TODO: keep track of "quality"
 390     if (fcnt>=1000) { fcnt=0; prev_ts5=now; quality= ((quality*3)+good)>>2; good=0;}
 391     if (have_ltc) { good++;}
 392     
 393     prev_ltc= (prev_ltc<<1) | (have_ltc?1:0);
 394     if ((prev_ltc&4095)==0 && mode ==0) {
 395      #if 0 // blank if not running 
 396       format_value(0); format_blank_zero();
 397      #endif
 398       dotstate|=4;
 399     } else if (mode==0) dotstate&=~4;
 400     
 401     have_ltc=false;
 402     return 1;
 403   }
 404   return 0;
 405 }
 406 
 407 int availableMemory() {
 408   int size = 640;
 409   byte *buf;
 410   while ((buf = (byte *) malloc(--size)) == NULL) ;
 411   free(buf);
 412   return size;
 413 }
 414 
 415 
