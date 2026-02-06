# Claude AI Plugin Makefile
include ../../../Makefile.plugin

PLUGIN_NAME = claude
PLUGIN_SOURCES = claude.cpp
PLUGIN_LDFLAGS = 

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
