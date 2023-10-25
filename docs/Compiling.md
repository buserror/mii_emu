## How to I compile it and run it?
   * You need a C compiler, make, and a few libraries:
      * libasound2-dev [ optional, for audio ]
      * libgl-dev
      * libglu-dev
      * libx11-dev
   * Many of them will probably be installed already if you regulargly compile stuff.
   * Then just type `make` and it should compile.
   * To run it, just type `build-x86_64-linux-gnu/bin/bin/mii_emu` and it should start.

## Development
* This is the first project I made using vscode as my main IDE; I used Eclipse for many years as the indexer was pretty much unbeatable (still is).
* To support vscode I needed some tooling to create the '*compile_commands.json*' file, which is used by the C/C++ extension to provide intellisense. So to create that file, you use `make lsp` and it will create the file in the project directory.
* Another thing vscode sucks at is Makefiles (despite the extension, building is painful and next to useless). So there is a target called `make watch` that you can run in a separate terminal to auto-build as soon as you same any files in vscode. That is when you realize for example that vscode re-save the file anytime you press control-S, regardless of wether the file has changed or not. So you end up with a lot of unnecessary builds. I'm sure there is a way to fix that, but I haven't found it yet.

## Code Style
I have pretty consistent code style across my projects.
* tabs=4. Just because.
* 80 columns lines or so. I don't like to scroll horizontally and I like splitting my screen vertically. (also see, tabs=4!).
* K&R style exclusively. None of that Allman/GNU horribleness.
* No Yoda, we are no longer in 1975. `if (0 == foo)` is just stupid.
* I use !! to convert to 1 or zero from an expression. I call it the 'normalisation operator'. it is not commonly seen, apart from places like the Linux kernel.
* I use *minipt.h* for protothreads. I like protothreads, and when you are aware of their limitations, they are a great tool. They are used for the video rendering, the no-slot-clock etc.

## What needs doing?
* I'm sure there are bugs. I haven't tested it on a lot of hardware, and apart from quite a few games and a few known productivity app, it had had very little extensive testing. So testing!
* In the code there are a few bits which needs fixing
   * The mouse card is not working properly. It works for some (most importantly A2 Desktop), but it's not working properly. I suspect the VBL Interrupt handling.
   * In mii.c, the *mii_page_table_update()* does work, but it sub-optimal, it was handy to debug the countless combination of soft-switches, but it is not the ideal way to handle that problem; it needs a proper switch-case statement.
   * The floppy drive emulation was borrowed from bobbin, and it works, and it
   got it all working, but it's definitely not matching the style of the rest of the codebase. It needs to be replaced.
   * Plenty of the most complicated piece of code (video, memory mapping) load a dozen of soft-switches, it probably should use a separately maintained bit field that is maintained by the on/off switches, and would make it a lot easier to test for bit combinations.
   * The static array of memory 'banks' works, but it prevents me easily making a memory extension card. It should be refactored at some point.