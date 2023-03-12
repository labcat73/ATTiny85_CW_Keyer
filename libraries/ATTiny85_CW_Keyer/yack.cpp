/*!
 
 @file      yack.c
 @brief     CW Keyer library
 @author    Jan Lategahn DK3LJ jan@lategahn.com (C) 2011; modified by Jack Welch AI4SV; modified by Don Froula WD9DMP
 
 @version   0.87
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 
 @date      15.10.2010  - Created
 @date      03.10.2013  - Last update
 @date      21.12.2016  - Added additional prosigns and punctuation. Added 2 additional memories for ATTINY85. Fixed save of speed change to EEPROM. (WD9DMP)
 @date      03.01.2017  - If memory recording is interrupted by command button, keyer now returns txok ("R") and stays in command mode. Memory is unchanged.
                          Memory playback halts immediately on command key instead of looping through message length without playing anything.
                          Removed playback of recorded message before saving.
                          Changed yackstring command to return to command mode instead of normal mode if interrupted with command key
 
 @todo      Make the delay dependent on T/C 1 

*/

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>
#include <stdint.h>
#include "yack.h"

// Forward declaration of private functions
static void key(byte mode);
static char morsechar(byte buffer);
static void keylatch(void);

// Enumerations
enum FSMSTATE
{
  IDLE,   //!< Not keyed, waiting for paddle
  KEYED,  //!< Keyed, waiting for duration of current element
  IEG     //!< In Inter-Element-Gap
};

// Module local definitions
static byte yackflags;     // Permanent (stored) status of module flags
static byte volflags = 0;  // Temporary working flags (volatile)
static word ctcvalue;      // Pitch
static word wpmcnt;        // Speed
static byte wpm;           // Real wpm
static byte farnsworth;    // Additional Farnsworth pause

// EEPROM Data
byte magic EEMEM = MAGPAT;                           // Needs to contain 'A5' if mem is valid
byte flagstor EEMEM = (IAMBICA | TXKEY | SIDETONE);  // Defaults
word ctcstor EEMEM = DEFCTC;                         // Pitch = 700Hz
byte wpmstor EEMEM = DEFWPM;                         // 15 WPM
byte fwstor EEMEM = 0;                               // No farnsworth pause
word user1 EEMEM = 0;                                // User storage
word user2 EEMEM = 0;                                // User storage

//char eebuffer1[100] EEMEM = "message 1";
//char eebuffer2[100] EEMEM = "message 2";
char eebuffer1[100] EEMEM = "message 1";
char eebuffer2[100] EEMEM = "message 2";
char eebuffer3[100] EEMEM = "message 3";
char eebuffer4[100] EEMEM = "message 4";

// Flash data

//! Morse code table in Flash
//! Encoding: Each byte is read from the left. 0 stands for a dot, 1
//! stands for a dash. After each played element the content is shifted
//! left. Playback stops when the leftmost bit contains a "1" and the rest
//! of the bits are all zero.
//!
//! Example: A = .-
//! Encoding: 01100000
//!           .-
//!             | This is the stop marker (1 with all trailing zeros)
const byte morse[] PROGMEM =
{
  0b11111100,  // 0
  0b01111100,  // 1
  0b00111100,  // 2
  0b00011100,  // 3
  0b00001100,  // 4
  0b00000100,  // 5
  0b10000100,  // 6
  0b11000100,  // 7
  0b11100100,  // 8
  0b11110100,  // 9
  0b01100000,  // A
  0b10001000,  // B
  0b10101000,  // C
  0b10010000,  // D
  0b01000000,  // E
  0b00101000,  // F
  0b11010000,  // G
  0b00001000,  // H
  0b00100000,  // I
  0b01111000,  // J
  0b10110000,  // K
  0b01001000,  // L
  0b11100000,  // M
  0b10100000,  // N
  0b11110000,  // O
  0b01101000,  // P
  0b11011000,  // Q
  0b01010000,  // R
  0b00010000,  // S
  0b11000000,  // T
  0b00110000,  // U
  0b00011000,  // V
  0b01110000,  // W
  0b10011000,  // X
  0b10111000,  // Y
  0b11001000,  // Z
  0b00110010,  // ?
  0b01010110,  // .
  0b10010100,  // /
  0b11101000,  // ! (American Morse version, commonly used in ham circles)
  0b11001110,  // ,
  0b11100010,  // :
  0b10101010,  // ;
  0b01001010,  // "
  0b00010011,  // $
  0b01111010,  // ' (Apostrophe)
  0b10110100,  // ( or [ (also prosign KN)
  0b10110110,  // ) or ]
  0b10000110,  // - (Hyphen or single dash)
  0b01101010,  // @
  0b00110110,  // _ (Underline)
  0b01010010,  // Paragaraph break symbol
  0b10001100,  // = and BT
  0b00010110,  // SK
  0b01010100,  // + and AR
  0b10001011,  // BK
  0b01000100,  // AS
  0b10101100,  // KA (also ! in alternate Continental Morse)
  0b00010100,  // VE
  0b01011000   // AA
};

