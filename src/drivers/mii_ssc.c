/*
 * mii_ssc.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 *
 */
/*
	Theory of operation:
	There is a thread that is started for any/all cards in the system. Cards
	themselves communicate via one 'command' FIFO to start/stop themselves,
	which add/remove them from the private list of cards kept by the thread.

	The cards attempts not to open the device (and start the thread) until
	they are actually used, so it monitors for access from the ROM area,
	(that will be happening if PR#x and IN#x are used), OR when the PC
	calls into the Cx00-CxFF area.

	Once running, the thread will monitor the 'tty' file descriptor for each
	and will deal with 2 'data' FIFOs to send and receive data from the 6502.

	The SSC driver itself just monitors the FIFO state and update the status
	register, and raise IRQs as needed.
 */
/*
git clone https://github.com/colinleroy/a2tools.git
cd a2tools/src/surl-server
sudo apt-get install libcurl4-gnutls-dev libgumbo-dev libpng-dev libjq-dev libsdl-image1.2-dev
make && A2_TTY=/dev/tntX ./surl-server
*/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>

#include "mii.h"
#include "mii_bank.h"
#include "mii_sw.h"
#include "mii_ssc.h"
#include "fifo_declare.h"
#include "bsd_queue.h"

#include <termios.h>
#include <pty.h>
#include <fcntl.h>

static const mii_ssc_setconf_t _mii_ssc_default_conf = {
	.baud = 9600,
	.bits = 8,
	.parity = 0,
	.stop = 1,
	.handshake = 0,
	.is_device = 1,
	.is_socket = 0,
	.is_pty = 0,
	.socket_port = 0,
	.device = "/dev/tnt0",
};

// SW1-4  SW1 is MSB, switches are inverted ? (0=on, 1=off)
static const int _mii_ssc_to_baud[16] = {
	[0] = B1152000,	[1] = B50,		[2] = B75,		[3] = B110,
	[4] = B134,		[5] = B150,		[6] = B300,		[7] = B600,
	[8] = B1200,	[9] = B1800,	[10] = B2400,
	[12] = B4800,					[14] = B9600,	[15] = B19200,
};
// Minus ones are the ones that haven't got a tty speed, so are invalid
static const unsigned int _mii_ssc_to_baud_rate[16] = {
	[0] = 1152000,	[1] = 50,		[2] = 75,		[3] = 110,
	[4] = 134,		[5] = 150,		[6] = 300,		[7] = 600,
	[8] = 1200,		[9] = 1800,		[10] = 2400,	[11] = -3600,
	[12] = 4800,	[13] = -7200,	[14] = 9600,	[15] = 19200,
};

enum {
	SSC_6551_CONTROL_BAUD 	= 0,	// see table
	SSC_6551_CONTROL_CLOCK 	= 4,	// always 1
	SSC_6551_CONTROL_WLEN 	= 5,	// 0=8 bits, 1=7 bits, 2=6 bits, 3=5 bits
	SSC_6551_CONTROL_STOP 	= 7,	// 0 = 1 stop bit, 1 = 2 stop bits

	SSC_6551_CONTROL_RESET	= 0,

	SSC_6551_COMMAND_DTR 	= 0,	// 0=Disable Receiver (DTR), 1=Enable
	SSC_6551_COMMAND_IRQ_R 	= 1,	// 0=IRQ Enabled, 1=IRQ Disabled
	// 0:IRQ_TX=0 + RTS=1
	// 1:IRQ_TX=1 + RTS=0
	// 2:IRQ_TX=0 + RTS=0
	// 3:IRQ_TX=0 + RTS=0 + BRK
	SSC_6551_COMMAND_IRQ_T 	= 2,
	SSC_6551_COMMAND_ECHO 	= 4,	// 0=off, 1=on
	SSC_6551_COMMAND_PARITY = 5,	// None, Odd, Even, Mark, Space

	SSC_6551_COMMAND_RESET	= (1 << SSC_6551_COMMAND_IRQ_R),

	SSC_6551_PARITY_ERROR	= 0,
	SSC_6551_FRAMING_ERROR	= 1,
	SSC_6551_OVERRUN		= 2,
	SSC_6551_RX_FULL		= 3,
	SSC_6551_TX_EMPTY		= 4,
	SSC_6551_DCD			= 5,
	SSC_6551_DSR			= 6,
	SSC_6551_IRQ			= 7,

