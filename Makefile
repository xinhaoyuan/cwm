.PHONY: all 

T_CC_FLAGS       ?= $(shell pkg-config --cflags xcb) -Wall
T_C_ONLY_FLAGS   ?= -std=c99
T_CC_OPT_FLAGS   ?= -O0
T_CC_DEBUG_FLAGS ?= -g
T_LD_FLAGS       ?= $(shell pkg-config --libs xcb)

SRCFILES:=	$(shell find src '(' '!' -regex '.*/_.*' ')' -and '(' -iname "*.c" -or -iname "*.cpp" ')' | sed -e 's!^\./!!g')

include ${T_BASE}/utl/template.mk

all: ${T_OBJ}/${PRJ}

-include ${DEPFILES}

${T_OBJ}/${PRJ}: ${OBJFILES}
	@echo LD $@
	${CXX} ${T_LD_FLAGS} -o $@ ${OBJFILES}