// The special characters at the end of the above table can not be decoded
// without a small table to define their content. # stands for SK, $ for AR

// To add new characters, add them in the code table above at the end and below
// Do not forget to increase the legth of the array..
const char spechar[24] PROGMEM = "?./!,:;~$^()-@_|=#+*%&<>";

// Define register bit for Timer0 tone output. Eiher PB0 or PB1 on ATTiny85
#if (STPIN == 0)
  #define COMSTPIN COM0A0
#elif (STPIN == 1)
  #define COMSTPIN COM0B0
#else
  #error "Only PB0 and PB1 supported on ATTiny85!
#endif


// Functions

// ***************************************************************************
// Control functions
// ***************************************************************************

/*! 
 @brief     Sets all yack parameters to standard values

 This function resets all YACK EEPROM settings to their default values as 
 stored in the .h file. It sets the dirty flag and calls the save routine
 to write the data into EEPROM immediately.
*/
void yackreset(void)
{
  ctcvalue = DEFCTC;                    // Initialize to 800 Hz
  wpm = DEFWPM;                         // Init to default speed
  wpmcnt = (1200 / YACKBEAT) / DEFWPM;  // default speed
  farnsworth = 0;                       // No Farnsworth gap
  yackflags = FLAGDEFAULT;
  volflags |= DIRTYFLAG;

  // Store them in EEPROM
  yacksave();
}


/*! 
 @brief     Initializes the YACK library
 
 This function initializes the keyer hardware according to configurations in the .h file.
 Then it attempts to read saved configuration settings from EEPROM. If not possible, it
 will reset all values to their defaults.
 This function must be called once before the remaining fuctions can be used.
*/
void yackinit(void)
{
  byte magval;

  // Configure DDR. Make OUT and ST output ports
  SETBIT(OUTDDR, OUTPIN);
  SETBIT(STDDR, STPIN);

  // Configure internal pullups for all inputs
  if (DITPULLUP)
  {
    SETBIT(KEYPORT, DITPIN);
  }
  if (DAHPULLUP)
  {
    SETBIT(KEYPORT, DAHPIN);
  }
  if (BTNPULLUP)
  {
    SETBIT(BTNPORT, BTNPIN);
  }

  // Retrieve magic value
  magval = eeprom_read_byte(&magic);

  // Is memory valid
  if (magval == MAGPAT)
  {
    ctcvalue = eeprom_read_word(&ctcstor);    // Retrieve last ctc setting
    wpm = eeprom_read_byte(&wpmstor);         // Retrieve last wpm setting
    wpmcnt = (1200 / YACKBEAT) / wpm;         // Calculate speed
    farnsworth = eeprom_read_byte(&fwstor);   // Retrieve last wpm setting
    yackflags = eeprom_read_byte(&flagstor);  // Retrieve last flags
  }
  else
  {
    yackreset();
  }

  yackinhibit(OFF);

#ifdef POWERSAVE
  PCMSK |= PWRWAKE;      // Define which keys wake us up
  GIMSK |= (1 << PCIE);  // Enable pin change interrupt
#endif

  // Initialize timer1 to serve as the system heartbeat
  // CK runs at 1MHz. Prescaling by 64 makes that 15625 Hz.
  // Counting 78 cycles of that generates an overflow every 5ms

  OCR1C = 78;                         // 77 counts per cycle
  TCCR1 |= (1 << CTC1) | 0b00000111;  // Clear Timer on match, prescale ck by 64
  OCR1A = 1;                          // CTC mode does not create an overflow so we use OCR1A
}


#ifdef POWERSAVE
/*! 
 @brief     A dummy pin change interrupt
 
 This function is called whenever the system is in sleep mode and there is a level change on one of the contacts 
 we are monitoring (dit, dah and the command key). As all handling is already taken care of by polling in the main 
 routines, there is nothing we need to do here.
 */
ISR(PCINT0_vect)
{
  // Nothing to do here. All we want is to wake up..
}


