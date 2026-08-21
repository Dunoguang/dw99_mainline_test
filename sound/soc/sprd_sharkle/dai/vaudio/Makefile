# SPRD Vaudio Support
SPRD_SOUND_TREE := sound/soc/sprd_sharkle/
KBUILD_CFLAGS   +=	-I$(srctree)/$(SPRD_SOUND_TREE) \
					-I$(srctree)/$(SPRD_SOUND_TREE)/dai \
					-I$(srctree)/$(SPRD_SOUND_TREE)/dai/vbc/ \
					-I$(srctree)/$(SPRD_SOUND_TREE)/platform

snd-soc-sprd-vaudio-objs       := vaudio.o
obj-$(CONFIG_SND_SOC_SPRD_VAUDIO)       += snd-soc-sprd-vaudio.o
