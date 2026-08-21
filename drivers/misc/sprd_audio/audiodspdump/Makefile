# SPRD Audio Dsp Log Support

SPRD_SND_TREE	:= sound/soc/sprd_sharkle/
SPRD_AUDIO_ASSISTANT_TREE := drivers/misc/sprd_audio/
ccflags-y += -I$(srctree)/$(SPRD_AUDIO_ASSISTANT_TREE)/audiomem/ \
	     -I$(srctree)/$(SPRD_AUDIO_ASSISTANT_TREE)/audiosipc/ \
	     -I$(srctree)/$(SPRD_AUDIO_ASSISTANT_TREE)/agdsp_access/ \
	     -I$(srctree)/$(SPRD_SND_TREE)/include

obj-$(CONFIG_SPRD_AUDIO_DSP_DUMP) +=  audio_sblock.o audio_dsp_dump.o