/*! 
 @brief     Manages the power saving mode
 
 This is called in yackbeat intervals with either a TRUE or FALSE as parameter. Whenever the
 parameter is TRUE a beat counter is advanced until the timeout level is reached. When timeout
 is reached, the chip shuts down and will only wake up again when issued a level change interrupt on
 either of the input pins.
 
 When the parameter is FALSE, the counter is reset.
 
 @param n   TRUE: OK to sleep, FALSE: Can not sleep now
 
*/
void yackpower(byte n)
{
  static uint32_t shdntimer = 0;

  // True = we could go to sleep
  if (n)
  {
    if (shdntimer++ == YACKSECS(PSTIME))
    {
      // So we do not go to sleep right after waking up..
      shdntimer = 0;

      set_sleep_mode(SLEEP_MODE_PWR_DOWN);
      sleep_bod_disable();
      sleep_enable();
      sei();
      sleep_cpu();
      cli();

      // There is no technical reason to CLI here but it avoids hitting the ISR every time
      // the paddles are touched. If the remaining code needs the interrupts this is OK to remove.
    }
  }
  // Passed parameter is FALSE
  else
  {
    shdntimer = 0;
  }
}
#endif


/*! 
 @brief     Saves all permanent settings to EEPROM
 
 To save EEPROM write cycles, writing only happens when the flag DIRTYFLAG is set.
 After writing the flag is cleared
 
 @callergraph
 
 */
void yacksave(void)
{
  // Dirty flag set?  
  if (volflags & DIRTYFLAG)
  {
    eeprom_write_byte(&magic, MAGPAT);
    eeprom_write_word(&ctcstor, ctcvalue);
    eeprom_write_byte(&wpmstor, wpm);
    eeprom_write_byte(&flagstor, yackflags);
    eeprom_write_byte(&fwstor, farnsworth);

    // Clear the dirty flag
    volflags &= ~DIRTYFLAG;
  }
}


/*! 
 @brief     Inhibits keying during command phases
 
 This function is used to inhibit and re-enable TX keying (if configured) and enforce the internal 
 sidetone oscillator to be active so that the user can communicate with the keyer.
 
 @param mode   ON inhibits keying, OFF re-enables keying 
 
 */
void yackinhibit(byte mode)
{
  if (mode)
  {
    volflags &= ~(TXKEY | SIDETONE);
    volflags |= SIDETONE;
  }
  else
  {
    volflags &= ~(TXKEY | SIDETONE);
    volflags |= (yackflags & (TXKEY | SIDETONE));

    key(UP);
  }
}


/*! 
 @brief     Saves user defined settings
 
 The routine using this library is given the opportunity to save up to two 16 bit sized
 values in EEPROM. In case of the sample main function this is used to store the beacon interval 
 timer value. The routine is not otherwise used by the library.
 
 @param func    States if the data is retrieved (READ) or written (WRITE) to EEPROM
 @param nr      1 or 2 (Number of user storage to access)
 @param content The 16 bit word to write. Not used in read mode.
 @return        The content of the retrieved value in read mode.
 
 */
word yackuser(byte func, byte nr, word content)
{
  if (func == READ)
  {
    if (nr == 1)
    {
      return (eeprom_read_word(&user1));
    }
    else if (nr == 2)
    {
      return (eeprom_read_word(&user2));
    }
  }

  if (func == WRITE)
  {
    if (nr == 1)
    {
      eeprom_write_word(&user1, content);
    }
    else if (nr == 2)
    {
      eeprom_write_word(&user2, content);
    }
  }

  return (FALSE);
}


/*! 
 @brief     Retrieves the current WPM speed
 
 This function delivers the current WPM speed. 

 @return        Current speed in WPM
 
 */
word yackwpm(void)
{
  return wpm;
}


/*! 
 @brief     Increases or decreases the current WPM speed
 
 The amount of increase or decrease is in amounts of wpmcnt. Those are close to real
 WPM in a 10ms heartbeat but can significantly differ at higher heartbeat speeds.
 
 @param dir     UP (faster) or DOWN (slower)
 
 */
void yackspeed(byte dir, byte mode)
{
  if (mode == FARNSWORTH)
  {
    if ((dir == UP) && (farnsworth > 0))
    {
      farnsworth--;
    }

    if ((dir == DOWN) && (farnsworth < MAXFARN))
    {
      farnsworth++;
    }
  }
  // WPMSPEED  
  else
  {
    if ((dir == UP) && (wpm < MAXWPM))
    {
      wpm++;
    }

    if ((dir == DOWN) && (wpm > MINWPM))
    {
      wpm--;
    }

    // Calculate beats
    wpmcnt = (1200 / YACKBEAT) / wpm;
  }

  // Set the dirty flag
  volflags |= DIRTYFLAG;

  yackplay(DIT);
  yackdelay(IEGLEN);  // Inter Element gap
  yackplay(DAH);
  yackdelay(ICGLEN);  // Inter Character gap
  yackfarns();        // Additional Farnsworth delay
}


