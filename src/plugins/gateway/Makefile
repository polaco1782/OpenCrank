# Gateway Plugin Makefile
# Uses CrowCpp (header-only) for HTTP/WebSocket - requires C++17

# Compiler and flags - Override to use C++17 for Crow
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I../../../include -I.
CXXFLAGS_PIC = $(CXXFLAGS) -fPIC
LDFLAGS = -lpthread -lsqlite3 -lssl -lcrypto -lcurl -ldl
LDFLAGS_PLUGIN = -shared

# Directories
ROOT_DIR = ../../..
BUILD_DIR = $(ROOT_DIR)/build
INSTALL_DIR = $(ROOT_DIR)/bin/plugins

# Core object files (shared across all plugins)
# NOTE: json.o removed - nlohmann/json is header-only
CORE_OBJECTS = $(BUILD_DIR)/types.o \
               $(BUILD_DIR)/logger.o \
               $(BUILD_DIR)/config.o \
               $(BUILD_DIR)/http_client.o \
               $(BUILD_DIR)/utils.o \
               $(BUILD_DIR)/session.o \
               $(BUILD_DIR)/rate_limiter.o \
               $(BUILD_DIR)/loader.o \
               $(BUILD_DIR)/ai.o \
               $(BUILD_DIR)/memory_store.o \
               $(BUILD_DIR)/memory_manager.o

PLUGIN_NAME = gateway
PLUGIN_SOURCES = gateway.cpp
PLUGIN_OBJECTS = $(PLUGIN_SOURCES:.cpp=.o)

# No external library dependencies - Crow is header-only
# Requires: libz (usually installed), libpthread
PLUGIN_LDFLAGS = -lz

all: $(PLUGIN_NAME).so

# Build rule for plugin objects with C++17
%.o: %.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(PLUGIN_NAME).so: $(PLUGIN_OBJECTS)
	$(CXX) $(LDFLAGS_PLUGIN) $(PLUGIN_OBJECTS) $(CORE_OBJECTS) -o $@ $(LDFLAGS) $(PLUGIN_LDFLAGS)
	@echo "Built plugin: $@ (using Crow HTTP/WebSocket)"

clean:
	rm -f $(PLUGIN_OBJECTS) $(PLUGIN_NAME).so

install: $(PLUGIN_NAME).so
	mkdir -p $(INSTALL_DIR)
	cp $(PLUGIN_NAME).so $(INSTALL_DIR)/

.PHONY: all clean install
