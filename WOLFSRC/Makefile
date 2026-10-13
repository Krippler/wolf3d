################################################################
#
# Wolfenstein 3-D for 64-bit Linux
#
# Builds an engine for each game, because the chunk numbers of the
# graphics, sounds and music are compiled in and every game's differ:
#
#	linux/wolf3d		the registered game, *.WL6 (or the 3-episode *.WL3)
#	linux/wolf3d-wl1	the shareware episode, *.WL1, version 1.4
#	linux/wolf3d-sod	Spear of Destiny, *.SOD
#	linux/wolf3d-sdm	Spear of Destiny's two-level demo, *.SDM
#
# The original was built by Borland C++ 3.1 from WOLF3D.PRJ; those files are
# left as they were. PULSE=0 leaves out PulseAudio, for a build that only
# ever serves its sound over TCP (the container's) or has none.
#
################################################################

CC		?= gcc
PULSE	?= 1

# -fwrapv: int arithmetic wraps, as it did on the 8086, and the game relies
# on it. -fno-strict-aliasing: the game reads its data files through
# pointers of whatever type suits. -std=gnu17: C23, gcc 15's default, makes
# false and true keywords, and the game declares them itself; and the
# 1992 code's loose function pointer types stay warnings, as in gcc 13,
# rather than the errors gcc 14 makes them.
CFLAGS	?= -O2 -g
CFLAGS	+= -std=gnu17 -fwrapv -fno-strict-aliasing -fcommon \
		   -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
		   -Wno-error=implicit-function-declaration -Wno-incompatible-pointer-types \
		   -Wall -Wno-unused -Wno-parentheses -Wno-missing-braces \
		   -Wno-pointer-sign -Wno-char-subscripts -Wno-switch \
		   -Wno-implicit-int -Wno-dangling-else -Wno-format-overflow \
		   -Wno-stringop-truncation -Wno-format-truncation \
		   -Wno-unknown-pragmas -Wno-unused-result -Wno-maybe-uninitialized
CPPFLAGS += -I.
LDLIBS	= -lX11 -lXext -lpthread -lm

ifeq ($(PULSE),1)
CPPFLAGS += -DWOLF_PULSE
LDLIBS	+= -lpulse-simple -lpulse
endif

O		= linux

GAME	= WL_ACT1 WL_ACT2 WL_AGENT WL_DEBUG WL_DRAW WL_GAME WL_INTER \
		  WL_MAIN WL_MENU WL_PLAY WL_SCALE WL_STATE \
		  ID_CA ID_IN ID_MM ID_PM ID_SD ID_US_1 ID_US_A ID_VH ID_VL
SYS		= sys_x11 sys_port sys_audio sys_pad sys_settings

# The help screens and the end text: Spear of Destiny had neither, and its
# project left WL_TEXT.C out
TEXT_wl6 = WL_TEXT
TEXT_wl1 = WL_TEXT
TEXT_sod =
TEXT_sdm =
DATA	= gamepal signon

# Each game: its directory under $(O), its define, and its engine's name
VARIANTS = wl6 wl1 sod sdm
DEF_wl6	=
DEF_wl1	= -DWOLF_WL1
DEF_sod	= -DWOLF_SOD
DEF_sdm	= -DWOLF_SDM
BIN_wl6	= wolf3d
BIN_wl1	= wolf3d-wl1
BIN_sod	= wolf3d-sod
BIN_sdm	= wolf3d-sdm

all: $(foreach v,$(VARIANTS),$(O)/$(BIN_$(v)))

$(O)/opl3.o: opl/opl3.c opl/opl3.h | $(O)
	$(CC) $(CFLAGS) -w -c $< -o $@

$(O)/data/%.o: $(O)/data/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# The palette and the signon screen, out of the object files they shipped as
$(O)/data/gamepal.c: OBJ/GAMEPAL.OBJ $(O)/obj2c | $(O)/data
	$(O)/obj2c $< gamepal > $@

$(O)/data/signon.c: OBJ/SIGNON.OBJ $(O)/obj2c | $(O)/data
	$(O)/obj2c $< signon > $@

$(O)/obj2c: tools/obj2c.c | $(O)
	$(CC) -O2 -o $@ $<

#
# The engine, once for each game
#
define variant
$(O)/$(1)/%.o: %.C *.H id_port.h | $(O)/$(1)
	$$(CC) $$(CPPFLAGS) $(DEF_$(1)) $$(CFLAGS) -x c -c $$< -o $$@

$(O)/$(1)/%.o: %.c *.H id_port.h | $(O)/$(1)
	$$(CC) $$(CPPFLAGS) $(DEF_$(1)) $$(CFLAGS) -c $$< -o $$@

# Every actor state, for the savegames: see tools/mkstates.sh
$(O)/$(1)/states.c: WL_*.C *.H tools/mkstates.sh | $(O)/$(1)
	sh tools/mkstates.sh $$(CC) $$(CPPFLAGS) $(DEF_$(1)) > $$@

$(O)/$(1)/states.o: $(O)/$(1)/states.c
	$$(CC) $$(CFLAGS) -c $$< -o $$@

$(O)/$(BIN_$(1)): $(addprefix $(O)/$(1)/,$(addsuffix .o,$(GAME) $(TEXT_$(1)) $(SYS) states)) \
		$(O)/opl3.o $(addprefix $(O)/data/,$(addsuffix .o,$(DATA)))
	$$(CC) $$(CFLAGS) -o $$@ $$^ $$(LDLIBS)

$(O)/$(1):
	mkdir -p $$@
endef

$(foreach v,$(VARIANTS),$(eval $(call variant,$(v))))

$(O) $(O)/data:
	mkdir -p $@

clean:
	rm -rf $(O)

.PHONY: all clean
.SECONDARY:
