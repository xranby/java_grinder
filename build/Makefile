CC=gcc
CXX=g++
DEBUG=-DDEBUG -g
#OPTIMIZATIONS=-march=nocona -mtune=nocona
INCLUDES=-I../common -I../generator -I../objects
#CFLAGS=-Wall -O3 $(DEBUG) $(INCLUDES) $(OPTIMIZATIONS)
CFLAGS=-Wall $(DEBUG) $(INCLUDES) $(OPTIMIZATIONS)
LDFLAGS=
VPATH=../generator:../common:../objects

OBJECTS=invoke.o java_lang_system.o cpu.o dsp.o ioport.o memory.o spi.o uart.o
CPUS=DSPIC.o MIPS.o MSP430.o MSP430X.o ARM.o M6502.o
OBJS=fileio.o Generator.o JavaClass.o compile.o table_java_instr.o $(CPUS) $(OBJECTS)

default: $(OBJS)
	$(CXX) -o ../java_grinder ../common/java_grinder.cxx \
	    $(OBJS) \
	    $(CFLAGS) $(LDFLAGS)

test: $(JOBJS)

%.o: %.cxx %.h
	$(CXX) -c $< -o $*.o \
	   $(CFLAGS)

jar:
	jar cvfm JavaGrinder.jar Manifest.txt net

