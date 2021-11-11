
ALL=sigma_dut

all: $(ALL)

ifdef UBSAN
CC=clang
CHECKS=undefined,unsigned-integer-overflow
CFLAGS += -fsanitize=$(CHECKS)
CFLAGS += -fno-sanitize-recover=all
LDFLAGS += -fsanitize=$(CHECKS)
LDFLAGS += -fno-sanitize-recover=all
endif

ifdef CFI
CC=clang-6.0
CFLAGS += -MMD -O2 -Wall -g
CFLAGS += -flto -fvisibility=hidden -fsanitize=cfi -fno-sanitize-trap=cfi
LDFLAGS += -flto -fvisibility=hidden -fsanitize=cfi -fno-sanitize-trap=cfi
endif

ifndef CC
CC=gcc
endif

ifndef LDO
LDO=$(CC)
endif

ifndef CFLAGS
CFLAGS = -MMD -O2 -Wall -g
endif

GITVER := $(shell git describe --dirty=+)
ifneq ($(GITVER),)
CFLAGS += -DSIGMA_DUT_VER=\"$(GITVER)\"
endif

export BINDIR ?= /usr/local/bin/

OBJS=sigma_dut.o
OBJS += utils.o
OBJS += wpa_ctrl.o
OBJS += wpa_helpers.o

OBJS += cmds_reg.o
OBJS += basic.o
OBJS += sta.o
OBJS += traffic.o
OBJS += p2p.o
OBJS += dev.o
OBJS += ap.o
OBJS += powerswitch.o
OBJS += atheros.o
OBJS += ftm.o
OBJS += dpp.o

ifndef NO_TRAFFIC_AGENT
CFLAGS += -DCONFIG_TRAFFIC_AGENT -DCONFIG_WFA_WMM_AC
OBJS += traffic_agent.o
OBJS += uapsd_stream.o
LIBS += -lpthread
endif

ifndef NO_WLANTEST
CFLAGS += -DCONFIG_WLANTEST
OBJS += wlantest.o
endif

ifndef NO_SNIFFER
CFLAGS += -DCONFIG_SNIFFER
OBJS += sniffer.o
endif

ifdef SERVER
CFLAGS += -DCONFIG_SERVER
OBJS += server.o
LIBS += -lsqlite3
endif

ifdef MIRACAST
OBJS += miracast.o
CFLAGS += -DMIRACAST
LIBS += -ldl
endif

ifdef NL80211_SUPPORT
CFLAGS += -DNL80211_SUPPORT -I /usr/include/libnl3
LIBS += -lnl-3 -lnl-genl-3
endif

sigma_dut: $(OBJS)
	$(LDO) $(LDFLAGS) -o sigma_dut $(OBJS) $(LIBS)

clean:
	rm -f core *~ *.o *.d sigma_dut

$(DESTDIR)$(BINDIR)/%: %
	install -D $(<) $(@)

install: $(addprefix $(DESTDIR)$(BINDIR)/,$(ALL))

-include $(OBJS:%.o=%.d)
