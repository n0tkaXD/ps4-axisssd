PS4_HOST ?= 192.168.100.230
PS4_PORT ?= 9090

ifdef PS4_PAYLOAD_SDK
    include $(PS4_PAYLOAD_SDK)/toolchain/orbis.mk
else
    $(error PS4_PAYLOAD_SDK is undefined)
endif

ELF := ifusb_probe.elf

CFLAGS := -Wall -Werror -O1 -g

all: $(ELF)

$(ELF): main.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(ELF)

send: $(ELF)
	nc -w 3 $(PS4_HOST) $(PS4_PORT) < $^
