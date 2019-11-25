It all began in 2003 when I wrote an Adlib tracker for MS-DOS in QuickBASIC 4.5. My own use of it turned out to be more extensive than I ever thought it would be, leading to the creation of at least the twenty‑one tunes included in this music‑'disk', of which some were featured in various netlabel releases.

Sixteen years later, I finally sat down and did what I had been meaning to do for a long time. As some kind of personal digital preservation project, I ported the replay routine to C and got it to play the old tunes in the browser by compiling it to WebAssembly with Emscripten and utilizing the Emscripten port of SDL2.

The credits proper for this are:

* OPL2 emulation code―Tatsuyuki Satoh
* Replay routine and musicdisk code―The Hardliner
* Music―The Hardliner˟

˟‘Cannonball’ originally by The Breeders, and ‘Zeldni’ by Koji Kondo

The source code for all this, the tunes, and a FreeBASIC/Windows port of the original MS-DOS program, which appears to be working perfectly with Wine, may be found in the GitHub repository.

https://github.com/santosoj/pisplay

The title of this release refers to the name that was chosen for the tracker module format, ‘PIS’, which does not stand for anything and was meant as an alternative spelling of the word ‘peace’. Peace and love to everyone who loves Adlib/FM/OPL music as much as I do.

―thl 20191125
