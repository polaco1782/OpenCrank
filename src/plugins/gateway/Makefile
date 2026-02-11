# Gateway Plugin Makefile
# Uses CrowCpp (header-only) for HTTP/WebSocket - requires C++17
include ../../../Makefile.plugin

# Override to C++17 for Crow compatibility
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I../../../include -fPIE
CXXFLAGS_PIC = $(CXXFLAGS) -fPIC

PLUGIN_NAME = gateway
PLUGIN_SOURCES = gateway.cpp
PLUGIN_LDFLAGS = -lz

all: $(PLUGIN_NAME).so

$(PLUGIN_NAME).so: $(PLUGIN_OBJECTS)
	$(CXX) $(LDFLAGS_PLUGIN) $(PLUGIN_OBJECTS) $(CORE_OBJECTS) -o $@ $(LDFLAGS) $(PLUGIN_LDFLAGS)
	@echo "Built plugin: $@"

clean:
	rm -f $(PLUGIN_OBJECTS) $(PLUGIN_NAME).so

install: $(PLUGIN_NAME).so
	mkdir -p $(INSTALL_DIR)
	cp $(PLUGIN_NAME).so $(INSTALL_DIR)/

.PHONY: all clean install
