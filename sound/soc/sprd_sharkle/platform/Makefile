# SPRD Platform Support
SPRD_SOUND_TREE			:= sound/soc/sprd_sharkle/
SPRD_AUDIO_ASSISTANT_TREE	:= drivers/misc/sprd_audio/
SPRD_MCDT_TREE			:= drivers/misc/mcdt/
ccflags-y	+= -I$(srctree)/$(SPRD_AUDIO_ASSISTANT_TREE)/audiomem/ \
		   -I$(srctree)/$(SPRD_AUDIO_ASSISTANT_TREE)/audiosipc/ \
		   -I$(srctree)/$(SPRD_MCDT_TREE)
ifneq ($(CONFIG_SND_SOC_SPRD_AUDIO_TWO_STAGE_DMAENGINE_SURPPORT),)
	obj-$(CONFIG_SND_SOC_SPRD_SHARKLE) += sprd-2stage-dmaengine-pcm.o
else
	obj-$(CONFIG_SND_SOC_SPRD_SHARKLE) += sprd-dmaengine-pcm.o
endif

ifneq ($(CONFIG_SND_SOC_SPRD_COMPRESS),)
	obj-$(CONFIG_SND_SOC_SPRD_SHARKLE) += sprd-compr-2stage-dma.o
endif