	SSC_6551_STATUS_RESET	= (1 << SSC_6551_TX_EMPTY),
};
enum {
	SSC_SW2_STOPBITS		= 1 << 7,
	SSC_SW2_DATABITS		= 1 << 6,
	SSC_SW2_IRQEN			= 1 << 0,
};
// SW2-1 is stop bits OFF = Two, ON = One (inverted)
static const unsigned int _mii_ssc_to_stop[2] = {
	[0] = 0,		[1] = CSTOPB,
};
// SW2-2 is data bits
static const unsigned int _mii_ssc_to_bits[4] = {
	[0] = CS8,		[1] = CS7, 	[2] = CS6, 		[3] = CS5,
};
static const int _mii_scc_to_bits_count[4] = {
	[0] = 8,		[1] = 7,		[2] = 6,		[3] = 5,
};
// SW2-3-4 is parity
static const unsigned int _mii_ssc_to_parity[4] = {
	[0] = 0,		[1] = PARODD, [2] = PARENB,		[3] = PARENB|PARODD,
};


enum {
	MII_SSC_STATE_INIT = 0,
	MII_SSC_STATE_START,
	MII_SSC_STATE_RUNNING,
	MII_SSC_STATE_STOP,
	MII_SSC_STATE_STOPPED,
	MII_THREAD_TERMINATE,
};

struct mii_card_ssc_t;
typedef struct mii_ssc_cmd_t {
	int 	cmd;
	union {
		struct mii_card_ssc_t * card;
	};
} mii_ssc_cmd_t;

DECLARE_FIFO(mii_ssc_cmd_t, mii_ssc_cmd_fifo, 8);
DEFINE_FIFO(mii_ssc_cmd_t, mii_ssc_cmd_fifo);

DECLARE_FIFO(uint8_t, mii_ssc_fifo, 16);
DEFINE_FIFO(uint8_t, mii_ssc_fifo);

typedef struct mii_card_ssc_t {
	// queued when first allocated, to keep a list of all cards
	STAILQ_ENTRY(mii_card_ssc_t) self;
	// queued when started, for the thread
	STAILQ_ENTRY(mii_card_ssc_t) started;
	struct mii_slot_t *	slot;
	struct mii_bank_t * rom;
	mii_rom_t *			rom_ssc;
	mii_t *				mii;
	uint8_t 			irq_num;	// MII IRQ line
	uint8_t 			slot_offset;
	mii_ssc_setconf_t	conf;
	int 				state; 		// current state, MII_SSC_STATE_*
	char 				tty_path[128];
	int 				tty_fd;		// <= 0 is not opened yet
	char 				human_config[32];
	// global counter of bytes sent/received. No functional use
	uint32_t 			total_rx, total_tx;
	uint8_t 			timer_check;
	uint32_t 			timer_delay;
	mii_ssc_fifo_t 		rx,tx;
	// 6551 registers
	uint8_t 			dipsw1, dipsw2, control, command, status;
} mii_card_ssc_t;

STAILQ_HEAD(, mii_card_ssc_t)
		_mii_card_ssc_slots = STAILQ_HEAD_INITIALIZER(_mii_card_ssc_slots);
/*
 * These bits are only meant to communicate with the thread
 */
STAILQ_HEAD(, mii_card_ssc_t)
		_mii_card_ssc_started = STAILQ_HEAD_INITIALIZER(_mii_card_ssc_started);
pthread_t 		_mii_ssc_thread_id = 0;
mii_ssc_cmd_fifo_t 	_mii_ssc_cmd = {};
int _mii_ssc_signal[2] = {0, 0};

static int
_mii_scc_set_conf(
		mii_card_ssc_t *c,
		const mii_ssc_setconf_t *conf,
		int re_open);

