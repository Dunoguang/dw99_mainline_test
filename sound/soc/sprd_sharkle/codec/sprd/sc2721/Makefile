#SPRD CODEC

SPRD_SOUND_TREE := sound/soc/sprd_sharkle/
ccflags-y += -I$(srctree)/$(SPRD_SOUND_TREE)/codec/sprd

snd-soc-sprd-codec-sc2721-objs   := sprd-codec.o sprd-headset-sc2721.o
obj-$(CONFIG_SND_SOC_SPRD_CODEC_SC2721)        += snd-soc-sprd-codec-sc2721.o
obj-$(CONFIG_SND_SOC_SPRD_CODEC_SC2721)        += sprd-audio-power.o