/*! 
 @brief     Heartbeat delay
 
 Several functions in the keyer are timing dependent. The most prominent example is the
 yackiambic function that implements the IAMBIC keyer finite state machine.
 The same expects to be called in intervals of YACKBEAT milliseconds. How this is 
 implemented is left to the user. In a more complex application this would be done
 using an interrupt or a timer. For simpler cases this is a busy wait routine
 that delays exactly YACKBEAT ms.
 
 */
void yackbeat(void)
{
  while ((TIFR & (1 << OCF1A)) == 0)
  {
    // Wait for Timeout
    ;
  }

  // Reset output compare flag
  TIFR |= (1 << OCF1A);
}


/*! 
 @brief     Increases or decreases the sidetone pitch
 
 Changes are done not in Hz but in ctc control values. This is to avoid extensive 
 calculations at runtime. As is all calculations are done by the preprocessor.
 
 @param dir     UP or DOWN
 
 */
void yackpitch(byte dir)
{
  if (dir == UP)
  {
    ctcvalue--;
  }

  if (dir == DOWN)
  {
    ctcvalue++;
  }

  if (ctcvalue < MAXCTC)
  {
    ctcvalue = MAXCTC;
  }

  if (ctcvalue > MINCTC)
  {
    ctcvalue = MINCTC;
  }

  // Set the dirty flag
  volflags |= DIRTYFLAG;
}


/*! 
 @brief     Activates Tuning mode
 
 This produces a solid keydown for TUNEDURATION seconds. After this the TX is unkeyed.
 The same can be achieved by presing either the DIT or the DAH contact or the control key.
 
*/
void yacktune(void)
{
  word timer = YACKSECS(TUNEDURATION);

  key(DOWN);

  while (timer && (KEYINP & (1 << DITPIN)) && (KEYINP & (1 << DAHPIN)) && !yackctrlkey(TRUE))
  {
    timer--;
    yackbeat();
  }

  key(UP);
}


/*! 
 @brief     Sets the keyer mode (e.g. IAMBIC A)
 
 This allows to set the content of the two mode bits in yackflags. Currently only
 two modes are supported, IAMBIC A and IAMBIC B.
 
 @param mode    IAMBICA or IAMBICB
 @return    TRUE is all was OK, FALSE if configuration lock prevented changes
 
 */
void yackmode(byte mode)
{
  yackflags &= ~MODE;
  yackflags |= mode;

  // Set the dirty flag
  volflags |= DIRTYFLAG;
}


/*! 
 @brief     Query feature flags
 
 @param flag A byte which indicate which flags are to be queried 
 @return     0 if the flag(s) were clear, >0 if flag(s) were set
 
 */
byte yackflag(byte flag)
{
  return yackflags & flag;
}


/*! 
 @brief     Toggle feature flags
 
 When passed one (or more) flags, this routine flips the according bit in yackflags and
 thereby enables or disables the corresponding feature.
 
 @param flag    A byte where any bit to toggle is set e.g. SIDETONE 
 @return    TRUE if all was OK, FALSE if configuration lock prevented changes
 
 */
void yacktoggle(byte flag)
{
  // Toggle the feature bit
  yackflags ^= flag;

  // Set the dirty flag
  volflags |= DIRTYFLAG;
}


/*! 
 @brief     Creates a series of 8 dits
 
 The error prosign (8 dits) can not be encoded in our coding table. A call to this
 function produces it..
 
 */
void yackerror(void)
{
  byte i;

  for (i = 0; i < 8; i++)
  {
    yackplay(DIT);
    yackdelay(DITLEN);
  }

  yackdelay(DAHLEN);
}


// ***************************************************************************
// CW Playback related functions
// ***************************************************************************

/*! 
 @brief     Keys the transmitter and produces a sidetone
 
 .. but only if the corresponding functions (TXKEY and SIDETONE) have been set in
 the feature register. This function also handles a request to invert the keyer line
 if necessary (TXINV bit).
 
 This is a private function.

 @param mode    UP or DOWN
 
 */
static void key(byte mode)
{
  if (mode == DOWN)
  {
    // Are we generating a Sidetone?    
    if (volflags & SIDETONE)
    {
      // Then switch on the Sidetone generator
      OCR0A = ctcvalue;
      OCR0B = ctcvalue;

      // Activate CTC mode
      TCCR0A |= (1 << COMSTPIN | 1 << WGM01);

      // Configure prescaler; clkio/8
      TCCR0B = 1 << CS01;
    }

    // Are we keying the TX?
    if (volflags & TXKEY)
    {
      // Do we need to invert keying?
      if (yackflags & TXINV)
      {
        CLEARBIT(OUTPORT, OUTPIN);
      }
      else
      {
        SETBIT(OUTPORT, OUTPIN);
      }
    }
  }

  if (mode == UP)
  {
    // Sidetone active?
    if (volflags & SIDETONE)
    {
      TCCR0A = 0;
      TCCR0B = 0;
    }

    // Are we keying the TX?
    if (volflags & TXKEY)
    {
      // Do we need to invert keying?
      if (yackflags & TXINV)
      {
        SETBIT(OUTPORT, OUTPIN);
      }
      else
      {
        CLEARBIT(OUTPORT, OUTPIN);
      }
    }
  }
}


