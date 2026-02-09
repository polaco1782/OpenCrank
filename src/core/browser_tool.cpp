#include <opencrank/core/browser_tool.hpp>
#include <opencrank/core/registry.hpp>
#include <opencrank/core/agent.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace opencrank {

// ============ BrowserTool Implementation ============

namespace {

static std::vector<std::string> chunk_text(const std::string& text, size_t chunk_size, size_t max_chunks) {
    std::vector<std::string> chunks;
    if (chunk_size == 0) return chunks;

    size_t start = 0;
    while (start < text.size() && chunks.size() < max_chunks) {
        size_t remaining = text.size() - start;
        size_t len = remaining < chunk_size ? remaining : chunk_size;
        chunks.push_back(text.substr(start, len));
        start += len;
    }

    return chunks;
}

static size_t get_optional_size(const Json& params, const std::string& key, size_t default_value) {
    if (params.contains(key) && params[key].is_number_integer()) {
        int64_t value = params[key].get<int64_t>();
        if (value > 0) return static_cast<size_t>(value);
    }
    return default_value;
}

static bool get_optional_bool(const Json& params, const std::string& key, bool default_value) {
    if (params.contains(key) && params[key].is_boolean()) {
        return params[key].get<bool>();
    }
    return default_value;
}

} // namespace

BrowserTool::BrowserTool()
    : max_content_length_(100000)
    , timeout_secs_(30) {
}

const char* BrowserTool::name() const { return "browser"; }
const char* BrowserTool::version() const { return "1.0.0"; }
const char* BrowserTool::description() const {
    return "HTTP browser tool for web content fetching and extraction";
}

const char* BrowserTool::tool_id() const { return "browser"; }

std::vector<std::string> BrowserTool::actions() const {
    std::vector<std::string> result;
    result.push_back("fetch");
    result.push_back("extract_text");
    result.push_back("get_links");
    result.push_back("status");
    return result;
}

std::vector<AgentTool> BrowserTool::get_agent_tools() const {
    std::vector<AgentTool> tools;
    ToolProvider* self = const_cast<BrowserTool*>(this);
    
    // browser_fetch - Fetch raw HTML content from a URL
    {
        AgentTool tool;
        tool.name = "browser_fetch";
        tool.description = "Fetch raw HTML content from a URL. Returns the full HTML source code.";
        tool.params.push_back(ToolParamSchema("url", "string", "The URL to fetch (must start with http:// or https://)", true));
        tool.params.push_back(ToolParamSchema("max_length", "number", "Maximum content length to return (default: 100000)", false));
        tool.params.push_back(ToolParamSchema("extract_text", "boolean", "If true, strip HTML tags and return plain text (default: false)", false));
        tool.params.push_back(ToolParamSchema("proxy", "string", "Proxy URL to use. Supports HTTP (http://host:port), SOCKS5 (socks5://host:port), SOCKS4 (socks4://host:port). Optional authentication: http://user:pass@host:port", false));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("fetch", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            return AgentToolResult::ok(result.data.dump(2));
        };
        
        tools.push_back(tool);
    }
    
    // browser_extract_text - Extract readable text from a URL or HTML
    {
        AgentTool tool;
        tool.name = "browser_extract_text";
        tool.description = "Extract readable plain text from a URL or HTML content. Strips all HTML tags, scripts, and styles. Best for reading article content.";
        tool.params.push_back(ToolParamSchema("url", "string", "The URL to fetch and extract text from", false));
        tool.params.push_back(ToolParamSchema("html", "string", "Raw HTML content to extract text from (alternative to url)", false));
        tool.params.push_back(ToolParamSchema("max_length", "number", "Maximum text length to return (default: 100000)", false));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("extract_text", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            // Return just the text for easier consumption
            if (result.data.contains("text") && result.data["text"].is_string()) {
                return AgentToolResult::ok(result.data["text"].get<std::string>());
            }
            return AgentToolResult::ok(result.data.dump(2));
        };
        
        tools.push_back(tool);
    }
    
    // browser_get_links - Extract all links from a URL or HTML
    {
        AgentTool tool;
        tool.name = "browser_get_links";
        tool.description = "Extract all hyperlinks from a URL or HTML content. Returns a list of URLs with their link text.";
        tool.params.push_back(ToolParamSchema("url", "string", "The URL to fetch and extract links from", false));
        tool.params.push_back(ToolParamSchema("html", "string", "Raw HTML content to extract links from (alternative to url)", false));
        tool.params.push_back(ToolParamSchema("base_url", "string", "Base URL for resolving relative links", false));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("get_links", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            return AgentToolResult::ok(result.data.dump(2));
        };
        
        tools.push_back(tool);
    }
    
    return tools;
}

