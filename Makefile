CC      = gcc
CFLAGS  = -Wall -O2 -std=c11
LIBS    = -lncursesw -lpthread

TARGET  = coshell
SRC     = coshell.c chat.c qr.c todo_client.c todo_core.c todo_server.c todo_ui.c

.PHONY: all setup install clean

all: setup $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

run: $(TARGET)
	./$(TARGET)

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin

setup:
	@echo "Installing dependencies..."
	@sudo apt update -qq
	@sudo apt install -y libncursesw5-dev qrencode

clean:
	rm -f $(TARGET)