static void*
_mii_ssc_thread(
		void *param)
{
	printf("%s: start\n", __func__);
	do {
		/*
		 * Get commands from the MII running thread. Add/remove cards
		 * from the 'running' list, and a TERMINATE to kill the thread
		 */
		while (!mii_ssc_cmd_fifo_isempty(&_mii_ssc_cmd)) {
			mii_ssc_cmd_t cmd = mii_ssc_cmd_fifo_read(&_mii_ssc_cmd);
			switch (cmd.cmd) {
				case MII_SSC_STATE_START: {
					mii_card_ssc_t *c = cmd.card;
					printf("%s: start slot %d\n", __func__, c->slot->id);
					STAILQ_INSERT_TAIL(&_mii_card_ssc_started, c, self);
					c->state = MII_SSC_STATE_RUNNING;
				}	break;
				case MII_SSC_STATE_STOP: {
					mii_card_ssc_t *c = cmd.card;
					printf("%s: stop slot %d\n", __func__, c->slot->id);
					STAILQ_REMOVE(&_mii_card_ssc_started, c, mii_card_ssc_t, self);
					c->state = MII_SSC_STATE_STOPPED;
				}	break;
				case MII_THREAD_TERMINATE:
					printf("%s: terminate\n", __func__);
					return NULL;
			}
		}
		/*
		 * Here we use select. This is not optimal on linux, but it is portable
		 * to other OSes -- perhaps I'll add an epoll() version later
		 */
		fd_set rfds, wfds;
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		int maxfd = 0;
		FD_SET(_mii_ssc_signal[0], &rfds);
		if (_mii_ssc_signal[0] > maxfd)
			maxfd = _mii_ssc_signal[0];
		mii_card_ssc_t *c = NULL, *safe;
		STAILQ_FOREACH_SAFE(c, &_mii_card_ssc_started, self, safe) {
			// this guy might be being reconfigured, or perhaps had an error
			if (c->tty_fd < 0)
				continue;
			if (!mii_ssc_fifo_isempty(&c->tx)) {
				FD_SET(c->tty_fd, &wfds);
				if (c->tty_fd > maxfd)
					maxfd = c->tty_fd;
			}
			if (!mii_ssc_fifo_isfull(&c->rx)) {
				FD_SET(c->tty_fd, &rfds);
				if (c->tty_fd > maxfd)
					maxfd = c->tty_fd;
			}
		}
		struct timeval tv = { .tv_sec = 0, .tv_usec = 1000 };
		int res = select(maxfd + 1, &rfds, &wfds, NULL, &tv);
		if (res < 0) {
			// there are OK errors, we just ignore them
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			printf("%s ssc select: %s\n", __func__, strerror(errno));
			break;
		}
		if (res == 0)	// timeout
			continue;
		if (FD_ISSET(_mii_ssc_signal[0], &rfds)) {
			uint8_t b;
			if (read(_mii_ssc_signal[0], &b, sizeof(b)))
				/* ignored */ {};
		}
		STAILQ_FOREACH(c, &_mii_card_ssc_started, self) {
			/*
			 * Here we know the read fifo isn't full, otherwise we wouldn't have
		     * asked for more data. See what space we have in the fifo, try
		     * reading as much as that, and push it to the FIFO
			 */
			if (FD_ISSET(c->tty_fd, &rfds)) {
				uint8_t buf[mii_ssc_cmd_fifo_fifo_size];
				int max = mii_ssc_fifo_get_write_size(&c->rx);
				int res = read(c->tty_fd, buf, max);
				if (res < 0) {
					if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
						break;
					c->tty_fd = -1;
					printf("%s ssc read: %s\n", __func__, strerror(errno));
					break;
				}
				for (int i = 0; i < res; i++)
					mii_ssc_fifo_write(&c->rx, buf[i]);
			}
			/*
			 * Same here, this wouldn't be set if we hadn't got stuff to send --
			 * see what's in the fifo, 'peek' the bytes into an aligned buffer,
			 * try to write it all, and then *actually* remove them (read) the
			 * one that were sent from the fifo
			 */
			if (FD_ISSET(c->tty_fd, &wfds)) {
				uint8_t buf[mii_ssc_cmd_fifo_fifo_size];
				int max = mii_ssc_fifo_get_read_size(&c->tx);
				for (int i = 0; i < max; i++)
					buf[i] = mii_ssc_fifo_read_at(&c->tx, i);
				res = write(c->tty_fd, buf, max);
				if (res < 0) {
					if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
						break;
					printf("%s ssc write: %s\n", __func__, strerror(errno));
					break;
				}
				// flush what we've just written
				while (res--)
					mii_ssc_fifo_read(&c->tx);
			}
		}
	} while (1);
	return NULL;
}

/*
 * Write a byte to 'our' end of the socketpair (the other end is in the thread).
 * This is used to wake up the thread for example when the FIFO is full/empty
 */
static void
_mii_ssc_thread_signal(
		mii_card_ssc_t *c)
{
	uint8_t b = 0x55;
	if (_mii_ssc_signal[1] > 0) {
		write(_mii_ssc_signal[1], &b, sizeof(b));
	}
}

