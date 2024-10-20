
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mii.h"
#include "mockingboard.h"
#include "mii_audio.h"

typedef struct mii_mb_t {
	mii_t *				mii;
	uint8_t	 			init; 		// init sequence bits
	bool 				init_done;
	uint8_t 			timer;
	uint8_t 			irq_num;
	struct mb_t *		mb;
	mii_audio_source_t	source;
	uint64_t 			flush_cycle_count;
	uint64_t			last_flush_cycle;
} mii_mb_t;


static uint64_t
_mii_mb_timer(
		mii_t * mii,
		void * param )
{
	mii_mb_t * mb = param;
	mb_clock_t clock = {
		.ref_step = MB_CLOCKS_PHI0_CYCLE,
		.ts = mii->cpu.total_cycle * MB_CLOCKS_14MHZ_CYCLE,
	};
	uint64_t res = 1 + -mii_timer_get(mii, mb->timer);

	uint32_t irq = mb_io_sync(mb->mb, &clock);
	if (irq & MB_CARD_IRQ)
		mii->cpu_state.irq = 1;

	if ((mii->cpu.total_cycle - mb->last_flush_cycle) >= mb->flush_cycle_count) {
		mb->last_flush_cycle = mii->cpu.total_cycle;

		static float audio[1224] = {};
		memset(audio, 0, sizeof(audio));
		int r = mb_ay3_render(mb->mb, audio, 1024, 2, MII_AUDIO_FREQ);
		float min = 1e6, max = -1e6;
		mii_audio_frame_t *f = &mb->source.fifo;
		for (int i = 0; i < r * 2; i += 2) {
			mii_audio_sample_t s = (audio[i] + audio[i + 1]);
//			mii_audio_sample_t s = (audio[i]);
			if (s < min)
				min = s;
			if (s > max)
				max = s;
			mii_audio_frame_write(f, s);
		}
		printf("MB Audio cycle %ld r=%d min %.4f max %.4f\n",
				mb->flush_cycle_count, r, min, max);
	}
	return res;
}

static void
mii_mb_start(
		mii_mb_t *mb)
{
//	mb_io_reset(mb->mb, &clock);
	printf("MB Start\n");
	mb->init = 0;
	mb->init_done = true;
	mb->flush_cycle_count = 512 * mb->mii->audio.clk_per_sample;
	mii_audio_add_source(&mb->mii->audio, &mb->source);
}

/*
 * this is a protothread, so remember no locals that will survive a yield()
 */
static bool
_mii_mb_romspace_access(
		struct mii_bank_t *bank,
		void *param,
		uint16_t addr,
		uint8_t * byte,
		bool write)
{
	if (!bank) {	// TODO: dispose
		printf("%s: no bank\n", __func__);
		return false;
	}
	mii_mb_t * mb = param;
//	addr &= 0xff;
//	if (mb->init_done)
	switch (addr & 0xff) {
		case MB_CARD_MOCKINGBOARD_ORB1 ... MB_CARD_MOCKINGBOARD_ORB1+0xf:
		case MB_CARD_MOCKINGBOARD_ORB2 ... MB_CARD_MOCKINGBOARD_ORB2+0xf:
			if (write) {
				/*
				 * Once code has writen to the first registers to initialize them,
				 * the mockingboard is considered initialized.
				 * At that point we take over the 'reading' of these addresees,
				 * so the CARD rom (the mouse) can't read them anymore.
				 * It *still* should be OK as these address do not overlap the
				 * mouse ROM.
				 */
				if ((addr & 0x7f) == 2 && *byte == 0xff) {
					if (!mb->init_done)
						mii_mb_start(mb);
				}
			//	printf("%s: %s addr %04x byte %02x write %d\n", __func__, bank->name, addr, *byte, write);
				mb_io_write(mb->mb, *byte, addr & 0xff);
			} else if (mb->init_done) {
				mb_io_read(mb->mb, byte, addr & 0xff);
			}
		//	printf("%s: %s addr %04x byte %02x write %d\n", __func__,
		//			bank->name, addr, *byte, write);
			return mb->init_done;
			break;
		default:
			if (!write)
				*byte = mii_video_get_vapor(mb->mii);
			break;
	}
	return false;
}

static int
_mii_mb_probe(
		mii_t *mii,
		uint32_t flags)
{
	printf("%s %s\n", __func__, flags & MII_INIT_MOCKINGBOARD ?
			"enabled" : "disabled");
//	if (!(flags & MII_INIT_MOCKINGBOARD))
//		return 0;
	return 1;
}


static int
_mii_mb_init(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	printf("%s\n", __func__);
	mii_mb_t * mb = calloc(1, sizeof(*mb));
	slot->drv_priv = mb;
	mb->mii = mii;
	mb->mb = mb_alloc();
	mb_clock_t clock = {
			.ref_step = 1,
			.ts = mii->cpu.total_cycle,
	};
	mb_io_reset(mb->mb, &clock);

	char name[32];
	snprintf(name, sizeof(name), "MB %d", slot->id+1);

	mb->timer = mii_timer_register(mb->mii, _mii_mb_timer, mb, 1000, name);
	mb->irq_num = mii_irq_register(mb->mii, name);

	uint16_t addr = 0xc100 + (slot->id * 0x100);
	mii_mb_start(mb);
	mii_bank_install_access_cb(&mii->bank[MII_BANK_CARD_ROM],
			_mii_mb_romspace_access, mb, addr >> 8, 0);
	return 0;
}


static void
_mii_mb_reset(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	mii_mb_t *mb = slot->drv_priv;
	printf("%s\n", __func__);
	mb_clock_t clock = {
			.ref_step = 1,
			.ts = mii->cpu.total_cycle,
	};
	mb_io_reset(mb->mb, &clock);
}

static uint8_t
_mii_mb_iospace_access(
	mii_t * mii, struct mii_slot_t *slot,
	uint16_t addr, uint8_t byte, bool write)
{
//	mii_mb_t *mb = slot->drv_priv;
	if (!write)
		byte = mii_video_get_vapor(mii);
	return byte;
}

static mii_slot_drv_t _driver = {
	.name = "mockingboard",
	.desc = "Mockingboard",
//	.enable_flag = MII_INIT_MOCKINGBOARD,
	.init = _mii_mb_init,
	.reset = _mii_mb_reset,
	.access = _mii_mb_iospace_access,
//	.probe = _mii_mb_probe,
};
MI_DRIVER_REGISTER(_driver);

