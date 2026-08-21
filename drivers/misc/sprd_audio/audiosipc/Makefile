# SPRD Audio SIPC Support

SPRD_SND_TREE   := sound/soc/sprd_sharkle/
SPRD_AUDIO_ASSISTANT_TREE := drivers/misc/sprd_audio/
ccflags-y	+= -I$(srctree)/drivers/staging/sprd/modem/include/ \
		   -I$(srctree)/$(SPRD_AUDIO_ASSISTANT_TREE)/audiomem/ \
		   -I$(srctree)/$(SPRD_SND_TREE)/include/

obj-$(CONFIG_SPRD_AUDIO_SIPC) += audio-sipc.o audio-smsg.o audio-pipe.o