static void
_mii_ssc_thread_start(
		mii_card_ssc_t *c)
{
	if (c->state > MII_SSC_STATE_INIT && c->state < MII_SSC_STATE_STOP)
		return;
	if (c->tty_fd < 0) {
		printf("%s TTY not open, skip\n", __func__);
		return;
	}
	// create as socketpair in _mii_ssc_signal
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, _mii_ssc_signal) < 0) {
		printf("%s: socketpair: %s\n", __func__, strerror(errno));
		return;
	}
	c->state = MII_SSC_STATE_START;
	mii_ssc_cmd_t cmd = { .cmd = MII_SSC_STATE_START, .card = c };
	mii_ssc_cmd_fifo_write(&_mii_ssc_cmd, cmd);
	// start timer that'll check out card status
	mii_timer_set(c->mii, c->timer_check, c->timer_delay);
	// start, or kick the thread awake
	if (!_mii_ssc_thread_id) {
		printf("%s: starting thread\n", __func__);
		pthread_create(&_mii_ssc_thread_id, NULL, _mii_ssc_thread, NULL);
	} else {
		_mii_ssc_thread_signal(c);
	}
}

/*
 * This is called when the CPU touches the CX00-CXFF ROM area, and we
 * need to install the secondary part of the ROM.
 * This is what the hardware does, and that ROM gets 'reset' when something
 * touches $cfff. (see _mii_deselect_cXrom())
 *
 * Also, the other key thing here is that if we detect the PC is in the the main
 * part of our ROM code, we start the card as there's no other way to know when
 * the card is started -- we don't want to create a thread for SSC if the card
 * is installed but never used.
 *
 * This seems to work pretty well, it handles most programs that use the SSC
 * that I have tried.
 */
static bool
_mii_ssc_select(
		struct mii_bank_t *bank,
		void *param,
		uint16_t addr,
		uint8_t * byte,
		bool write)
{
	if (bank == NULL)	// this is normal, called on dispose
		return false;
	mii_card_ssc_t *c = param;
	if (c->slot->aux_rom_selected)
		return false;
	uint16_t pc = c->mii->cpu.PC;
	printf("SSC%d SELECT auxrom PC:%04x\n", c->slot->id+1, pc);
	/* Supports when the ROM starts prodding into the ROM */
	if (c->state != MII_SSC_STATE_RUNNING) {
		printf("SSC%d: start card from ROM poke? (PC $%04x)?\n",
				c->slot->id+1, pc);
		if ((pc & 0xff00) == (0xc100 + (c->slot->id << 8)) ||
				(pc >> 12) >= 0xc) {
			_mii_scc_set_conf(c, &c->conf, 1);
			_mii_ssc_thread_start(c);
		}
	}
	mii_bank_write(c->rom, 0xc800, c->rom_ssc->rom, 2048);
	c->slot->aux_rom_selected = true;
	return false;
}

/*
 * Called at some sort of proportional number of cycles related to the baudrate
 * to check the FIFOs and update the status/raise IRQs. this doesn't have to be
 * exact, it just have to be often enough to not miss any data.
 */
static uint64_t
_mii_ssc_timer_cb(
		mii_t * mii,
		void * param )
{
	mii_card_ssc_t *c = param;
	// stop timer
	if (c->state != MII_SSC_STATE_RUNNING)
		return 0;

	// check the FIFOs -- not technically 'true' we raise an IRQ as soon as
	// theres some bytes to proceed, but we'll do it here for simplicity
	uint8_t rx_full = !mii_ssc_fifo_isempty(&c->rx);
	// what it really mean is 'there room for more data', not 'full'
	uint8_t tx_empty = !mii_ssc_fifo_isfull(&c->tx);
//	uint8_t old = c->status;
	c->status = (c->status & ~(1 << SSC_6551_RX_FULL)) |
					(rx_full << SSC_6551_RX_FULL);
	c->status = (c->status & ~(1 << SSC_6551_TX_EMPTY)) |
					(tx_empty << SSC_6551_TX_EMPTY);
	uint8_t irq = 0;//(c->status & (1 << SSC_6551_IRQ));
	uint8_t t_irqen = ((c->command >> SSC_6551_COMMAND_IRQ_T) & 3) == 1;
	uint8_t r_irqen = !(c->command & (1 << SSC_6551_COMMAND_IRQ_R));
#if 0
	if (old != c->status)
		printf("SSC%d New Status %08b RX:%2d TX:%2d t_irqen:%d r_irqen:%d\n",
			c->slot->id+1, c->status,
			mii_ssc_fifo_get_read_size(&c->rx), mii_ssc_fifo_get_write_size(&c->tx),
			t_irqen, r_irqen);
#endif
	// we set the IRQ flag even if the real IRQs are disabled.
	// rising edge triggers the IRQR
	if (!irq && rx_full) {
		// raise the IRQ
		if (r_irqen) {
			irq = 1;
		//	printf("SSC%d: IRQ RX\n", c->slot->id+1);
		}
	}
	if (!irq && (tx_empty)) {
		// raise the IRQ
		if (t_irqen) {
			irq = 1;
		//	printf("SSC%d: IRQ TX\n", c->slot->id+1);
		}
	}
	if (irq) {
		c->status |= 1 << SSC_6551_IRQ;
		mii_irq_raise(mii, c->irq_num);
	}
	return c->timer_delay;
}

