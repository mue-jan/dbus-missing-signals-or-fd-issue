#
# Makefile minimal example for signal-/fd-issue
#
CC ?= gcc
CFLAGS = -Wall -Wshadow -Wundef
CFLAGS += -O0 -g3 -I./
LDFLAGS += -lsystemd -lpthread
BINSENDER = mbsp_signal_sender
BINRECEIVER = mbsp_signal_receiver
BUILD_DIR = build
VPATH = 

DEPDIR = $(BUILD_DIR)
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
POSTCOMPILE = @mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d && touch $@

SENDERSRC = mbsp_signal_sender.c
RECEIVERSRC = mbsp_signal_receiver.c

OBJEXT ?= o
SENDEROBJ = $(SENDERSRC:%.c=$(BUILD_DIR)/%.$(OBJEXT))
RECEIVEROBJ = $(RECEIVERSRC:%.c=$(BUILD_DIR)/%.$(OBJEXT))

SRCS = $(SENDERSRC) $(RECEIVERSRC)

OBJS = $(SENDEROBJ) $(RECEIVEROBJ)

## OBJS -> OBJFILES

all: $(BINSENDER) $(BINRECEIVER)

$(BUILD_DIR):
	$(shell mkdir -p $(BUILD_DIR) > /dev/null)

$(BUILD_DIR)/%.o: %.c 
$(BUILD_DIR)/%.o: %.c $(DEPDIR)/%.d 
	$(CC) $(DEPFLAGS) $(CFLAGS) -c $< -o $@
	@$(POSTCOMPILE)
    
$(BINSENDER): $(BUILD_DIR) $(SENDEROBJ)
	$(CC) -o $(BINSENDER) $(SENDEROBJ) $(LDFLAGS)
$(BINRECEIVER): $(BUILD_DIR) $(RECEIVEROBJ) 
	$(CC) -o $(BINRECEIVER) $(RECEIVEROBJ) $(LDFLAGS)

clean: 
	rm -f $(BINSENDER) $(BINRECEIVER) $(OBJS) 

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

include $(wildcard $(patsubst %,$(DEPDIR)/%.d,$(basename $(SRCS))))

