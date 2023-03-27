# ATTiny85_CW_Keyer

This branch contains an Arduino IDE version of the ATTiny85_CW_Keyer. This is pretty much the original code from github user donfroula a little bit reshuffled to make the Arduino IDE happy.
It should be possible to load this sketch and the yack library on a Digispark ATTiny85 board using the ATTinyCore package.

Add this link to 'Additional Boards Manager URLs' in your Arduino IDE:
http://drazzy.com/package_drazzy.com_index.json
And then install ATTinyCore.

To load the sketch select ATTiny85 (Micronucleus / Digispark) in the board manager and use Programmer "Micronucleus". You must set the clock to '1MHz (no USB)'.
Make sure everything works with the standard Arduino Blink Example sketch before trying to load the keyer code.


ToDo:
- Add schematic and pictures
- Cleanup __old__ directory
- Add license textfile (GPL v2 or v3 ???)


YouTube videos:

LiveStream 1:48h
ATTiny85 CW Keyer using a Digispark | HamRadio
https://www.youtube.com/watch?v=mSMUsS8t9xk

Shorts Video with a quick demonstration
ATTiny85 CW Keyer with Digispark | #hamradio #cw #shorts
https://www.youtube.com/shorts/frkpCNYbVr8


Original description:

A full-featured CW keyer for amateur radio use. The keyer is built around a cheap and tiny ATTINY85 microcontroller. The circuit boasts 4-100 character memories, beacon mode, multiple timing options, and a CW trainer for improving your Morse code speed.