static int
_mii_scc_set_conf(
		mii_card_ssc_t *c,
		const mii_ssc_setconf_t *conf,
		int re_open)
{
	if (conf == NULL)
		conf = &_mii_ssc_default_conf;

	if (!re_open && strcmp(c->conf.device, conf->device) == 0 &&
			c->conf.baud == conf->baud &&
			c->conf.bits == conf->bits &&
			c->conf.parity == conf->parity &&
			c->conf.stop == conf->stop &&
			c->conf.handshake == conf->handshake &&
			c->conf.is_device == conf->is_device &&
			c->conf.is_socket == conf->is_socket &&
			c->conf.is_pty == conf->is_pty &&
			c->conf.socket_port == conf->socket_port)
		return 0;
	if (re_open || strcmp(c->conf.device, conf->device) != 0) {
		if (c->tty_fd > 0) {
			int tty = c->tty_fd;
			c->tty_fd = -1;
			// this will wake up the thread as well
			close(tty);
		}
	}
	c->conf = *conf;
	strncpy(c->tty_path, conf->device, sizeof(c->tty_path) - 1);
	c->tty_path[sizeof(c->tty_path) - 1] = 0; 
	if (c->tty_fd < 0) {
		int new_fd = -1;
		if (conf->is_pty) {
			int master = 0;
			int res = openpty(&master, &new_fd, c->tty_path, NULL, NULL);
			if (res < 0) {
				printf("SSC%d openpty: %s\n", c->slot->id+1, strerror(errno));
				return -1;
			}
		} else {
			int res = open(c->tty_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
			if (res < 0) {
				printf("SSC%d open(%s): %s\n", c->slot->id+1,
						c->tty_path, strerror(errno));
				return -1;
			}
			new_fd = res;
		}
		// set non-blocking mode
		int flags = fcntl(new_fd, F_GETFL, 0);
		fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);
		c->tty_fd = new_fd;
	}
	if (c->tty_fd < 0) {
		printf("SSC%d: %s TTY not open, skip\n", c->slot->id+1, __func__);
		return -1;
	}
	// get current terminal settings
	struct termios tio;
	tcgetattr(c->tty_fd, &tio);
	// set raw mode
	cfmakeraw(&tio);
	c->human_config[0] = 0;
	// set speed
	for (int i = 0; i < 16; i++) {
		if (_mii_ssc_to_baud_rate[i] == conf->baud) {
			c->dipsw1 = 0x80 | i;
			cfsetospeed(&tio, _mii_ssc_to_baud[i]);
			cfsetispeed(&tio, _mii_ssc_to_baud[i]);
			sprintf(c->human_config, "Baud:%d ", conf->baud);
			break;
		}
	}
	// set 8N1
	tio.c_cflag = (tio.c_cflag & ~PARENB) | _mii_ssc_to_parity[conf->parity];
	tio.c_cflag = (tio.c_cflag & ~CSTOPB) | _mii_ssc_to_stop[conf->stop];
	tio.c_cflag = (tio.c_cflag & ~CSIZE);

	tio.c_cflag |= _mii_ssc_to_bits[conf->bits];
	sprintf(c->human_config + strlen(c->human_config), "%d",
			_mii_scc_to_bits_count[conf->bits]);
	static const char *parity = "noeb";
	sprintf(c->human_config + strlen(c->human_config), "%c%c",
			parity[conf->parity], conf->stop ? '2' : '1');

	// Hardware Handshake
	tio.c_cflag = (tio.c_cflag & ~CRTSCTS) | (conf->handshake ? CRTSCTS : 0);
	// set the new settings
	tcsetattr(c->tty_fd, TCSANOW, &tio);
	c->dipsw1 = 0x80 | conf->baud;
	c->dipsw2 = SSC_SW2_IRQEN;
	c->control = 0;
	mii_ssc_fifo_reset(&c->rx);
	mii_ssc_fifo_reset(&c->tx);
	return 0;
}

