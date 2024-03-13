<p align="center">
   <img src="contrib/mii-icon-64.png" alt="MII Logo">
</p>

# MII Version Changelog
## 1.8
 * Changed the floppy disk view. *It now rotates*, and the heat map is now a
   'trail' of the head, showing where it's been. It's a bit more useful, and
   looks cooler.

<div align="center">
  <img src="docs/screen/v18new_display.gif" alt="New Floppy display">
</div>
<center><i>Poor quality gif, It is a LOT smoother at 60fps in the program!</i>
</center>

 * Added support for a *Ramworks III card*, with 1MB of RAM. It could have more,
   but I thought and extra whole friggin MEGABYTE was enough for anyone.
 * Added support for flashing text in text mode. I know, it was a bit of a
   glaring omission, but it's there now.
 * Internal changes to the UI, I've split the whole codebase into a few more files,
   split the 'XORG/GLX' code from the 'Pure GL' code from the 'MII UI' code, so it should be a lot easier to port to other platforms.
 * Redid the *DHRES rendering*, it's now a lot more accurate, has the correct
   artifacts. It's not as optimized as the previous version, but it looks better.
 * Now remap the *joystick* coodinates to a 'square' -- my current 8bitdo joystick
    has a circular deadzone, and it was a bit annoying to use. I might make that
    a setting, but for now, it's hardwired.
 * *Emulator now passes a2audit*. There is only one kludge to do it, regarding the
 'phantom read' of the indirect addressing.
 * *Working Super Serial Card Driver*, it can bridge to a Linux /dev device for the moment, or to
   a 'fake' loopback device. IN#x and PR#x works, and I can bootstrap using
   ADTPro. It's all in there so I can run Mastodon!

<div align="center">
  <img src="docs/screen/v18ssc_dialog.png" alt="SSC Config">
</div>
<center><i>Super Serial Card config dialog</i></center>

### libmui
 * Standard file picker now shows *floppy icons*.
 * Added a *Color Apple Menu*, in pure Macintosh II style.
<div align="center">
  <img src="docs/screen/v18colorapple.png" alt="Color Apple">
</div>

 * Fixed a few minor memory leaks.
 * Tons more stuff in libmui, inc new font styles (bold, underline, condensed). There is also a text edit control now (still prototype).

### Internals
 * Made an architecture document, see [Compiling](docs/Compiling.md) for a top-down view.
 * Ported the support for VCD (Value Change Dump) from simavr, so I can now
   record and playback the whole simulation of the floppy driver. No real use for the user.
 * Split the video rendering into bits with a 'main' line rendering function pointer that is set only when video mode changes. This is a bit faster, and that gets rid of the Giant Function.

## 1.7
 * New animated about box, because, everyone loves a good about box.
 * Added support for Write Protect of floppy disks; disk can be write protected manually, or if the file lacks write permissions, OR if the file format (NIB, DSK) doesn't support writes.
 * New fancypants 'bit view' of the floppy are they are read/written, with a
   heat map to show where the head was last. Drive 1 appears left of the screen,
   drive 2 on the right. It fades out after the drive motor stops.

<div align="center">
  <img src="docs/screen/v17heatmap.png" alt="Heat map disk view">
</div>
<center><i>DOS 3.3 Disk 'bitstream view' on the left, the green trace shows what's just be read.</i></center>

## 1.6
 * Another big update, yanked the old DiskII driver, and replaced it with a
   homebrew one, low level, with native support for WOZ (1 & 2) files (*read AND write!*) as well as NIB and DSK (read only).
 * This is big news, and MII can now boot all kind of copy protected disks, in
   fact I tried a few hundreds, and they all worked so far!
 * There is currently no way to create a new disk, but you can use a tool like
   [CiderPress](https://a2ciderpress.com/) to create them, and then use them in MII. Or just copy your DOS 3.3.woz file and reformat it!
 * There were a few other minor changes, mostly added some timing measurement
   tooling, and a couple of (necessary for disk to work) tweaks to the emulator
   itself as it was not counting cycles correctly for a couple of instructions.
 * The UI now has support for pure power-of-two textures, for really old OpenGL
   implementations, it is turned off by default, but could work with some
   old hardware. Also updated *libmui* to make it less linux-centric.
 * Fixed some more color issues, mostly DHIRES.
 * Added 'typeahead' for when you select files in the dialog, like on old Macs
## 1.5
 * BIG update, loads of changes, fixes, improvements.
 * New super UI, using home-made libmui, channeling both GS/OS and MacOS 7.x!
 * New emulation fixes, way more accurate. Video redone, audio redone.
 * New front-end program using XLib and OpenGL 'low level'.
 * New Icon.
## 1.0
 * Fixed a few graphics rendering bugs/color swapped
 * Fixed a few Makefile issues involving pathnamed with 'spaces' in them.
 * More tweaks to the emulation, added a few cycles here and there.
 ## 0.9
 * Added a 'debugger' shell, accessible via telnet.
 * Added a mini-assembler, used to compile the drivers and the CPU unit tests.
 * Added a 'Titan Accelerator IIe' simulation, to turn on/off fast mode.
## 0.5
 * Initial release
