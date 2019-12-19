
CFLAGS = -Wall -Wextra -O2 -g
LDLIBS = -lncurses -lpthread

BIN = lametta

.PHONY: all clean

all: $(BIN)

clean:
	@$(RM) $(BIN)