bool BrowserTool::init(const Config& cfg) {
    max_content_length_ = cfg.get_int("browser.max_content_length", 100000);
    timeout_secs_ = cfg.get_int("browser.timeout", 30);

    LOG_INFO("Browser tool initialized (max_content=%zu, timeout=%ds)",
             max_content_length_, timeout_secs_);

    initialized_ = true;
    return true;
}

void BrowserTool::shutdown() {
    initialized_ = false;
}

ToolResult BrowserTool::execute(const std::string& action, const Json& params) {
    if (action == "fetch") {
        return do_fetch(params);
    } else if (action == "extract_text") {
        return do_extract_text(params);
    } else if (action == "get_links") {
        return do_get_links(params);
    } else if (action == "status") {
        return do_status();
    }

    ToolResult result;
    result.success = false;
    result.error = "Unknown action: " + action;
    return result;
}

ToolResult BrowserTool::do_fetch(const Json& params) {
    ToolResult result;

    // Validate URL parameter
    if (!params.contains("url") || !params["url"].is_string()) {
        result.success = false;
        result.error = "Missing required parameter: url";
        return result;
    }

    std::string url = params["url"].get<std::string>();
    LOG_DEBUG("[Browser] ▶ OUT Fetching URL: %s", url.c_str());

    // Validate URL format
    if (url.find("http://") != 0 && url.find("https://") != 0) {
        result.success = false;
        result.error = "URL must start with http:// or https://";
        return result;
    }

    // Set up headers
    std::map<std::string, std::string> headers;
    headers["User-Agent"] = "OpenCrank C++/1.0";
    headers["Accept"] = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
    headers["Accept-Language"] = "en-US,en;q=0.5";

    // Add custom headers if provided
    if (params.contains("headers") && params["headers"].is_object()) {
        const Json& custom_headers = params["headers"];
        for (auto it = custom_headers.begin(); it != custom_headers.end(); ++it) {
            if (it.value().is_string()) {
                headers[it.key()] = it.value().get<std::string>();
            }
        }
    }

    // Get proxy parameter if provided
    std::string proxy;
    if (params.contains("proxy") && params["proxy"].is_string()) {
        proxy = params["proxy"].get<std::string>();
    }
    
    // Make HTTP request
    HttpResponse response = http_.get(url, headers, proxy);
    
    LOG_DEBUG("[Browser] ◀ IN  Response from %s: HTTP %ld (%zu bytes)", 
              url.c_str(), response.status_code, response.body.size());

    // Build result data
    Json data;
    data["url"] = url;
    data["status_code"] = response.status_code;
    data["success"] = (response.status_code >= 200 && response.status_code < 300);

    if (response.status_code >= 200 && response.status_code < 300) {
        // Optional behavior controls
        size_t max_len = get_optional_size(params, "max_length", max_content_length_);
        size_t chunk_size = get_optional_size(params, "chunk_size", 0);
        size_t max_chunks = get_optional_size(params, "max_chunks", 20);
        bool extract_text = get_optional_bool(params, "extract_text", false);

        std::string content = response.body;
        if (extract_text) {
            content = normalize_whitespace(strip_html(content));
        }

        bool truncated = false;
        size_t original_length = content.length();
        if (content.length() > max_len) {
            content = content.substr(0, max_len);
            truncated = true;
        }

        data["truncated"] = truncated;
        data["original_length"] = static_cast<int64_t>(original_length);

        if (chunk_size > 0) {
            std::vector<std::string> chunks = chunk_text(content, chunk_size, max_chunks);
            Json chunks_array = Json::array();
            for (size_t i = 0; i < chunks.size(); ++i) {
                chunks_array.push_back(chunks[i]);
            }
            data["chunks"] = chunks_array;
            data["chunk_count"] = static_cast<int64_t>(chunks.size());
            data["content_length"] = static_cast<int64_t>(content.length());
            if (content.length() > chunk_size * max_chunks) {
                data["truncated"] = true;
            }
        } else {
            data["content"] = content;
            data["content_length"] = static_cast<int64_t>(content.length());
        }

        // Extract content type from headers if available
        std::string content_type;
        std::map<std::string, std::string>::const_iterator ct_it = response.headers.find("Content-Type");
        if (ct_it == response.headers.end()) {
            ct_it = response.headers.find("content-type");
        }
        if (ct_it != response.headers.end()) {
            content_type = ct_it->second;
        }
        if (extract_text) {
            data["content_type"] = "text/plain; charset=utf-8";
            data["extracted_text"] = true;
        } else {
            data["content_type"] = content_type;
        }

        result.success = true;
    } else {
        data["error"] = "HTTP request failed with status " + std::to_string(response.status_code);
        result.success = false;
        result.error = data["error"].get<std::string>();
    }

    result.data = data;
    return result;
}