/*! 
 @brief     Produces an additional waiting delay for farnsworth mode.
 
 */
void yackfarns(void)
{
  word i = farnsworth;

  while (i--)
  {
    yackdelay(1);
  }
}


/*! 
 @brief     Produces an active waiting delay for n dot counts
 
 This is used during the playback functions where active waiting is needed
 
 @param n   number of dot durations to delay (dependent on current keying speed!
 
 */
void yackdelay(byte n)
{
  byte i = n;
  byte x;

  while (i--)
  {
    x = wpmcnt;

    while (x--)
    {
      yackbeat();
    }
  }
}



/*! 
 @brief     Key the TX / Sidetone for the duration of a dit or a dah
 
 @param i   DIT or DAH
 
 */
void yackplay(byte i)
{
  key(DOWN);

#ifdef POWERSAVE
  yackpower(FALSE);  // Avoid powerdowns when keying
#endif

  switch (i)
  {
    case DAH:
      yackdelay(DAHLEN);
      break;

    case DIT:
      yackdelay(DITLEN);
      break;
  }

  key(UP);
}


/*! 
 @brief     Send a character in morse code
 
 This function translates a character passed as parameter into morse code using the 
 translation table in Flash memory. It then keys transmitter / sidetone with the characters
 elements and adds all necessary gaps (as if the character was part of a longer word).
 
 If the character can not be translated, nothing is sent.
 
 If a space is received, an interword gap is sent.
  
 @param c   The character to send
 
*/
void yackchar(char c)
{
  byte code = 0x80;  // 0x80 is an empty morse character (just eoc bit set)
  byte i;            // a counter

  // First we need to map the actual character to the encoded morse sequence in
  // the array "morse"

  // Is it a numerical digit?
  if (c >= '0' && c <= '9')
  {
    // Find it in the beginning of array
    code = pgm_read_byte(&morse[c - '0']);
  }

  // Is it a character?
  if (c >= 'a' && c <= 'z')
  {
    // Find it from position 10
    code = pgm_read_byte(&morse[c - 'a' + 10]);
  }

  // Is it a character in upper case?
  if (c >= 'A' && c <= 'Z')
  {
    // Same as above
    code = pgm_read_byte(&morse[c - 'A' + 10]);
  }

  // Last we need to handle special characters. There is a small char
  // array "spechar" which contains the characters for the morse elements
  // at the end of the "morse" array (see there!)

  // Read through the array
  for (i = 0; i < sizeof(spechar); i++)
  {
    // Does it contain our character    
    if (c == pgm_read_byte(&spechar[i]))
    {
      // Map it to morse code
      code = pgm_read_byte(&morse[i + 36]);
    }
  }

  // Do they want us to transmit a space (a gap of 7 dots)
  if (c == ' ')
  {
    // ICG was already played after previous char
    yackdelay(IWGLEN - ICGLEN);
  }
  else
  {
    // Stop when EOC bit has reached MSB
    while (code != 0x80)
    {
      // Stop playing if someone pushes key
      if (yackctrlkey(FALSE))
      {
        return;
      }

      // MSB set ?
      if (code & 0x80)
      {
        // ..then play a dash
        yackplay(DAH);
      }
      // MSB cleared ?
      else
      {
        // .. then play a dot
        yackplay(DIT);
      }

      // Inter Element gap
      yackdelay(IEGLEN);

      // Shift code on position left (to next element)
      code = code << 1;
    }

    // IEG was already played after element
    yackdelay(ICGLEN - IEGLEN);

    // Insert another gap for farnsworth keying
    yackfarns();
  }
}


/*! 
 @brief     Sends a 0-terminated string in CW which resides in Flash
 
 Reads character by character from flash, translates into CW and keys the transmitter
 and/or sidetone depending on feature bit settings.
 
 @param p   Pointer to string location in FLASH 
 
 */
void yackstring(const char *p)
{
  char c;

  // While end of string in flash not reached and ctrl not pressed
  while ((c = pgm_read_byte(p++)) && !(yackctrlkey(FALSE)))
  {
    // Play the read character
    yackchar(c);

    // abort now if someone presses command key
  }
}


/*! 
 @brief     Sends a number in CW
 
 Transforms a number up to 65535 into its digits and sends them in CW
 
 @param n   The number to send
 
 */