static int
_mii_ssc_init(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	mii_card_ssc_t *c = calloc(1, sizeof(*c));
	c->slot = slot;
	slot->drv_priv = c;
	c->mii = mii;

	c->slot_offset = slot->id + 1 + 0xc0;

	uint16_t addr = 0xc100 + (slot->id * 0x100);
	c->rom = &mii->bank[MII_BANK_CARD_ROM];
	c->rom_ssc = mii_rom_get("ssc");
	mii_bank_write(c->rom, addr, c->rom_ssc->rom + 7*256, 256);
	/*
	 * install a callback that will be called for every access to the
	 * ROM area, we need this to re-install the secondary part of the ROM
	 * when the card 'slot' rom is accessed.
	 */
	mii_bank_install_access_cb(c->rom,
			_mii_ssc_select, c, addr >> 8, addr >> 8);

	/*
	 * And this is the timer that will check the status of FIFOs and update
	 * the status of the card, and raise IRQs if needed
	 */
	char name[32];
	snprintf(name, sizeof(name), "SSC %d", slot->id+1);
	c->timer_check = mii_timer_register(mii,
							_mii_ssc_timer_cb, c, 0, strdup(name));
	c->irq_num = mii_irq_register(mii, strdup(name));

	// this is semi random for now, it is recalculated once the program
	// changes the baud rate/config
	c->timer_delay = 11520;
	c->tty_fd = -1;
	STAILQ_INSERT_TAIL(&_mii_card_ssc_slots, c, self);

	c->dipsw1 	= 0x80 | 14;		// communication mode, 9600
	// in case progs read that to decide to use IRQs or not
	c->dipsw2 	= SSC_SW2_IRQEN;
	c->state 	= MII_SSC_STATE_INIT;
	c->status 	= SSC_6551_STATUS_RESET;
	c->command 	= SSC_6551_COMMAND_RESET;
	c->control 	= 0;
	_mii_scc_set_conf(c, &c->conf, 1);

	return 0;
}

static void
_mii_ssc_dispose(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	mii_card_ssc_t *c = slot->drv_priv;

	STAILQ_REMOVE(&_mii_card_ssc_slots, c, mii_card_ssc_t, self);
	if (c->state == MII_SSC_STATE_RUNNING) {
		mii_ssc_cmd_t cmd = { .cmd = MII_SSC_STATE_STOP, .card = c };
		mii_ssc_cmd_fifo_write(&_mii_ssc_cmd, cmd);
		_mii_ssc_thread_signal(c);
		while (c->state == MII_SSC_STATE_RUNNING)
			usleep(1000);
		printf("SSC%d: stopped\n", c->slot->id+1);
	}
	if (STAILQ_FIRST(&_mii_card_ssc_slots) == NULL && _mii_ssc_thread_id) {
		printf("SSC%d: stopping thread\n", c->slot->id+1);
		pthread_t id = _mii_ssc_thread_id;
		_mii_ssc_thread_id = 0;
		mii_ssc_cmd_t cmd = { .cmd = MII_THREAD_TERMINATE };
		mii_ssc_cmd_fifo_write(&_mii_ssc_cmd, cmd);
		_mii_ssc_thread_signal(c);
		pthread_join(id, NULL);
		printf("SSC%d: thread stopped\n", c->slot->id+1);
	}
	mii_irq_unregister(mii, c->irq_num);
	free(c);
	slot->drv_priv = NULL;
}

static void
_mii_ssc_command_set(
		mii_card_ssc_t *c,
		uint8_t byte)
{
	mii_t * mii = c->mii;
	if (!(c->command & (1 << SSC_6551_COMMAND_DTR)) &&
			(byte & (1 << SSC_6551_COMMAND_DTR))) {
		_mii_scc_set_conf(c, &c->conf, 1);
		_mii_ssc_thread_start(c);
	}
	if (c->tty_fd < 0) {
		printf("SSC%d: %s TTY not open, skip\n", c->slot->id+1, __func__);
		return;
	}
	/* This triggers the IRQ if it enabled when there is a IRQ flag on,
	 * this make it behave more like a 'level' IRQ instead of an edge IRQ
	 */
	if ((c->command & (1 << SSC_6551_COMMAND_IRQ_R)) &&
			!(byte & (1 << SSC_6551_COMMAND_IRQ_R))) {
		if (c->status & (1 << SSC_6551_IRQ))
			mii_irq_raise(mii, c->irq_num);
	}
	int status;
	if (ioctl(c->tty_fd, TIOCMGET, &status) == -1) {
		printf("SSC%d: DTR/RTS: %s\n", c->slot->id+1, strerror(errno));
	}
	int old = status;
	status = (status & ~TIOCM_DTR) |
				((byte & (1 << SSC_6551_COMMAND_DTR)) ? TIOCM_DTR : 0);
	switch ((byte >> SSC_6551_COMMAND_IRQ_T) & 3) {
		case 0:	// IRQ_TX=0 + RTS=1
			status |= TIOCM_RTS;
			break;
		case 1:	// IRQ_TX=1 + RTS=0
			status &= ~TIOCM_RTS;
			break;
		case 2:	// IRQ_TX=0 + RTS=0
			status &= ~TIOCM_RTS;
			break;
		case 3:	// IRQ_TX=0 + RTS=0 + BRK
			status &= ~TIOCM_RTS;
			break;
	}
	if (old != status) {
		printf("%s%d: $%04x DTR %d RTS %d\n", __func__, c->slot->id+1,
				c->mii->cpu.PC,
				(status & TIOCM_DTR) ? 1 : 0,
				(status & TIOCM_RTS) ? 1 : 0);	// 0=on, 1=off
		if (ioctl(c->tty_fd, TIOCMSET, &status) == -1) {
			printf("SSC%d: DTR/RTS: %s\n", c->slot->id+1, strerror(errno));
		}
	}
	c->command = byte;
}

