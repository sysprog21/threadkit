TESTS = \
    threadpool \
    heavy \
    shutdown
TESTS := $(addprefix tests/test-,$(TESTS))
deps := $(TESTS:%=%.o.d)

.PHONY: all check clean
GIT_HOOKS := .git/hooks/applied
all: $(GIT_HOOKS) $(TESTS)

$(GIT_HOOKS):
	@scripts/install-git-hooks
	@echo

include common.mk

CFLAGS = -I./include
CFLAGS += -std=gnu11 -Wall -W
CFLAGS += -D_GNU_SOURCE
CFLAGS += -DUNUSED="__attribute__((unused))"
LDFLAGS = -lpthread

TESTS_OK = $(TESTS:=.ok)
check: $(TESTS_OK)

$(TESTS_OK): %.ok: %
	$(Q)$(PRINTF) "*** Validating $< ***\n"
	$(Q)./$< && $(PRINTF) "\t$(PASS_COLOR)[ Verified ]$(NO_COLOR)\n"
	@touch $@

# standard build rules
.SUFFIXES: .o .c
.c.o:
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -c -MMD -MF $@.d $<

OBJS = \
       src/threadpool.o \
       src/threadtracer.o
deps += $(OBJS:%.o=%.o.d)

$(TESTS): %: %.o $(OBJS)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ $^ $(LDFLAGS)

clean:
	$(VECHO) "  Cleaning...\n"
	$(Q)$(RM) $(TESTS) $(TESTS_OK) $(TESTS:=.o) $(OBJS) threadtracer*.json $(deps)

-include $(deps)