ToolResult BrowserTool::do_extract_text(const Json& params) {
    ToolResult result;

    // Can extract from HTML content directly or fetch from URL
    std::string html;

    if (params.contains("html") && params["html"].is_string()) {
        html = params["html"].get<std::string>();
    } else if (params.contains("url") && params["url"].is_string()) {
        // Fetch the URL first
        ToolResult fetch_result = do_fetch(params);
        if (!fetch_result.success) {
            return fetch_result;
        }
        if (fetch_result.data.contains("content") && fetch_result.data["content"].is_string()) {
            html = fetch_result.data["content"].get<std::string>();
        }
    } else {
        result.success = false;
        result.error = "Missing required parameter: url or html";
        return result;
    }

    // Extract text from HTML
    std::string text = strip_html(html);
    text = normalize_whitespace(text);

    // Optional truncation and chunking
    size_t max_len = get_optional_size(params, "max_length", max_content_length_);
    size_t chunk_size = get_optional_size(params, "chunk_size", 0);
    size_t max_chunks = get_optional_size(params, "max_chunks", 20);

    bool truncated = false;
    size_t original_length = text.length();
    if (text.length() > max_len) {
        text = text.substr(0, max_len);
        truncated = true;
    }

    Json data;
    if (chunk_size > 0) {
        std::vector<std::string> chunks = chunk_text(text, chunk_size, max_chunks);
        Json chunks_array = Json::array();
        for (size_t i = 0; i < chunks.size(); ++i) {
            chunks_array.push_back(chunks[i]);
        }
        data["chunks"] = chunks_array;
        data["chunk_count"] = static_cast<int64_t>(chunks.size());
        data["text_length"] = static_cast<int64_t>(text.length());
        if (text.length() > chunk_size * max_chunks) {
            truncated = true;
        }
    } else {
        data["text"] = text;
        data["text_length"] = static_cast<int64_t>(text.length());
    }
    data["original_length"] = static_cast<int64_t>(original_length);
    data["truncated"] = truncated;

    result.success = true;
    result.data = data;
    return result;
}

ToolResult BrowserTool::do_get_links(const Json& params) {
    ToolResult result;

    std::string html;
    std::string base_url;

    if (params.contains("html") && params["html"].is_string()) {
        html = params["html"].get<std::string>();
        base_url = params.value("base_url", std::string(""));
    } else if (params.contains("url") && params["url"].is_string()) {
        base_url = params["url"].get<std::string>();

        // Fetch the URL first
        ToolResult fetch_result = do_fetch(params);
        if (!fetch_result.success) {
            return fetch_result;
        }
        if (fetch_result.data.contains("content") && fetch_result.data["content"].is_string()) {
            html = fetch_result.data["content"].get<std::string>();
        }
    } else {
        result.success = false;
        result.error = "Missing required parameter: url or html";
        return result;
    }

    // Extract links
    std::vector<std::pair<std::string, std::string> > links = extract_links(html, base_url);

    // Build result
    Json links_array = Json::array();
    for (size_t i = 0; i < links.size(); ++i) {
        Json link;
        link["url"] = links[i].first;
        link["text"] = links[i].second;
        links_array.push_back(link);
    }

    Json data;
    data["links"] = links_array;
    data["count"] = static_cast<int64_t>(links.size());

    result.success = true;
    result.data = data;
    return result;
}

ToolResult BrowserTool::do_status() {
    ToolResult result;

    Json data;
    data["status"] = "ok";
    data["max_content_length"] = static_cast<int64_t>(max_content_length_);
    data["timeout_secs"] = timeout_secs_;

    result.success = true;
    result.data = data;
    return result;
}

// ============ Helper Functions ============

