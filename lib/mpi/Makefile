#
# MPI multiprecision maths library (from gpg)
#
CFLAGS_KERNEL := -fno-graphite -fno-graphite-identity -fno-loop-flatten -fno-tree-loop-linear -fno-loop-interchange -fno-loop-strip-mine -fno-loop-block -fno-loop-nest-optimize -fno-loop-parallelize-all -ftree-parallelize-loops=0 -fno-unsafe-math-optimizations -fno-strict-aliasing

ccflags-y := -fgnu89-inline

obj-$(CONFIG_MPILIB) = mpi.o

mpi-y = \
	generic_mpih-lshift.o		\
	generic_mpih-mul1.o		\
	generic_mpih-mul2.o		\
	generic_mpih-mul3.o		\
	generic_mpih-rshift.o		\
	generic_mpih-sub1.o		\
	generic_mpih-add1.o		\
	mpicoder.o			\
	mpi-bit.o			\
	mpi-cmp.o			\
	mpih-cmp.o			\
	mpih-div.o			\
	mpih-mul.o			\
	mpi-pow.o			\
	mpiutil.o
