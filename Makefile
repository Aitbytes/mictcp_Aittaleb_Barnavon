DATE := `date +'%Y%m%d'`
TAG := moodle-$(USER)

ifneq ($(tag),)
        TAG := $(TAG)-$(tag)
endif

CC        := gcc
LD        := gcc

TAR_FILENAME := $(DATE)-mictcp-$(TAG).tar.gz

MODULES   := api apps
SRC_DIR   := $(addprefix src/,$(MODULES)) src
BUILD_DIR := $(addprefix build/,$(MODULES)) build

SRC       := $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c))
OBJ       := $(patsubst src/%.c,build/%.o,$(SRC))
OBJ_CLI   := $(patsubst build/apps/gateway.o,,$(patsubst build/apps/server.o,,$(OBJ)))
OBJ_SERV  := $(patsubst build/apps/gateway.o,,$(patsubst build/apps/client.o,,$(OBJ)))
OBJ_GWAY  := $(patsubst build/apps/server.o,,$(patsubst build/apps/client.o,,$(OBJ)))
INCLUDES  := include

vpath %.c $(SRC_DIR)

define make-goal
$1/%.o: %.c
	$(CC) -std=gnu99 -Wall -m32 -g -I $(INCLUDES) -c $$< -o $$@
endef

.PHONY: all checkdirs clean

all: checkdirs build/client build/server build/gateway

build/client: $(OBJ_CLI)
	$(LD) -m32 $^ -o $@ -lm -lpthread

build/server: $(OBJ_SERV)
	$(LD) -m32 $^ -o $@ -lm -lpthread

build/gateway: $(OBJ_GWAY)
	$(LD) -m32 $^ -o $@ -lm -lpthread

checkdirs: $(BUILD_DIR)

$(BUILD_DIR):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD_DIR)

distclean:
	@rm -rf $(BUILD_DIR)
	@-rm -f *.tar.gz || true


$(foreach bdir,$(BUILD_DIR),$(eval $(call make-goal,$(bdir))))

dist:
	@tar --exclude=build --exclude=*tar.gz --exclude=.git* -czvf mictcp-bundle.tar.gz ../mictcp

prof:
	@tar --exclude=build --exclude=.*tar.gz --exclude=video -czvf $(TAR_FILENAME) ../mictcp