std::string BrowserTool::strip_html(const std::string& html) {
    std::string result;
    result.reserve(html.size());

    bool in_tag = false;
    bool in_script = false;
    bool in_style = false;

    for (size_t i = 0; i < html.size(); ++i) {
        char c = html[i];

        if (c == '<') {
            in_tag = true;

            // Check for script/style tags
            std::string lower_rest;
            for (size_t j = i; j < html.size() && j < i + 10; ++j) {
                lower_rest += static_cast<char>(std::tolower(html[j]));
            }

            if (lower_rest.find("<script") == 0) {
                in_script = true;
            } else if (lower_rest.find("</script") == 0) {
                in_script = false;
            } else if (lower_rest.find("<style") == 0) {
                in_style = true;
            } else if (lower_rest.find("</style") == 0) {
                in_style = false;
            }
        } else if (c == '>') {
            in_tag = false;
            result += ' ';  // Replace tag with space
        } else if (!in_tag && !in_script && !in_style) {
            // Decode common HTML entities
            if (c == '&' && i + 1 < html.size()) {
                std::string entity;
                size_t j = i + 1;
                while (j < html.size() && j < i + 10 && html[j] != ';' && html[j] != ' ') {
                    entity += html[j];
                    ++j;
                }

                if (j < html.size() && html[j] == ';') {
                    std::string decoded;
                    if (entity == "nbsp" || entity == "#160") decoded = " ";
                    else if (entity == "amp") decoded = "&";
                    else if (entity == "lt") decoded = "<";
                    else if (entity == "gt") decoded = ">";
                    else if (entity == "quot") decoded = "\"";
                    else if (entity == "apos") decoded = "'";

                    if (!decoded.empty()) {
                        result += decoded;
                        i = j;
                        continue;
                    }
                }
            }
            result += c;
        }
    }

    return result;
}

std::string BrowserTool::normalize_whitespace(const std::string& s) {
    std::string result;
    result.reserve(s.size());

    bool last_was_space = true;  // Start true to trim leading whitespace

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        bool is_space = std::isspace(static_cast<unsigned char>(c));

        if (is_space) {
            if (!last_was_space) {
                result += ' ';
                last_was_space = true;
            }
        } else {
            result += c;
            last_was_space = false;
        }
    }

    // Trim trailing space
    if (!result.empty() && result[result.size() - 1] == ' ') {
        result.erase(result.size() - 1);
    }

    return result;
}

std::vector<std::pair<std::string, std::string> > BrowserTool::extract_links(
        const std::string& html, const std::string& base_url) {
    std::vector<std::pair<std::string, std::string> > links;

    // Simple regex-free link extraction
    std::string lower_html = html;
    for (size_t i = 0; i < lower_html.size(); ++i) {
        lower_html[i] = static_cast<char>(std::tolower(lower_html[i]));
    }

    size_t pos = 0;
    while ((pos = lower_html.find("<a ", pos)) != std::string::npos) {
        // Find href attribute
        size_t href_pos = lower_html.find("href=", pos);
        if (href_pos == std::string::npos || href_pos > pos + 200) {
            pos++;
            continue;
        }

        // Extract URL
        size_t url_start = href_pos + 5;
        char quote = html[url_start];
        if (quote == '"' || quote == '\'') {
            url_start++;
            size_t url_end = html.find(quote, url_start);
            if (url_end != std::string::npos) {
                std::string url = html.substr(url_start, url_end - url_start);

                // Make absolute URL if relative
                if (!url.empty() && url[0] == '/') {
                    // Find base domain from base_url
                    size_t scheme_end = base_url.find("://");
                    if (scheme_end != std::string::npos) {
                        size_t domain_end = base_url.find('/', scheme_end + 3);
                        if (domain_end != std::string::npos) {
                            url = base_url.substr(0, domain_end) + url;
                        } else {
                            url = base_url + url;
                        }
                    }
                }

                // Extract link text
                size_t tag_end = html.find('>', url_end);
                size_t link_text_end = lower_html.find("</a>", tag_end);
                std::string text;
                if (tag_end != std::string::npos && link_text_end != std::string::npos) {
                    text = strip_html(html.substr(tag_end + 1, link_text_end - tag_end - 1));
                    text = normalize_whitespace(text);
                }

                if (!url.empty() && url.find("javascript:") != 0 && url.find("#") != 0) {
                    links.push_back(std::make_pair(url, text));
                }
            }
        }
        pos++;
    }

    return links;
}

} // namespace opencrank