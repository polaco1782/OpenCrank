# OpenCrank C++11 Makefile
# Minimal build system for the modular AI assistant framework

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -g -O0 -I./include
CXXFLAGS_PIC = $(CXXFLAGS) -fPIC -fvisibility=hidden
LDFLAGS = -lpthread -lsqlite3 -lssl -lcrypto -lcurl -ldl

# Directories
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin
PLUGIN_DIR = $(BIN_DIR)/plugins

# Plugin directories (each has its own Makefile)
PLUGIN_DIRS = src/plugins/telegram \
              src/plugins/whatsapp \
              src/plugins/claude \
              src/plugins/llamacpp \
              src/plugins/polls \
              src/plugins/gateway

# Core source files (always built into main binary)
CORE_SOURCES = $(SRC_DIR)/core/types.cpp \
               $(SRC_DIR)/core/logger.cpp \
               $(SRC_DIR)/core/config.cpp \
               $(SRC_DIR)/core/http_client.cpp \
               $(SRC_DIR)/core/commands.cpp \
               $(SRC_DIR)/core/browser_tool.cpp \
               $(SRC_DIR)/core/tool.cpp \
               $(SRC_DIR)/core/utils.cpp \
               $(SRC_DIR)/core/session.cpp \
               $(SRC_DIR)/core/rate_limiter.cpp \
               $(SRC_DIR)/core/loader.cpp \
               $(SRC_DIR)/core/memory_tool.cpp \
               $(SRC_DIR)/core/application.cpp \
               $(SRC_DIR)/core/message_handler.cpp \
               $(SRC_DIR)/core/builtin_tools.cpp \
               $(SRC_DIR)/core/ai_monitor.cpp \
               $(SRC_DIR)/ai/ai.cpp \
               $(SRC_DIR)/memory/store.cpp \
               $(SRC_DIR)/memory/manager.cpp \
               $(SRC_DIR)/skills/loader.cpp \
               $(SRC_DIR)/skills/manager.cpp

# Core object files
CORE_OBJECTS = $(BUILD_DIR)/types.o \
               $(BUILD_DIR)/logger.o \
               $(BUILD_DIR)/config.o \
               $(BUILD_DIR)/http_client.o \
               $(BUILD_DIR)/commands.o \
               $(BUILD_DIR)/browser_tool.o \
               $(BUILD_DIR)/tool.o \
               $(BUILD_DIR)/utils.o \
               $(BUILD_DIR)/session.o \
               $(BUILD_DIR)/rate_limiter.o \
               $(BUILD_DIR)/loader.o \
               $(BUILD_DIR)/plugin.o \
               $(BUILD_DIR)/thread_pool.o \
               $(BUILD_DIR)/memory_tool.o \
               $(BUILD_DIR)/agent.o \
               $(BUILD_DIR)/application.o \
               $(BUILD_DIR)/message_handler.o \
               $(BUILD_DIR)/builtin_tools.o \
               $(BUILD_DIR)/ai_monitor.o \
               $(BUILD_DIR)/ai.o \
               $(BUILD_DIR)/memory_store.o \
               $(BUILD_DIR)/memory_manager.o \
               $(BUILD_DIR)/skills_loader.o \
               $(BUILD_DIR)/skills_manager.o

# Main binary objects
MAIN_OBJECTS = $(BUILD_DIR)/main.o $(CORE_OBJECTS)

TARGET = $(BIN_DIR)/opencrank

# Default target - build main binary and all plugins
all: dirs core $(TARGET) plugins

# Build only core objects
core: dirs $(CORE_OBJECTS)

# Build only plugins (delegates to plugin Makefiles)
plugins: core
	@for dir in $(PLUGIN_DIRS); do \
		echo "Building plugin in $$dir..."; \
		$(MAKE) -C $$dir install || exit 1; \
	done

# Create directories
dirs:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR) $(PLUGIN_DIR)

# Link main binary
$(TARGET): $(MAIN_OBJECTS)
	$(CXX) -rdynamic $(MAIN_OBJECTS) -o $@ $(LDFLAGS)
	@echo "Built: $@"

