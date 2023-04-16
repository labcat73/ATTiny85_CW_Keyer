# ATTiny85_CW_Keyer

This branch contains an Arduino IDE version of the ATTiny85_CW_Keyer.
This is pretty much the original code from github user donfroula a little bit reshuffled to make the Arduino IDE happy.
It should be possible to load this sketch and the yack library on a Digispark ATTiny85 board using the ATTinyCore package.
Just click on Code -> Download ZIP and unpack it in an Arduino project directory.

Add this link to 'Additional Boards Manager URLs' in your Arduino IDE:
http://drazzy.com/package_drazzy.com_index.json
And then install ATTinyCore.

To load the sketch select ATTiny85 (Micronucleus / Digispark) in the board manager and use Programmer "Micronucleus".
You must set the clock to '1MHz (no USB)'.
Make sure everything works with the standard Arduino Blink Example sketch before trying to load the keyer code.

For more details see the content of the "ATTiny85_CW_Keyer/doc" folder.
This contains schematics, an image of my prototype build and a slightly updated documentation.

My contribution to this project is really pretty minor.
Some code reformatting and reshuffling and a few small changes to HW and SW were done to make it run on the Digispark.
The motivation putting it onto the Digispark was to make it simpler to build this device without a dedicated AVR programmer and knowledge how to use it.
With this you can get a Digispark or a chinese clone from eBay, just add a few external components, load the SW via the Arduino IDE and you are done.
Maybe this will be useful for somebody. Alas it is 2023 and most amateur radio transceivers come with built-in keyer support anyway.


YouTube videos:

LiveStream 1:48h
ATTiny85 CW Keyer using a Digispark | HamRadio
https://www.youtube.com/watch?v=mSMUsS8t9xk

Shorts Video with a quick demonstration
ATTiny85 CW Keyer with Digispark | #hamradio #cw #shorts
https://www.youtube.com/shorts/frkpCNYbVr8


Original description:

A full-featured CW keyer for amateur radio use. The keyer is built around a cheap and tiny ATTINY85 microcontroller.
The circuit boasts 4-100 character memories, beacon mode, multiple timing options, and a CW trainer for improving your Morse code speed.