static uint8_t
_mii_ssc_access(
		mii_t * mii,
		struct mii_slot_t *slot,
		uint16_t addr,
		uint8_t byte,
		bool write)
{
	mii_card_ssc_t *c = slot->drv_priv;
	uint8_t res = 0;
	int psw = addr & 0x0F;

	switch (psw) {
		case 0x1: // DIPSW1
			if (!write) {
				printf("%s%d: $%04x read DIPSW1 : %02x\n",
						__func__, slot->id+1, mii->cpu.PC, c->dipsw1);
				res = c->dipsw1;
				/* this handle access by the ROM via PR#x and IN#x */
				if (c->state == MII_SSC_STATE_INIT &&
						(mii->cpu.PC & 0xff00) == 0xcb00)
					_mii_ssc_thread_start(c);
			}
			break;
		case 0x2: // DIPSW2
			if (!write) {
				printf("%s%d: $%04x read DIPSW2 : %02x\n",
						__func__, slot->id+1, mii->cpu.PC, c->dipsw2);
				res = c->dipsw2;
			}
			break;
		case 0x8: { // TD/RD
			if (c->state != MII_SSC_STATE_RUNNING)
				break;
			if (write) {
				bool tx_empty = mii_ssc_fifo_isempty(&c->tx);
			//	printf("%s: write %02x '%c'\n", __func__,
			//			byte, byte <= ' ' ? '.' : byte);
				c->total_tx++;
				mii_ssc_fifo_write(&c->tx, byte);
				if (tx_empty)	// wake thread if it's sleeping
					_mii_ssc_thread_signal(c);
				bool isfull = mii_ssc_fifo_isfull(&c->tx);
				if (isfull) {
					c->status &= ~(1 << SSC_6551_TX_EMPTY);
				}
			} else {
				if (mii_ssc_fifo_isempty(&c->rx)) {
					res = 0;
				} else {
					c->total_rx++;
					bool wasfull = mii_ssc_fifo_isfull(&c->rx);
					res = mii_ssc_fifo_read(&c->rx);
					bool isempty = mii_ssc_fifo_isempty(&c->rx);
					if (isempty) {
						c->status &= ~(1 << SSC_6551_RX_FULL);
					} else {
						if (wasfull)	// wake thread to read more
							_mii_ssc_thread_signal(c);
						// send another irq?
						uint8_t r_irqen =
									!(c->command & (1 << SSC_6551_COMMAND_IRQ_R));
						if (r_irqen) {
							mii_irq_raise(mii, c->irq_num);
						}
					}
				}
			}
		}	break;
		case 0x9: {// STATUS
			if (write) {
				printf("SSC%d: RESET request\n", c->slot->id+1);
				_mii_ssc_command_set(c, 0x10);
				break;
			}
			res = c->status;
			// if it was set before, clear it.
			c->status &= ~(1 << SSC_6551_IRQ);
			mii_irq_clear(mii, c->irq_num);
		}	break;
		case 0xa: {// COMMAND
			if (!write) {
				res = c->command;
				break;
			}
			_mii_ssc_command_set(c, byte);
		}	break;
		case 0xb: { // CONTROL
			if (!write) {
				res = c->control;
				break;
			}
			c->control = byte;
			struct termios tio;
			tcgetattr(c->tty_fd, &tio);
			// Update speed
			int baud = _mii_ssc_to_baud[c->control & 0x0F];
			cfsetospeed(&tio, baud);
			cfsetispeed(&tio, baud);
			tio.c_cflag &= ~(CSTOPB|CSIZE|PARENB|PARODD);
			// Update stop bits bit 7: 0 = 1 stop bit, 1 = 2 stop bits
			tio.c_cflag |= _mii_ssc_to_stop[(c->control >> 7) & 1];
			// Update data bits bit 5-6 0=8 bits, 1=7 bits, 2=6 bits, 3=5 bits
			tio.c_cflag |= _mii_ssc_to_bits[(c->control >> 6) & 3];
			// parity are in c->command, bits 5-7,
			// 0=None, 1=Odd, 2=Even, 3=Mark, 4=Space
			tio.c_cflag |= _mii_ssc_to_parity[(c->command >> 5) & 3];
			tcsetattr(c->tty_fd, TCSANOW, &tio);
			int framesize = 1 + (((c->control >> 6) & 3)) +
								((c->control >> 7) & 1 ? 2 : 1) +
								_mii_scc_to_bits_count[(c->control >> 5) & 3] +
								(((c->command >> 5) & 3) ? 1 : 0);
			// recalculate the timer delay between characters
			float cps = (float)_mii_ssc_to_baud_rate[c->control & 0x0F] / framesize;
			c->timer_delay = (1000000.0 * mii->speed) / cps;

			printf("SSC%d: baud:%5d stop:%d data:%d parity:%d (total %d)\n",
					c->slot->id+1,
					_mii_ssc_to_baud_rate[c->control & 0x0F],
					(c->control >> 7) & 1 ? 2 : 1,
					_mii_scc_to_bits_count[(c->control >> 5) & 3],
					(c->command >> 5) & 3,
					framesize);
//			printf("SSC%d: cps %.2f timer cycles %u\n",
//					c->slot->id+1, cps, c->timer_delay);
			// update the timer if it is too far in the future
			if (mii_timer_get(mii, c->timer_check) > c->timer_delay) {
				mii_timer_set(mii, c->timer_check, c->timer_delay);
			}
		}	break;
		default:
		//	printf("%s PC:%04x addr %04x %02x wr:%d\n", __func__,
		//			mii->cpu.PC, addr, byte, write);
			break;
	}
	return res;
}

