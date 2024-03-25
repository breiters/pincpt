PIN_ROOT?=/home/breiters/local/pin-3.26-98690-g1fc9d60e6-gcc-linux

ifdef PIN_ROOT
CONFIG_ROOT := $(PIN_ROOT)/source/tools/Config
else
CONFIG_ROOT := ../Config
endif
include $(CONFIG_ROOT)/makefile.config
include makefile.rules
include $(TOOLS_ROOT)/Config/makefile.default.rules

test:
	${PIN_ROOT}/pin -t obj-intel64/pincpt.so -- ./test/test