void yacknumber(word n)
{
  char buffer[5];
  byte i = 0;

  // Until nothing left or control key pressed
  while (n)
  {
    // Store rest of division by 10
    buffer[i++] = n % 10 + '0';

    // Divide by 10
    n /= 10;
  }

  while (i)
  {
    if (yackctrlkey(FALSE))
    {
      break;
    }

    yackchar(buffer[--i]);
  }

  yackchar(' ');
}


// ***************************************************************************
// CW Keying related functions
// ***************************************************************************

/*! 
 @brief     Latches the status of the DIT and DAH paddles
 
 If either DIT or DAH are keyed, this function sets the corresponding bit in 
 volflags. This is used by the IAMBIC keyer to determine which element needs to 
 be sounded next.
 
 This is a private function.

 */
static void keylatch(void)
{
  // Status of swap flag
  byte swap;

  swap = (yackflags & PDLSWAP);

  if (!(KEYINP & (1 << DITPIN)))
  {
    volflags |= (swap ? DAHLATCH : DITLATCH);
  }

  if (!(KEYINP & (1 << DAHPIN)))
  {
    volflags |= (swap ? DITLATCH : DAHLATCH);
  }
}


/*! 
 @brief     Scans for the Control key
 
 This function is regularly called at different points in the program. In a normal case
 it terminates instantly. When the command key is found to be closed, the routine idles
 until it is released again and returns a TRUE return value.
 
 If, during the period where the contact was closed one of the paddles was closed too,
 the wpm speed is changed and the keypress not interpreted as a Control request. 

 @param mode    TRUE if caller has taken care of command key press, FALSE if not
 @return        TRUE if a press of the command key is not yet handled. 
 
 @callergraph
 
 */
byte yackctrlkey(byte mode)
{
  byte volbfr;

  // Remember current volatile settings
  volbfr = volflags;

  // If command button is pressed
  if (!(BTNINP & (1 << BTNPIN)))
  {
    // Set control key latch
    volbfr |= CKLATCH;

    // Apparently the control key has been pressed. To avoid bouncing
    // We will now wait a short while and then busy wait until the key is
    // released.
    // Should we find that someone is keying the paddle, let him change
    // the speed and pretend ctrl was never pressed in the first place..

    // Stop keying, switch on sidetone.
    yackinhibit(ON);

    _delay_ms(50);

    // Busy wait for release
    while (!(BTNINP & (1 << BTNPIN)))
    {
      // Someone pressing DIT paddle
      if (!(KEYINP & (1 << DITPIN)))
      {
        yackspeed(DOWN, WPMSPEED);
        // Ignore that control key was pressed
        volbfr &= ~(CKLATCH);
      }

      // Someone pressing DAH paddle
      if (!(KEYINP & (1 << DAHPIN)))
      {
        yackspeed(UP, WPMSPEED);
        volbfr &= ~(CKLATCH);
      }
    }

    // Trailing edge debounce
    _delay_ms(50);

    // In case we had a speed change
    yacksave();
  }

  // Restore previous state
  volflags = volbfr;

  // Does caller want us to reset latch?
  if (mode == TRUE)
  {
    volflags &= ~(CKLATCH);
  }

  // In case we had a speed change (Does NOT work if command is here - moved immediately after button release debounce)
  // yacksave();

  // Tell caller if we had a ctrl button press
  return ((volbfr & CKLATCH) != 0);
}


/*! 
 @brief     Reverse maps a combination of dots and dashes to a character
 
 This routine is passed a sequence of dots and dashes in the format we use for morse
 character encoding (see top of this file). It looks up the corresponding character in
 the Flash table and returns it to the caller. 
 
 This is a private function.
 
 @param buffer    A character in YACK CW notation
 @return          The mapped character or /0 if no match was found  
 
 */
static char morsechar(byte buffer)
{
  byte i;

  for (i = 0; i < sizeof(morse); i++)
  {
    if (pgm_read_byte(&morse[i]) == buffer)
    {
      // First 10 chars are digits
      if (i < 10)
      { 
        return ('0' + i);
      }

      // Then follow letters
      if (i < 36)
      {
        return ('A' + i - 10);
      }

      // Then special chars
	    return (pgm_read_byte(&spechar[i - 36]));
    }
  }

  return '\0';
}


/*! 
 @brief     Handles EEPROM stored CW messages (macros)
 
 When called in RECORD mode, the function records a message up to 100 characters and stores it in 
 EEPROM. The routine stops recording when timing out after DEFTIMEOUT seconds. Recording
 can be aborted using the control key. If more than 100 characters are recorded, the error prosign
 is sounded and recording starts from the beginning. After recording and timing out the message is played
 back once before it is stored. To erase a message, do not key one.
 
 When called in PLAY mode, the message is just played back. Playback can be aborted using the command
 key.
 
 @param     function    RECORD or PLAY
 @param     msgnr       1 or 2 or 3 or 4
 @return    TRUE if all OK, FALSE if lock prevented message recording
 
 */