# ============ Core object files ============
$(BUILD_DIR)/main.o: $(SRC_DIR)/main.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/types.o: $(SRC_DIR)/core/types.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/logger.o: $(SRC_DIR)/core/logger.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/config.o: $(SRC_DIR)/core/config.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/http_client.o: $(SRC_DIR)/core/http_client.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/commands.o: $(SRC_DIR)/core/commands.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/browser_tool.o: $(SRC_DIR)/core/browser_tool.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/tool.o: $(SRC_DIR)/core/tool.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/loader.o: $(SRC_DIR)/core/loader.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/plugin.o: $(SRC_DIR)/core/plugin.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/thread_pool.o: $(SRC_DIR)/core/thread_pool.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/ai.o: $(SRC_DIR)/ai/ai.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/agent.o: $(SRC_DIR)/core/agent.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/application.o: $(SRC_DIR)/core/application.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/message_handler.o: $(SRC_DIR)/core/message_handler.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/ai_monitor.o: $(SRC_DIR)/core/ai_monitor.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/builtin_tools.o: $(SRC_DIR)/core/builtin_tools.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/memory_tool.o: $(SRC_DIR)/core/memory_tool.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/memory_store.o: $(SRC_DIR)/memory/store.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/memory_manager.o: $(SRC_DIR)/memory/manager.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/skills_loader.o: $(SRC_DIR)/skills/loader.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/skills_manager.o: $(SRC_DIR)/skills/manager.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/utils.o: $(SRC_DIR)/core/utils.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/session.o: $(SRC_DIR)/core/session.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

$(BUILD_DIR)/rate_limiter.o: $(SRC_DIR)/core/rate_limiter.cpp
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

# Debug build
debug: CXXFLAGS += -g -DDEBUG -O0
debug: CXXFLAGS_PIC += -g -DDEBUG -O0
debug: clean all

# Release build (with optimizations and stripping)
release: CXXFLAGS += -O3 -DNDEBUG
release: CXXFLAGS_PIC += -O3 -DNDEBUG
release: clean all
	strip $(TARGET)
	strip $(PLUGIN_DIR)/*.so

# Clean everything
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	@for dir in $(PLUGIN_DIRS); do \
		$(MAKE) -C $$dir clean 2>/dev/null || true; \
	done

# Install to /usr/local
install: all
	install -d /usr/local/bin
	install -d /usr/local/lib/opencrank/plugins
	install -m 755 $(TARGET) /usr/local/bin/opencrank
	install -m 755 $(PLUGIN_DIR)/*.so /usr/local/lib/opencrank/plugins/

# Uninstall
uninstall:
	rm -f /usr/local/bin/opencrank
	rm -rf /usr/local/lib/opencrank

# Run with example
run: all
	@echo "Starting OpenCrank..."
	@echo "Make sure config.json has telegram.bot_token set"
	$(TARGET) config.json

# Help
help:
	@echo "OpenCrank C++11 Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build main binary and all plugins (default)"
	@echo "  core     - Build only core object files"
	@echo "  plugins  - Build only plugin shared libraries"
	@echo "  debug    - Build with debug symbols"
	@echo "  release  - Build optimized release"
	@echo "  clean    - Remove build artifacts"
	@echo "  install  - Install to /usr/local"
	@echo "  run      - Build and run"
	@echo "  help     - Show this help"
	@echo ""
	@echo "Plugin Structure:"
	@echo "  Each plugin in src/plugins/* has its own Makefile"
	@echo "  Plugin builds are delegated to individual Makefiles"
	@echo ""
	@echo "Available Plugins:"
	@echo "  telegram     - Telegram Bot API channel"
	@echo "  whatsapp     - WhatsApp messaging channel"
	@echo "  browser      - HTTP browser tool"
	@echo "  claude       - Anthropic Claude AI"
	@echo "  llamacpp     - Llama.cpp local AI"
	@echo "  polls        - Polls/voting tool"
	@echo "  gateway      - WebSocket gateway server"
	@echo ""
	@echo "Core Components:"
	@echo "  memory       - Memory/task management (built into core)"
	@echo "  session      - Session management (built into core)"
	@echo "  rate_limiter - Rate limiting utility (built into core)"
	@echo ""
	@echo "Requirements:"
	@echo "  - g++ with C++11 support"
	@echo "  - libcurl development headers"
	@echo "  - libsqlite3 development headers"
	@echo "  - libssl development headers"
	@echo "  - libwebsockets development headers (for gateway)"

.PHONY: all dirs core plugins clean debug release install uninstall run help
