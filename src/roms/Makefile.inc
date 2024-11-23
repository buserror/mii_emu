

# ROMS: This bits convert the binary roms with the product number in the name
# to a C header file that can be included in the project. I used to use
# incbin.h for this, but there were issues with some of the linker used, for
# example, webassembly, so we now use xxd to generate the .h file.
# You'd NEED the xxd tool if you want to re-generate these files, but well,
# they've been constant for over 40 years, my guess is that they ain't going
# anywhere.
#
# The reason these roms are used like this here is that I always found that it
# was a massive pain to have to deal with the roms in *any* apple II (or Mac)
# emulator. You had to find the roms, put them in the right place, name them
# correctly, and then, if you wanted to use a different version, you had to
# rename them, and so on. Dreadful.
# I think it prevents a lot of people learning about the Apple II, because it's
# just too much hassle to get started. So, I've included the roms in the source
# code, and they are compiled in the binary. For the user. Not for convenience,
# Not for 'stealing' from apple, but for the user. For the user to have a
# seamless experience. To be able to just run the emulator, and have it work.
# And be amazed *at the brand*.
#
# Now, I understand that strictly speaking these are copyrighted material, but
# they are so old, and so widely available, and are used here for educational
# purposes, and with the upmost respect for all the original authors, and for
# what 'the brand' represented for us at the time. With that in mind, I think
# that there shouldn't be an issue. But if you, Mr&Mrs Apple Lawyer think
# otherwise, please let me know, I'll remove them. Reluctantly. I'll cry&scream!
.PHONY : roms

define rom_to_c
$(1) :
	@if [ ! -f $$@ ]; then \
		echo "ROM file $$@ not found, relying on the exiting .h"; \
		touch $$@; \
	fi
src/roms/mii_rom_$(2).c : $(1) utils/convert-rom-tcc.c
	if [ ! -s "$$<" ]; then \
		touch $$@; \
	else { \
		tcc -run utils/convert-rom-tcc.c -o "$$@" -i "$$<" \
				-n $(2) -c $(3) -d $(4); \
		echo "ROM $$@ Generated"; \
	} fi
roms:: src/roms/mii_rom_$(2).c
endef


# 38063e08c778503fc03ecebb979769e9  contrib/mii_rom_iiee_3420349b.bin
$(eval $(call rom_to_c,contrib/mii_rom_iiee_3420349b.bin,iiee,main,\
			"Apple IIe ROM"))
# 9123fff3442c0e688cc6816be88dd4ab  contrib/mii_rom_iiee_video_3420265a.bin
$(eval $(call rom_to_c,contrib/mii_rom_iiee_video_3420265a.bin,iiee_video,video,\
			"Apple IIe Video ROM"))
# e0d67bb1aabe2030547b4cbdf3905b60  contrib/mii_rom_iic_3420033a.bin
$(eval $(call rom_to_c,contrib/mii_rom_iic_3420033a.bin,iic,main,\
			"Apple IIc ROM"))
# 67c0d61ab0911183faf05270f881a97e  contrib/mii_rom_ssc_3410065a.bin
$(eval $(call rom_to_c,contrib/mii_rom_ssc_3410065a.bin,ssc,ssc,\
			"Super Serial Card ROM"))
# 9123fff3442c0e688cc6816be88dd4ab  contrib/mii_rom_iic_video_3410265a.bin
$(eval $(call rom_to_c,contrib/mii_rom_iic_video_3410265a.bin,iic_video,video,\
			"Apple IIc Video ROM"))
# 2020aa1413ff77fe29353f3ee72dc295  contrib/mii_rom_disk2_p5_3410037.bin
$(eval $(call rom_to_c,contrib/mii_rom_disk2_p5_3410037.bin,disk2_p5,disk2_p5,\
			"Disk II Card ROM"))
# The original of this file online is 16KB and is marked as IIe/IIc. The first
# 8KB are empty (0xff) so I trimmed that off.
# a7d723b93d09f776a433fee4d31e1d0d  contrib/mii_rom_iiee_video_fr_342274a.bin
$(eval $(call rom_to_c,contrib/mii_rom_iiee_video_fr_342274a.bin,iiee_video_fr,video,\
			"Apple IIe Video ROM FR"))
# 45a0fe6d800273ec9cef4fc23da7f25f  contrib/mii_rom_iiee_video_uk_3420273a.bin
$(eval $(call rom_to_c,contrib/mii_rom_iiee_video_uk_3420273a.bin,iiee_video_uk,video,\
			"Apple IIe Video ROM UK"))
# This is the ROM file for the EEPROM card, with some games too...
$(eval $(call rom_to_c,disks/GamesWithFirmware.po,epromcard,epromcard,\
			"EEPROM Card ROM"))
# And the smartport driver
$(eval $(call rom_to_c,test/asm/mii_smartport_driver.bin,smartport,smartport,\
			"Smartport Driver ROM"))


