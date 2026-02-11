#include <opencrank/core/browser_tool.hpp>
#include <opencrank/core/registry.hpp>
#include <opencrank/core/agent.hpp>
#include <opencrank/core/utils.hpp>
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

static std::string url_encode_simple(const std::string& s) {
    std::string result;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else if (c == ' ') {
            result += '+';
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            result += buf;
        }
    }
    return result;
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
    result.push_back("request");
    result.push_back("extract_text");
    result.push_back("get_links");
    result.push_back("extract_forms");
    result.push_back("status");
    return result;
}

std::vector<AgentTool> BrowserTool::get_agent_tools() const {
    std::vector<AgentTool> tools;
    ToolProvider* self = const_cast<BrowserTool*>(this);
    
    // ===== browser_fetch (GET) =====
    {
        AgentTool tool;
        tool.name = "browser_fetch";
        tool.description = 
            "Perform an HTTP GET request and return the response. "
            "You should use this instead of using external tools such as curl or wget, when something instructs you to fetch a web page or URL content. "
            "The browser tool can automatically extract text and links, and it also respects proxy settings. "
            "Use this to read web pages, download JSON APIs, or retrieve any URL content.\n"
            "Returns: url, status_code, content (raw HTML or text), content_type, content_length, truncated.\n"
            "Set extract_text=true to automatically strip HTML and get readable plain text.";
        tool.params.push_back(ToolParamSchema("url", "string", "The URL to fetch (must start with http:// or https://)", true));
        tool.params.push_back(ToolParamSchema("max_length", "number", "Maximum content length to return in characters (default: 100000)", false));
        tool.params.push_back(ToolParamSchema("extract_text", "boolean", "If true, strip HTML tags/scripts/styles and return plain text (default: false)", false));
        tool.params.push_back(ToolParamSchema("proxy", "string", "Proxy URL. Supports http://host:port, socks5://host:port, socks4://host:port. Auth: http://user:pass@host:port", false));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("fetch", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            return AgentToolResult::ok(result.data.dump(2));
        };
        tools.push_back(tool);
    }
    
    // ===== browser_request (any HTTP method: POST, PUT, HEAD, DELETE, PATCH) =====
    {
        AgentTool tool;
        tool.name = "browser_request";
        tool.description = 
            "Perform an HTTP request with any method (POST, PUT, DELETE, PATCH, HEAD). "
            "Use this for API calls, form submissions, and any non-GET request.\n\n"
            "== Sending a JSON body ==\n"
            "Set content_type to 'application/json' and provide body as a JSON string:\n"
            "  {\"method\": \"POST\", \"url\": \"https://api.example.com/data\", "
            "\"content_type\": \"application/json\", \"body\": \"{\\\"key\\\": \\\"value\\\"}\"} \n\n"
            "== Submitting an HTML form (URL-encoded) ==\n"
            "Set content_type to 'application/x-www-form-urlencoded' and provide form_data as key-value pairs:\n"
            "  {\"method\": \"POST\", \"url\": \"https://example.com/login\", "
            "\"content_type\": \"application/x-www-form-urlencoded\", "
            "\"form_data\": {\"username\": \"admin\", \"password\": \"secret\"}} \n\n"
            "== Workflow: Extracting and submitting a form ==\n"
            "1. Use browser_fetch to GET the page containing the form.\n"
            "2. Use browser_extract_forms on the page URL or HTML to discover form actions, methods, and fields.\n"
            "3. Use browser_request with the form's action URL, method, and the field values as form_data.\n\n"
            "Returns: url, status_code, content, content_type, content_length, response_headers, truncated.";
        tool.params.push_back(ToolParamSchema("method", "string", "HTTP method: POST, PUT, DELETE, PATCH, or HEAD", true));
        tool.params.push_back(ToolParamSchema("url", "string", "The URL to send the request to (must start with http:// or https://)", true));
        tool.params.push_back(ToolParamSchema("body", "string", "Raw request body string. Use for JSON or raw payloads. Mutually exclusive with form_data.", false));
        tool.params.push_back(ToolParamSchema("form_data", "object", "Key-value pairs to send as application/x-www-form-urlencoded form body. Mutually exclusive with body.", false));
        tool.params.push_back(ToolParamSchema("content_type", "string", "Content-Type header (default: application/x-www-form-urlencoded when form_data is set, otherwise application/json)", false));
        tool.params.push_back(ToolParamSchema("headers", "object", "Additional HTTP headers as key-value pairs, e.g. {\"Authorization\": \"Bearer token\"}", false));
        tool.params.push_back(ToolParamSchema("max_length", "number", "Maximum response content length in characters (default: 100000)", false));
        tool.params.push_back(ToolParamSchema("extract_text", "boolean", "If true, strip HTML from response and return plain text (default: false)", false));
        tool.params.push_back(ToolParamSchema("proxy", "string", "Proxy URL. Supports http://, socks5://, socks4://", false));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("request", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            return AgentToolResult::ok(result.data.dump(2));
        };
        tools.push_back(tool);
    }

    // ===== browser_extract_text =====
    {
        AgentTool tool;
        tool.name = "browser_extract_text";
        tool.description = 
            "Extract readable plain text from a URL or raw HTML content. "
            "Strips all HTML tags, scripts, styles, and normalizes whitespace. "
            "Best for reading article/page content without HTML clutter.\n"
            "Provide either 'url' (fetches the page first) or 'html' (parses directly).";
        tool.params.push_back(ToolParamSchema("url", "string", "The URL to fetch and extract text from", false));
        tool.params.push_back(ToolParamSchema("html", "string", "Raw HTML content to extract text from (alternative to url)", false));
        tool.params.push_back(ToolParamSchema("max_length", "number", "Maximum text length to return (default: 100000)", false));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("extract_text", params);
            if (!result.success) {
                return AgentToolResult::fail(result.error);
            }
            if (result.data.contains("text") && result.data["text"].is_string()) {
                return AgentToolResult::ok(result.data["text"].get<std::string>());
            }
            return AgentToolResult::ok(result.data.dump(2));
        };
        tools.push_back(tool);
    }
    
    // ===== browser_get_links =====
    {
        AgentTool tool;
        tool.name = "browser_get_links";
        tool.description = 
            "Extract all hyperlinks (<a href>) from a URL or raw HTML. "
            "Returns an array of {url, text} objects. "
            "Relative URLs are resolved against the page URL or base_url.\n"
            "Provide either 'url' (fetches the page first) or 'html' (parses directly).";
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

    // ===== browser_extract_forms =====
    {
        AgentTool tool;
        tool.name = "browser_extract_forms";
        tool.description = 
            "Extract all HTML forms from a URL or raw HTML. "
            "Returns an array of forms, each with: action (URL), method (GET/POST), id, name, "
            "and fields (array of {name, type, value, required}).\n\n"
            "This is essential for interacting with web forms. Typical workflow:\n"
            "1. Call browser_extract_forms with the page URL to discover available forms.\n"
            "2. Review the form fields: fill in required fields, keep hidden field values as-is.\n"
            "3. Call browser_request with method=POST (or whatever the form method is), "
            "url=<form action>, and form_data={field_name: value, ...} for all fields.\n\n"
            "Example response:\n"
            "  {\"forms\": [{\"action\": \"https://example.com/login\", \"method\": \"POST\", "
            "\"fields\": [{\"name\": \"username\", \"type\": \"text\", \"value\": \"\", \"required\": true}, "
            "{\"name\": \"password\", \"type\": \"password\", \"value\": \"\", \"required\": true}, "
            "{\"name\": \"csrf_token\", \"type\": \"hidden\", \"value\": \"abc123\", \"required\": false}]}]}\n"
            "Then submit with: browser_request method=POST url=https://example.com/login "
            "form_data={\"username\": \"user\", \"password\": \"pass\", \"csrf_token\": \"abc123\"}";
        tool.params.push_back(ToolParamSchema("url", "string", "The URL to fetch and extract forms from", false));
        tool.params.push_back(ToolParamSchema("html", "string", "Raw HTML content to extract forms from (alternative to url)", false));
        tool.params.push_back(ToolParamSchema("base_url", "string", "Base URL for resolving relative form action URLs", false));
        
        tool.execute = [self](const Json& params) -> AgentToolResult {
            ToolResult result = self->execute("extract_forms", params);
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
    } else if (action == "request") {
        return do_request(params);
    } else if (action == "extract_text") {
        return do_extract_text(params);
    } else if (action == "get_links") {
        return do_get_links(params);
    } else if (action == "extract_forms") {
        return do_extract_forms(params);
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

    // Sanitize URL to remove HTML tags and invalid characters
    std::string sanitized_url = sanitize_url(url);
    
    // If sanitization removed everything, return error
    if (sanitized_url.empty()) {
        result.success = false;
        result.error = "Invalid URL: URL contains only invalid characters or HTML tags";
        return result;
    }
    
    // Validate URL format
    if (!starts_with(sanitized_url, "http://") && !starts_with(sanitized_url, "https://")) {
        result.success = false;
        result.error = "URL must start with http:// or https://";
        return result;
    }
    
    // Use the sanitized URL for the request
    url = sanitized_url;
    LOG_DEBUG("[Browser] ▶ OUT Fetching URL: %s", url.c_str());

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
            content.resize(max_len);
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
        text.resize(max_len);
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

// ============ Generic HTTP Request ============

ToolResult BrowserTool::perform_browser_request(const std::string& method,
                                                 const std::string& url,
                                                 const std::string& body,
                                                 const std::string& content_type,
                                                 const std::map<std::string, std::string>& extra_headers,
                                                 const std::string& proxy,
                                                 size_t max_len,
                                                 bool do_extract_text) {
    ToolResult result;

    // Set up headers
    std::map<std::string, std::string> headers = extra_headers;
    if (headers.find("User-Agent") == headers.end()) {
        headers["User-Agent"] = "OpenCrank C++/1.0";
    }
    if (headers.find("Accept") == headers.end()) {
        headers["Accept"] = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
    }
    if (headers.find("Accept-Language") == headers.end()) {
        headers["Accept-Language"] = "en-US,en;q=0.5";
    }
    if (!content_type.empty()) {
        headers["Content-Type"] = content_type;
    }

    LOG_DEBUG("[Browser] ▶ OUT %s %s (body: %zu bytes)", method.c_str(), url.c_str(), body.size());

    // Use HttpClient's generic request method
    HttpResponse response = http_.request(method, url, body, headers, proxy);

    LOG_DEBUG("[Browser] ◀ IN  Response from %s: HTTP %ld (%zu bytes)",
              url.c_str(), response.status_code, response.body.size());

    // Build result
    Json data;
    data["url"] = url;
    data["method"] = method;
    data["status_code"] = response.status_code;
    data["success"] = (response.status_code >= 200 && response.status_code < 300);

    // Include response headers
    Json resp_headers = Json::object();
    for (std::map<std::string, std::string>::const_iterator it = response.headers.begin();
         it != response.headers.end(); ++it) {
        resp_headers[it->first] = it->second;
    }
    data["response_headers"] = resp_headers;

    if (response.status_code >= 200 && response.status_code < 300) {
        std::string content = response.body;
        if (do_extract_text) {
            content = normalize_whitespace(strip_html(content));
        }

        bool truncated = false;
        size_t original_length = content.length();
        if (content.length() > max_len) {
            content.resize(max_len);
            truncated = true;
        }

        data["content"] = content;
        data["content_length"] = static_cast<int64_t>(content.length());
        data["original_length"] = static_cast<int64_t>(original_length);
        data["truncated"] = truncated;

        // Content type from response
        std::string ct;
        std::map<std::string, std::string>::const_iterator ct_it = response.headers.find("Content-Type");
        if (ct_it == response.headers.end()) ct_it = response.headers.find("content-type");
        if (ct_it != response.headers.end()) ct = ct_it->second;

        if (do_extract_text) {
            data["content_type"] = "text/plain; charset=utf-8";
            data["extracted_text"] = true;
        } else {
            data["content_type"] = ct;
        }

        result.success = true;
    } else if (response.status_code == 0 && !response.error.empty()) {
        data["error"] = "Request failed: " + response.error;
        result.success = false;
        result.error = data["error"].get<std::string>();
    } else {
        data["error"] = "HTTP request failed with status " + std::to_string(response.status_code);
        // Include response body for error context
        if (!response.body.empty()) {
            std::string err_body = response.body;
            if (err_body.size() > 2000) {
                err_body.resize(2000);
                err_body += "...";
            }
            data["response_body"] = err_body;
        }
        result.success = false;
        result.error = data["error"].get<std::string>();
    }

    result.data = data;
    return result;
}

ToolResult BrowserTool::do_request(const Json& params) {
    ToolResult result;

    // Validate method
    if (!params.contains("method") || !params["method"].is_string()) {
        result.success = false;
        result.error = "Missing required parameter: method (POST, PUT, DELETE, PATCH, HEAD)";
        return result;
    }

    std::string method = params["method"].get<std::string>();
    // Uppercase the method
    for (size_t i = 0; i < method.size(); ++i) {
        method[i] = static_cast<char>(std::toupper(method[i]));
    }

    // Validate URL
    if (!params.contains("url") || !params["url"].is_string()) {
        result.success = false;
        result.error = "Missing required parameter: url";
        return result;
    }

    std::string url = params["url"].get<std::string>();
    
    // Sanitize URL to remove HTML tags and invalid characters
    std::string sanitized_url = sanitize_url(url);
    
    // If sanitization removed everything, return error
    if (sanitized_url.empty()) {
        result.success = false;
        result.error = "Invalid URL: URL contains only invalid characters or HTML tags";
        return result;
    }
    
    // Validate URL format
    if (!starts_with(sanitized_url, "http://") && !starts_with(sanitized_url, "https://")) {
        result.success = false;
        result.error = "URL must start with http:// or https://";
        return result;
    }
    
    // Use the sanitized URL for the request
    url = sanitized_url;

    // Build body
    std::string body;
    std::string content_type;

    if (params.contains("form_data") && params["form_data"].is_object()) {
        // URL-encode the form data
        content_type = "application/x-www-form-urlencoded";
        const Json& form_data = params["form_data"];
        bool first = true;
        for (auto it = form_data.begin(); it != form_data.end(); ++it) {
            if (!first) body += "&";
            first = false;
            // Simple URL encoding for the key and value
            std::string key = it.key();
            std::string val;
            if (it.value().is_string()) {
                val = it.value().get<std::string>();
            } else {
                val = it.value().dump();
            }
            // Use curl for proper encoding via HttpClient's post_form
            // For now, do simple encoding inline
            body += url_encode_simple(key) + "=" + url_encode_simple(val);
        }
    } else if (params.contains("body") && params["body"].is_string()) {
        body = params["body"].get<std::string>();
        content_type = params.value("content_type", std::string("application/json"));
    } else if (params.contains("body") && params["body"].is_object()) {
        body = params["body"].dump();
        content_type = params.value("content_type", std::string("application/json"));
    }

    // Override content_type if explicitly provided
    if (params.contains("content_type") && params["content_type"].is_string()) {
        content_type = params["content_type"].get<std::string>();
    }

    // Extra headers
    std::map<std::string, std::string> headers;
    if (params.contains("headers") && params["headers"].is_object()) {
        const Json& h = params["headers"];
        for (auto it = h.begin(); it != h.end(); ++it) {
            if (it.value().is_string()) {
                headers[it.key()] = it.value().get<std::string>();
            }
        }
    }

    // Proxy
    std::string proxy;
    if (params.contains("proxy") && params["proxy"].is_string()) {
        proxy = params["proxy"].get<std::string>();
    }

    size_t max_len = get_optional_size(params, "max_length", max_content_length_);
    bool extract_text = get_optional_bool(params, "extract_text", false);

    // For form data, use HttpClient's post_form directly for proper URL encoding
    if (params.contains("form_data") && params["form_data"].is_object() && method == "POST") {
        std::map<std::string, std::string> form_map;
        const Json& form_data = params["form_data"];
        for (auto it = form_data.begin(); it != form_data.end(); ++it) {
            if (it.value().is_string()) {
                form_map[it.key()] = it.value().get<std::string>();
            } else {
                form_map[it.key()] = it.value().dump();
            }
        }

        LOG_DEBUG("[Browser] ▶ OUT POST %s (form, %zu fields)", url.c_str(), form_map.size());

        HttpResponse response = http_.post_form(url, form_map, headers);

        LOG_DEBUG("[Browser] ◀ IN  Response from %s: HTTP %ld (%zu bytes)",
                  url.c_str(), response.status_code, response.body.size());

        Json data;
        data["url"] = url;
        data["method"] = "POST";
        data["status_code"] = response.status_code;
        data["success"] = (response.status_code >= 200 && response.status_code < 300);

        Json resp_headers = Json::object();
        for (std::map<std::string, std::string>::const_iterator hi = response.headers.begin();
             hi != response.headers.end(); ++hi) {
            resp_headers[hi->first] = hi->second;
        }
        data["response_headers"] = resp_headers;

        if (response.status_code >= 200 && response.status_code < 300) {
            std::string content = response.body;
            if (extract_text) {
                content = normalize_whitespace(strip_html(content));
            }
            bool truncated = false;
            size_t original_length = content.length();
            if (content.length() > max_len) {
                content.resize(max_len);
                truncated = true;
            }
            data["content"] = content;
            data["content_length"] = static_cast<int64_t>(content.length());
            data["original_length"] = static_cast<int64_t>(original_length);
            data["truncated"] = truncated;

            std::string ct;
            std::map<std::string, std::string>::const_iterator ct_it = response.headers.find("Content-Type");
            if (ct_it == response.headers.end()) ct_it = response.headers.find("content-type");
            if (ct_it != response.headers.end()) ct = ct_it->second;
            data["content_type"] = extract_text ? "text/plain; charset=utf-8" : ct;

            result.success = true;
        } else {
            data["error"] = "HTTP request failed with status " + std::to_string(response.status_code);
            if (!response.body.empty()) {
                std::string err_body = response.body;
                if (err_body.size() > 2000) {
                    err_body.resize(2000);
                    err_body += "...";
                }
                data["response_body"] = err_body;
            }
            result.success = false;
            result.error = data["error"].get<std::string>();
        }

        result.data = data;
        return result;
    }

    // For non-form requests, use perform_browser_request
    return perform_browser_request(method, url, body, content_type, headers, proxy, max_len, extract_text);
}

// ============ Form Extraction ============

ToolResult BrowserTool::do_extract_forms(const Json& params) {
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

    // Extract forms
    std::vector<HtmlForm> forms = extract_html_forms(html, base_url);

    // Build result
    Json forms_array = Json::array();
    for (size_t i = 0; i < forms.size(); ++i) {
        Json form;
        form["action"] = forms[i].action;
        form["method"] = forms[i].method;
        if (!forms[i].id.empty()) form["id"] = forms[i].id;
        if (!forms[i].name.empty()) form["name"] = forms[i].name;

        Json fields_array = Json::array();
        for (size_t j = 0; j < forms[i].fields.size(); ++j) {
            Json field;
            field["name"] = forms[i].fields[j].name;
            field["type"] = forms[i].fields[j].type;
            field["value"] = forms[i].fields[j].value;
            field["required"] = forms[i].fields[j].required;
            fields_array.push_back(field);
        }
        form["fields"] = fields_array;
        form["field_count"] = static_cast<int64_t>(forms[i].fields.size());
        forms_array.push_back(form);
    }

    Json data;
    data["forms"] = forms_array;
    data["count"] = static_cast<int64_t>(forms.size());

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

            if (starts_with(lower_rest, "<script")) {
                in_script = true;
            } else if (starts_with(lower_rest, "</script")) {
                in_script = false;
            } else if (starts_with(lower_rest, "<style")) {
                in_style = true;
            } else if (starts_with(lower_rest, "</style")) {
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

                if (!url.empty() && !starts_with(url, "javascript:") && !starts_with(url, "#")) {
                    links.push_back(std::make_pair(url, text));
                }
            }
        }
        pos++;
    }

    return links;
}

// ============ HTML Form Extraction ============

static std::string extract_attr(const std::string& tag, const std::string& attr_name) {
    // Search for attr_name= in the tag (case-insensitive)
    std::string lower_tag = tag;
    std::string lower_attr = attr_name;
    for (size_t i = 0; i < lower_tag.size(); ++i)
        lower_tag[i] = static_cast<char>(std::tolower(lower_tag[i]));
    for (size_t i = 0; i < lower_attr.size(); ++i)
        lower_attr[i] = static_cast<char>(std::tolower(lower_attr[i]));

    size_t pos = lower_tag.find(lower_attr + "=");
    if (pos == std::string::npos) return "";

    size_t val_start = pos + lower_attr.size() + 1;
    if (val_start >= tag.size()) return "";

    char quote = tag[val_start];
    if (quote == '"' || quote == '\'') {
        val_start++;
        size_t val_end = tag.find(quote, val_start);
        if (val_end != std::string::npos) {
            return tag.substr(val_start, val_end - val_start);
        }
    } else {
        // Unquoted attribute value - ends at space or >
        size_t val_end = val_start;
        while (val_end < tag.size() && tag[val_end] != ' ' && tag[val_end] != '>' && tag[val_end] != '\t') {
            val_end++;
        }
        return tag.substr(val_start, val_end - val_start);
    }
    return "";
}

static bool has_attr(const std::string& tag, const std::string& attr_name) {
    std::string lower_tag = tag;
    std::string lower_attr = attr_name;
    for (size_t i = 0; i < lower_tag.size(); ++i)
        lower_tag[i] = static_cast<char>(std::tolower(lower_tag[i]));
    for (size_t i = 0; i < lower_attr.size(); ++i)
        lower_attr[i] = static_cast<char>(std::tolower(lower_attr[i]));
    return lower_tag.find(lower_attr) != std::string::npos;
}

static std::string make_absolute_url(const std::string& url, const std::string& base_url) {
    if (url.empty()) return base_url;
    if (starts_with(url, "http://") || starts_with(url, "https://")) return url;
    if (url[0] == '/') {
        size_t scheme_end = base_url.find("://");
        if (scheme_end != std::string::npos) {
            size_t domain_end = base_url.find('/', scheme_end + 3);
            if (domain_end != std::string::npos) {
                return base_url.substr(0, domain_end) + url;
            } else {
                return base_url + url;
            }
        }
    }
    // Relative URL - append to base path
    if (!base_url.empty()) {
        size_t last_slash = base_url.rfind('/');
        size_t scheme_end = base_url.find("://");
        if (last_slash != std::string::npos && scheme_end != std::string::npos && last_slash > scheme_end + 2) {
            return base_url.substr(0, last_slash + 1) + url;
        }
        return base_url + "/" + url;
    }
    return url;
}

std::vector<BrowserTool::HtmlForm> BrowserTool::extract_html_forms(
        const std::string& html, const std::string& base_url) {
    std::vector<HtmlForm> forms;

    std::string lower_html = html;
    for (size_t i = 0; i < lower_html.size(); ++i) {
        lower_html[i] = static_cast<char>(std::tolower(lower_html[i]));
    }

    size_t pos = 0;
    while ((pos = lower_html.find("<form", pos)) != std::string::npos) {
        // Find the end of the <form> opening tag
        size_t tag_end = html.find('>', pos);
        if (tag_end == std::string::npos) break;

        std::string form_tag = html.substr(pos, tag_end - pos + 1);

        // Find the closing </form>
        size_t form_close = lower_html.find("</form>", tag_end);
        if (form_close == std::string::npos) form_close = html.size();

        std::string form_content = html.substr(tag_end + 1, form_close - tag_end - 1);
        std::string lower_form = lower_html.substr(tag_end + 1, form_close - tag_end - 1);

        HtmlForm form;
        form.action = make_absolute_url(extract_attr(form_tag, "action"), base_url);
        form.method = extract_attr(form_tag, "method");
        if (form.method.empty()) form.method = "GET";
        // Uppercase the method
        for (size_t i = 0; i < form.method.size(); ++i)
            form.method[i] = static_cast<char>(std::toupper(form.method[i]));
        form.id = extract_attr(form_tag, "id");
        form.name = extract_attr(form_tag, "name");

        // Extract <input> fields
        size_t input_pos = 0;
        while ((input_pos = lower_form.find("<input", input_pos)) != std::string::npos) {
            size_t input_end = form_content.find('>', input_pos);
            if (input_end == std::string::npos) break;

            std::string input_tag = form_content.substr(input_pos, input_end - input_pos + 1);

            FormField field;
            field.name = extract_attr(input_tag, "name");
            field.type = extract_attr(input_tag, "type");
            if (field.type.empty()) field.type = "text";
            // Lowercase type
            for (size_t i = 0; i < field.type.size(); ++i)
                field.type[i] = static_cast<char>(std::tolower(field.type[i]));
            field.value = extract_attr(input_tag, "value");
            field.required = has_attr(input_tag, "required");

            if (!field.name.empty()) {
                form.fields.push_back(field);
            }

            input_pos = input_end + 1;
        }

        // Extract <textarea> fields
        size_t ta_pos = 0;
        while ((ta_pos = lower_form.find("<textarea", ta_pos)) != std::string::npos) {
            size_t ta_tag_end = form_content.find('>', ta_pos);
            if (ta_tag_end == std::string::npos) break;

            std::string ta_tag = form_content.substr(ta_pos, ta_tag_end - ta_pos + 1);

            size_t ta_close = lower_form.find("</textarea>", ta_tag_end);
            std::string ta_value;
            if (ta_close != std::string::npos) {
                ta_value = form_content.substr(ta_tag_end + 1, ta_close - ta_tag_end - 1);
                // Trim whitespace
                size_t start = ta_value.find_first_not_of(" \t\n\r");
                size_t end = ta_value.find_last_not_of(" \t\n\r");
                if (start != std::string::npos && end != std::string::npos)
                    ta_value = ta_value.substr(start, end - start + 1);
                else
                    ta_value.clear();
            }

            FormField field;
            field.name = extract_attr(ta_tag, "name");
            field.type = "textarea";
            field.value = ta_value;
            field.required = has_attr(ta_tag, "required");

            if (!field.name.empty()) {
                form.fields.push_back(field);
            }

            ta_pos = ta_tag_end + 1;
        }

        // Extract <select> fields
        size_t sel_pos = 0;
        while ((sel_pos = lower_form.find("<select", sel_pos)) != std::string::npos) {
            size_t sel_tag_end = form_content.find('>', sel_pos);
            if (sel_tag_end == std::string::npos) break;

            std::string sel_tag = form_content.substr(sel_pos, sel_tag_end - sel_pos + 1);

            size_t sel_close = lower_form.find("</select>", sel_tag_end);
            std::string sel_content;
            if (sel_close != std::string::npos) {
                sel_content = form_content.substr(sel_tag_end + 1, sel_close - sel_tag_end - 1);
            }

            FormField field;
            field.name = extract_attr(sel_tag, "name");
            field.type = "select";
            field.required = has_attr(sel_tag, "required");

            // Find the selected option's value, or the first option
            std::string lower_sel = sel_content;
            for (size_t i = 0; i < lower_sel.size(); ++i)
                lower_sel[i] = static_cast<char>(std::tolower(lower_sel[i]));

            // Look for selected option first
            size_t selected_pos = lower_sel.find("selected");
            if (selected_pos != std::string::npos) {
                // Find the <option> tag containing "selected"
                size_t opt_start = lower_sel.rfind("<option", selected_pos);
                if (opt_start != std::string::npos) {
                    size_t opt_tag_end = sel_content.find('>', opt_start);
                    if (opt_tag_end != std::string::npos) {
                        std::string opt_tag = sel_content.substr(opt_start, opt_tag_end - opt_start + 1);
                        field.value = extract_attr(opt_tag, "value");
                    }
                }
            } else {
                // Use first option
                size_t first_opt = lower_sel.find("<option");
                if (first_opt != std::string::npos) {
                    size_t opt_tag_end = sel_content.find('>', first_opt);
                    if (opt_tag_end != std::string::npos) {
                        std::string opt_tag = sel_content.substr(first_opt, opt_tag_end - first_opt + 1);
                        field.value = extract_attr(opt_tag, "value");
                    }
                }
            }

            if (!field.name.empty()) {
                form.fields.push_back(field);
            }

            sel_pos = sel_tag_end + 1;
        }

        // Extract <button type="submit"> as submit fields
        size_t btn_pos = 0;
        while ((btn_pos = lower_form.find("<button", btn_pos)) != std::string::npos) {
            size_t btn_tag_end = form_content.find('>', btn_pos);
            if (btn_tag_end == std::string::npos) break;

            std::string btn_tag = form_content.substr(btn_pos, btn_tag_end - btn_pos + 1);
            std::string btn_type = extract_attr(btn_tag, "type");
            for (size_t i = 0; i < btn_type.size(); ++i)
                btn_type[i] = static_cast<char>(std::tolower(btn_type[i]));

            std::string btn_name = extract_attr(btn_tag, "name");
            if (!btn_name.empty() && (btn_type.empty() || btn_type == "submit")) {
                FormField field;
                field.name = btn_name;
                field.type = "submit";
                field.value = extract_attr(btn_tag, "value");
                field.required = false;
                form.fields.push_back(field);
            }

            btn_pos = btn_tag_end + 1;
        }

        forms.push_back(form);
        pos = (form_close != html.size()) ? form_close + 7 : html.size();
    }

    return forms;
}

} // namespace opencrank