/*
 * OpenCrank C++11 - Personal AI Assistant Framework
 * 
 * A minimal, modular implementation with dynamic plugin loading support.
 * 
 * Usage:
 *   ./opencrank [config.json]
 * 
 * All configuration is read from config.json file.
 */
#include <opencrank/core/application.hpp>

int main(int argc, char* argv[]) {
    auto& app = opencrank::Application::instance();
    
    if (!app.init(argc, argv)) {
        // init returns false for --help/--version or fatal errors
        return app.is_running() ? 1 : 0;
    }
    
    int result = app.run();
    app.shutdown();
    
    return result;
}
