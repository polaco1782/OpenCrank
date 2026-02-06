#ifndef opencrank_CORE_BROWSER_HPP
#define opencrank_CORE_BROWSER_HPP

#include <opencrank/core/tool.hpp>
#include <opencrank/core/agent.hpp>
#include <opencrank/core/http_client.hpp>
#include <opencrank/core/logger.hpp>
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace opencrank {

// Browser tool - HTTP fetching and web content extraction
class BrowserTool : public ToolProvider {
public:
    BrowserTool();

    // Plugin interface
    const char* name() const;
    const char* version() const;
    const char* description() const;

    // Tool interface
    const char* tool_id() const;
    std::vector<std::string> actions() const;
    
    // Agent tools with detailed descriptions
    std::vector<AgentTool> get_agent_tools() const;

    bool init(const Config& cfg);
    void shutdown();

    ToolResult execute(const std::string& action, const Json& params);

private:
    HttpClient http_;
    size_t max_content_length_;
    int timeout_secs_;

    ToolResult do_fetch(const Json& params);
    ToolResult do_extract_text(const Json& params);
    ToolResult do_get_links(const Json& params);
    ToolResult do_status();

    static std::string strip_html(const std::string& html);
    static std::string normalize_whitespace(const std::string& s);
    static std::vector<std::pair<std::string, std::string> > extract_links(
            const std::string& html, const std::string& base_url);
};

} // namespace opencrank

#endif // opencrank_CORE_BROWSER_HPP