void yackmessage(byte function, byte msgnr)
{
  unsigned char rambuffer[RBSIZE];  // Storage for the message
  unsigned char c;                  // Work character

  word extimer = 0;  // Detects end of message (10 sec)

  byte i = 0;  // Pointer into RAM buffer
  byte n;      // Generic counter

  if (function == RECORD)
  {
    // 5 Second until message end
    extimer = YACKSECS(DEFTIMEOUT);

    // Continue until we waited 10 seconds
    while (extimer--)
    {
      if (yackctrlkey(FALSE))
      {
        return;
      }

      // Check for a character from the key
      if ((c = yackiambic(ON)))
      {
        // Add that character to our buffer
        rambuffer[i++] = c;

        // Reset End of message timer
        extimer = YACKSECS(DEFTIMEOUT);
      }

      // End of buffer reached?
      if (i >= RBSIZE)
      {
        yackerror();
        i = 0;
      }

      // 10 ms heartbeat
      yackbeat();
    }

    // Extimer has expired. Message has ended

    // Was anything received at all?
    if (i)
    {
      // Add a \0 end marker over last space      
      rambuffer[--i] = 0;

#if 0
      // Replay the message
      for (n=0;n<i;n++)
      {
        //Break to command mode without saving if command key pressed
        if (yackctrlkey(TRUE))
        {
          return;
        }

        yackchar(rambuffer[n]);
      }
#endif

      // Store it in EEPROM
      if (msgnr == 1)
      {
        eeprom_write_block(rambuffer, eebuffer1, RBSIZE);
      }

      if (msgnr == 2)
      {
        eeprom_write_block(rambuffer, eebuffer2, RBSIZE);
      }

      if (msgnr == 3)
      {
        eeprom_write_block(rambuffer, eebuffer3, RBSIZE);
      }

      if (msgnr == 4)
      {
        eeprom_write_block(rambuffer, eebuffer4, RBSIZE);
      }
    }
    else
    {
      yackerror();
    }
  }

  if (function == PLAY)
  {
    // Retrieve the message from EEPROM
    if (msgnr == 1)
    {
      eeprom_read_block(rambuffer, eebuffer1, RBSIZE);
    }

    if (msgnr == 2)
    {
      eeprom_read_block(rambuffer, eebuffer2, RBSIZE);
    }

    if (msgnr == 3)
    {
      eeprom_read_block(rambuffer, eebuffer3, RBSIZE);
    }

    if (msgnr == 4)
    {
      eeprom_read_block(rambuffer, eebuffer4, RBSIZE);
    }

    // Replay the message
    // Read until end of message
    for (n = 0; (c = rambuffer[n]); n++)
    {
      //Break immediately if command key pressed
      if (yackctrlkey(TRUE))
      {
        return;
      }

      // Play it back
      yackchar(c);
    }
  }
}


/*! 
 @brief     Finite state machine for the IAMBIC keyer
 
 If IAMBIC (squeeze) keying is requested, this routine, which usually terminates
 immediately needs to be called in regular intervals of YACKBEAT milliseconds.
 
 This can happen though an outside busy waiting loop or a counter mechanism.
 
 @param ctrl    ON if the keyer should recognize when a word ends. OFF if not.
 @return        The character if one was recognized, /0 if not
 
 */
