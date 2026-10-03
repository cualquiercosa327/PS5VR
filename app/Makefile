# PS5VR: object build only. scripts/build.sh drives this inside the engine's
# toolchain image and does the link, PS5 conversion and signing.
#
#   make objects        compile everything into build/obj
#   make print-objects  list the objects, for the link step

CC  ?= prospero-clang
CXX ?= prospero-clang++
TFLAGS ?=
HB ?= /opt/ps5-payload-sdk/target/user/homebrew

# The PS5VR build (scripts/build.sh with APP_VR=1) keeps its own objects.
OBJDIR ?= build/obj
# PS5VR: the VR code paths, and the hardware decoder up to 8K (8192x4352).
EXTRA_DEFS ?= -DAPP_VR=1 -DENG_VDEC_NATIVE_MAX_W=8192 -DENG_VDEC_NATIVE_MAX_H=4352

# ENG_* switches select the engine's app-module paths (sceAgc present,
# sceVideodec2 incl. 10-bit). APP_APP marks the places this app changed.
DEFS := -DAPP_APP=1 -DENG_APP_MODULE=1 -DENG_AGC_DEVICE=1 -DENG_AGC_HAVE_VIDEO_PIPES=1 -DENG_TARGET_PS5=1 \
        -DENG_VDEC_NATIVE_10BIT=1 -DENG_APP_EDITION=1 -DPP_4K_PRODUCT_STAGE=4 \
        -DENG_PLAYER_VERSION=\"ps5vr\" $(EXTRA_DEFS)

INCS := -Isrc -Isrc/shim -Ithird_party \
        -Iengine/core/include -Iengine/include -Iengine/pp/include \
        -Iengine/media/include -Iengine/addons/include -Iengine/shaders/agc \
        -I$(HB)/include -I$(HB)/include/freetype2 -I$(HB)/include/harfbuzz \
        -I$(HB)/include/fribidi -I$(HB)/include/libxml2

CFLAGS   := -O2 -g -Wall -Wno-unused-parameter $(TFLAGS) $(DEFS) $(INCS)
CXXFLAGS := -O2 -g -Wall -Wno-unused-parameter -std=c++17 $(TFLAGS) $(DEFS) $(INCS)

ENGINE_C := $(wildcard engine/media/src/*.c) $(wildcard engine/pp/src/*.c) \
            $(wildcard engine/src/*.c) engine/addons/src/cJSON.c
ENGINE_CXX := engine/core/src/services/PlaybackController.cpp
APP_C   := $(wildcard src/*.c)
APP_CXX := $(wildcard src/*.cpp)

OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(ENGINE_C) $(APP_C)) \
        $(patsubst %.cpp,$(OBJDIR)/%.o,$(ENGINE_CXX) $(APP_CXX))

# PS5VR: edge264 (BSD, third_party/edge264) decodes H.264 MVC - 3D Blu-ray -
# with both views. One unity build (edge264.c includes the rest); the PS5 has
# no ENODATA, so edge264's end-of-stream code gets a private value.
EDGE264_DEFS := -DENODATA=9919
ifneq ($(findstring APP_VR,$(EXTRA_DEFS)),)
OBJS += $(OBJDIR)/third_party/edge264/edge264.o
endif
$(OBJDIR)/third_party/edge264/edge264.o: third_party/edge264/src/edge264.c $(wildcard third_party/edge264/src/*) $(FLAGS_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(TFLAGS) $(EDGE264_DEFS) -march=znver2 -std=gnu11 -O3 -flax-vector-conversions \
	    -Wno-override-init -w -c $< -o $@

# Objects depend on the flag set: a changed -D (ENG_AGC_HAVE_VIDEO_PIPES once
# went missing this way) rebuilds everything instead of linking stale code.
FLAGS_STAMP := $(OBJDIR)/.flags
$(shell mkdir -p $(OBJDIR); echo '$(CFLAGS) | $(CXXFLAGS)' | cmp -s - $(FLAGS_STAMP) || \
        echo '$(CFLAGS) | $(CXXFLAGS)' > $(FLAGS_STAMP))

# Fonts, icons and artwork are .incbin'd into ui_assets.o (scripts/gen_assets.py).
-include build-assets.mk
$(OBJDIR)/src/ui_assets.o: $(ASSET_FILES)

objects: $(OBJS)

print-objects:
	@echo $(abspath $(OBJS))

$(OBJDIR)/%.o: %.c $(FLAGS_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(OBJDIR)/%.o: %.cpp $(FLAGS_STAMP)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

clean:
	rm -rf build

-include $(OBJS:.o=.d)

.PHONY: objects print-objects clean