static int
_mii_ssc_command(
		mii_t * mii,
		struct mii_slot_t *slot,
		uint32_t cmd,
		void * param)
{
//	mii_card_ssc_t *c = slot->drv_priv;
	int res = -1;
	switch (cmd) {
		case MII_SLOT_SSC_SET_TTY: {
			const mii_ssc_setconf_t * conf = param;
			mii_card_ssc_t *c = slot->drv_priv;
			res = _mii_scc_set_conf(c, conf, 0);
			printf("SSC%d: set tty %s: %s\n",
					slot->id+1, conf->device, c->human_config);
		}	break;
		case MII_SLOT_SSC_GET_TTY: {
			mii_ssc_setconf_t * conf = param;
			mii_card_ssc_t *c = slot->drv_priv;
			*conf = c->conf;
			res = 0;
		}	break;
	}
	return res;
}

static mii_slot_drv_t _driver = {
	.name = "ssc",
	.desc = "Super Serial card",
	.init = _mii_ssc_init,
	.dispose = _mii_ssc_dispose,
	.access = _mii_ssc_access,
	.command = _mii_ssc_command,
};
MI_DRIVER_REGISTER(_driver);


#include "mish.h"

static void
_mii_mish_ssc(
		void * param,
		int argc,
		const char * argv[])
{
	if (!argv[1] || !strcmp(argv[1], "status")) {
		mii_card_ssc_t *c;
		printf("SSC: cards:\n");
		STAILQ_FOREACH(c, &_mii_card_ssc_slots, self) {
			printf("SSC %d: %s FD: %2d path:%s %s\n", c->slot->id+1,
					c->state == MII_SSC_STATE_RUNNING ? "running" : "stopped",
					c->tty_fd, c->tty_path, c->human_config);
			// print FIFO status, fd status, registers etc
			printf("  RX: %2d/%2d TX: %2d/%2d -- total rx:%6d tx:%6d\n",
					mii_ssc_fifo_get_read_size(&c->rx),
					mii_ssc_fifo_get_write_size(&c->rx),
					mii_ssc_fifo_get_read_size(&c->tx),
					mii_ssc_fifo_get_write_size(&c->tx),
					c->total_rx, c->total_tx);
			printf("  DIPSW1: %08b DIPSW2: %08b\n", c->dipsw1, c->dipsw2);
			printf("  CONTROL: %08b COMMAND: %08b STATUS: %08b\n",
					c->control, c->command, c->status);
		}
		return;
	}
}

MISH_CMD_NAMES(_ssc, "ssc");
MISH_CMD_HELP(_ssc,
		"ssc: Super Serial internals",
		" <default>: dump status"
		);
MII_MISH(_ssc, _mii_mish_ssc);
