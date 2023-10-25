
## libmish TL;DR :
*libmish* is a library that allows you to add a command prompt to any of your own program. It captures the standard output and standard error file descriptor, create a log with this, and displays it back to you, with a command prompt.

You can then add your own commands to interract with your (running) program. Oh, *libmish* can also serve this to incoming telnet sessions (on a custom port), so you can connect to detached processes too.

## Demo
Consider this program. The only "new" things are the <u>#include</u>, and the <u>mish_prepare()</u> call. That gives you telnet access and will log all output, but not much else.

```C
#include "mish.h"

int cnt = 0;

int main()
{
	mish_prepare(0);

	while (1) {
		sleep(1);
		printf("Count %d\n", cnt++);
	}
}
```

Now add this at the bottom of the file:

```C
/* And here is a command line action that can reset the 'cnt' variable */
static void _test_set_cnt(void * param, int argc, const char * argv[])
{
	if (argc > 1) {
		cnt = atoi(argv[1]);
	} else {
		fprintf(stderr,
				"%s: syntax 'set XXX' to set the variable\n", argv[0]);
	}
}

MISH_CMD_NAMES(set, "set");
MISH_CMD_HELP(set,
		"set 'cnt' variable",
		"test command for libmish!");
MISH_CMD_REGISTER(set, _test_set_cnt);
```

And here you go:
![Demo of libmish](doc/demo.gif)

As you can see, the stderr output is colorized. The program also told you the telnet port to call into (but you can also see it via the 'env' command later on).

Calling the 'set' command will change the main variable. Of course it doesn't been to be thread safe in this instance, if you want your command to run in a thread safe way, you have to use <u>mish_cmd_poll()</u> from your thread, this will run pending commands in your own context.

## Ok what's going on here, why do I need this?
Let's say, you have that program that runs for days. Or months, or years, and it has it's log in a log file and all is very well, but sometime, you'd like to just *interract* with it, say, check statistics, internal state, or just change a parameter or so. Or just pet it for the good job it's doing.

Traditionally, you'd just restart the program with new parameters, or use a *host* of other complicated methods to send commands to the running program, like signals, UNIX sockets and countless stuff like that.

*libmish* allows you to that poking around, without having to bother that much about it. Just add *libmish*, register some of your own commands, and you'll be able to 'connect' to your running program, check it's output history (without having to deal with log files) and use your own commands on your own running program.

## What does work?
Well, its' already quite useful:

  * You can browse the history of the log of your program with Beg/Page Up/Down/End
  * You can telnet in, and check the log too.
  * You can display command history and use control-P/N to navigate it
  * You can easily "register" your commands, with their own 'help'. 
  * It only requires one single "mish_prepare(...)" call at the start of yout main() to work.
 
## Security? What security?
Obviously, using mish in your program is an obvious security risk, you're 
allowing anyone on that machine to temper with your program via telnet, there's no 
safegards in place. *libmish* is really made for developement purpose on 
machine that you trust.

Also, as much as I don't think I code like a dork, I didn't go mega full paranoia on security in the internals, so there's bound checking, but I can almost guarantee there must be ways of fooling the various parsers into crashing. Again this was all written in the space of 3 days, so I went for the goalpost as a hare, not as a tank!

If you want to make sure *libmish* is disabled on machine that you *don't* trust (ie, production), you can set an environment variable MISH_OFF=1 before launching the programs and it will prevent the library starting. But again, buyers beware.

As to why I use a TCP port (bound to 127.0.0.1), well it's because:

  * I want to be able to use 'telnet' to connect...
  * 'telnet' on linux doesn't handle UNIX sockets
  * I need 'telnet' because it gives me the terminal window size.

... otherwise, I wouldn't have!

## Issues & todos & caveats
This is a brand new library, a whole ton of things works, but quite a few things don't, just yet. 

Some bits I know I want but didn't *need* and some I've tried to fix, but haven't spent enough time on it to nail them down.

**TODO:**

  * Currently the main thread uses select(), because it's portable. Need to add an epool() alternative one for linux.
  * Display a timestamp for lines in the backlog.
  * Display some sort of progressy-bar thing at the bottom when navigating the log.
  * Add a UNIX stream socket access, but we'll have to use socat/netcat, stty raw and some sort of helper command line.
  * Make the backlog expire after X hours/days. A stamp is already collected, but not used.

**BUGS:**

* Find a way to cleany quit without screwing up the terminal settings (telnet).
    - If you have that problem, use 'resize' or 'reset' to make your terminal work again.
    - Or better, use 'screen telnet xxx yyy' as screen is awesome at cleaning up.
    - or __betterer__, use the "dis" disconnect command, that works.
    - Backlog navigation doesn't take into account line wrapping.

## Why is it called *libmish* anyway, sounds silly.
Well since you think you're clever, "lib" is for library, "sh" is for *shell* and "mi" is for **me** aka, **michel** pronunced *me* *shell*. Geddit?

