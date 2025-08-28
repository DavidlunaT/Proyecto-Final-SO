CC=gcc
CFLAGS=-O2 -Wall -Wextra -std=c11
LDFLAGS=-pthread -lrt

INC=include

MANAGER_SRCS=src/manager.c src/common.c
DASHBOARD_SRCS=src/dashboard.c src/common.c
CONTROLLER_SRCS=src/controller.c src/common.c

BIN_MANAGER=burger_manager
BIN_DASHBOARD=dashboard
BIN_CONTROLLER=controller

all: $(BIN_MANAGER) $(BIN_DASHBOARD) $(BIN_CONTROLLER)

$(BIN_MANAGER): $(MANAGER_SRCS)
	$(CC) $(CFLAGS) -I$(INC) $^ -o $@ $(LDFLAGS)

$(BIN_DASHBOARD): $(DASHBOARD_SRCS)
	$(CC) $(CFLAGS) -I$(INC) $^ -o $@ $(LDFLAGS)

$(BIN_CONTROLLER): $(CONTROLLER_SRCS)
	$(CC) $(CFLAGS) -I$(INC) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(BIN_MANAGER) $(BIN_DASHBOARD) $(BIN_CONTROLLER)

.PHONY: all clean