char yackiambic(byte ctrl)
{
  static enum FSMSTATE fsms = IDLE;  // FSM state indicator
  static word timer;                 // A countdown timer
  static byte lastsymbol;            // The last symbol sent
  static byte buffer = 0;            // A place to store a sent char
  static byte bcntr = 0;             // Number of elements sent
  static byte iwgflag = 0;           // Flag: Are we in interword gap?
  static byte ultimem = 0;           // Buffer for last keying status
  char retchar;                      // The character to return to caller

  // This routine is called every YACKBEAT ms. It starts with idle mode where
  // the morse key is polled. Once a contact close is sensed, the TX key is
  // closed, the sidetone oscillator is fired up and the FSM progresses
  // to the next state (KEYED). There it waits for the timer to expire,
  // afterwards progressing to IEG (Inter Element Gap).
  // Once the IEG has completed, processing returns to the IDLE state.

  // If the FSM remains in idle state long enough (one dash time), the
  // character is assumed to be complete and a decoding is attempted. If
  // succesful, the ascii code of the character is returned to the caller

  // If the FSM remains in idle state for another 4 dot times (7 dot times
  // altogether), we assume that the word has ended. A space char
  // is transmitted in this case.

  // Count down
  if (timer)
  {
    timer--;
  }

  // No space detection
  if (ctrl == OFF)
  {
    iwgflag = 0;
  }

  switch (fsms)
  {
    case IDLE:
      keylatch();

#ifdef POWERSAVE
      // OK to go to sleep when here.
      yackpower(TRUE);
#endif

      // Handle latching logic for various keyer modes
      switch (yackflags & MODE)
      {
        case IAMBICA:
        case IAMBICB:
          // When the paddle keys are squeezed, we need to ensure that
          // dots and dashes are alternating. To do that, whe delete
          // any latched paddle of the same kind that we just sent.
          // However, we only do this ONCE
          volflags &= ~lastsymbol;
          lastsymbol = 0;

          break;

        case ULTIMATIC:
          // Ultimatic logic: The last paddle to be active will be repeated indefinitely
          // In case the keyer is squeezed right out of idle mode, we just send a DAH

          // Squeezed?
          if ((volflags & SQUEEZED) == SQUEEZED)
          {
            if (ultimem)
            {
              // Opposite symbol from last one
              volflags &= ~ultimem;
            }
            else
            {
              // Reset the DIT latch
              volflags &= ~DITLATCH;
            }
          }
          // Remember the last single key
          else
          {
            ultimem = volflags & SQUEEZED;
          }

          break;

        case DAHPRIO:
          // If both paddles pressed, DAH is given priority
          if ((volflags & SQUEEZED) == SQUEEZED)
          {
            // Reset the DIT latch
            volflags &= ~DITLATCH;
          }

          break;
      }

      // The following handles the inter-character gap. When there are
      // three (default) dot lengths of space after an element, the
      // character is complete and can be returned to caller

      // Have we idled for 3 dots and is there something to decode?
      if (timer == 0 && bcntr != 0)
      {
        buffer = buffer << 1;                // Make space for the termination bit
        buffer |= 1;                         // The 1 on the right signals end
        buffer = buffer << (7 - bcntr);      // Shift to left justify
        retchar = morsechar(buffer);         // Attempt decoding
        buffer = bcntr = 0;                  // Clear buffer
        timer = (IWGLEN - ICGLEN) * wpmcnt;  // If 4 further dots of gap, this might be a Word gap.

        // Signal we are waiting for IWG
        iwgflag = 1;

        // and return decoded char
        return (retchar);
      }

      // This handles the Inter-word gap. Already 3 dots have been
      // waited for, if 4 more follow, interpret this as a word end

      // Have we idled for 4+3 = 7 dots?
      if (timer == 0 && iwgflag)
      {
        // Clear Interword Gap flag
        iwgflag = 0;

        // And return a space
        return (' ');
      }

      // Now evaluate the latch and determine what to send next

      // Anything in the latch?
      if (volflags & (DITLATCH | DAHLATCH))
      {
        iwgflag = 0;           // No interword gap if dit or dah
        bcntr++;               // Count that we will send something now
        buffer = buffer << 1;  // Make space for the new character

        // Is it a dit?
        if (volflags & DITLATCH)
        {
          timer = DITLEN * wpmcnt;  // Duration = one dot time
          lastsymbol = DITLATCH;    // Remember what we sent
        }
        // must be a DAH then..
        else
        {
          timer = DAHLEN * wpmcnt;  // Duration = one dash time
          lastsymbol = DAHLATCH;    // Remember
          buffer |= 1;              // set LSB to remember dash
        }

        // Switch on the side tone and TX
        key(DOWN);

        // Reset both latches
        volflags &= ~(DITLATCH | DAHLATCH);

        // Change FSM state
        fsms = KEYED;
      }

      break;

    case KEYED:
#ifdef POWERSAVE
      yackpower(FALSE);  // can not go to sleep when keyed
#endif

      // If we are in IAMBIC B mode
      if ((yackflags & MODE) == IAMBICB)
      {
        // then latch here already
        keylatch();
      }

      // Done with sounding our element?
      if (timer == 0)
      {
        key(UP);                  // Then cancel the side tone
        timer = IEGLEN * wpmcnt;  // One dot time for the gap
        fsms = IEG;               // Change FSM state
      }

      break;

    case IEG:
      // Latch any paddle movements (both A and B)
      keylatch();

      // End of gap reached?
      if (timer == 0)
      {
        // Change FSM state
        fsms = IDLE;

        // The following timer determines what the IDLE state
        // accepts as character. Anything longer than 2 dots as gap will be
        // accepted for a character end.
        timer = (ICGLEN - IEGLEN - 1) * wpmcnt;
      }

      break;
  }

  // Nothing to return if not returned in above routine
  return '\0';
}
