ccflags-y += -I$(srctree)/sound/soc/codecs/tas256x

obj-y += snd-soc-tas256x.o

snd-soc-tas256x-y := tas256x-codec.o \
	tas256x-regmap.o \
	tas256x.o
snd-soc-tas256x-$(CONFIG_TAS25XX_ALGO) += algo/src/tas25xx-algo.o \
	algo/src/tas25xx-calib.o
