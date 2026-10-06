#performance
obj-$(CONFIG_SPRD_PERFORMANCE)		+= sprd_bm_perf.o
#ptm
obj-$(CONFIG_SPRD_PTM)			+= sprd_ptm.o
# sprd_ptm.c uses CREATE_TRACE_POINTS with TRACE_INCLUDE_PATH "." ,
# so the trace header must be found relative to the source directory.
CFLAGS_sprd_ptm.o += -I$(src)
