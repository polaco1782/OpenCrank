#include <opencrank/core/http_client.hpp>
#include <opencrank/core/logger.hpp>
#include <cstring>
#include <sstream>

namespace opencrank {

HttpClient::HttpClient() : curl_(nullptr), timeout_ms_(60000) {
    curl_ = curl_easy_init();
}

HttpClient::~HttpClient() {
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
}

void HttpClient::set_timeout(long ms) { 
    timeout_ms_ = ms; 
}

void HttpClient::set_proxy(const std::string& proxy_url) {
    proxy_url_ = proxy_url;
}

void HttpClient::clear_proxy() {
    proxy_url_.clear();
}

HttpResponse HttpClient::get(const std::string& url, 
                             const std::map<std::string, std::string>& headers,
                             const std::string& proxy) {
    return perform_request("GET", url, "", headers, proxy);
}

HttpResponse HttpClient::post_json(const std::string& url, 
                                   const Json& body,
                                   const std::map<std::string, std::string>& extra_headers) {
    std::map<std::string, std::string> headers = extra_headers;
    headers["Content-Type"] = "application/json";
    return perform_request("POST", url, body.dump(), headers, "");
}

HttpResponse HttpClient::post_json(const std::string& url, 
                                   const std::string& body,
                                   const std::map<std::string, std::string>& extra_headers) {
    std::map<std::string, std::string> headers = extra_headers;
    headers["Content-Type"] = "application/json";
    return perform_request("POST", url, body, headers, "");
}

HttpResponse HttpClient::post_form(const std::string& url,
                                   const std::map<std::string, std::string>& form_data,
                                   const std::map<std::string, std::string>& extra_headers) {
    std::map<std::string, std::string> headers = extra_headers;
    headers["Content-Type"] = "application/x-www-form-urlencoded";
    
    std::string body;
    for (std::map<std::string, std::string>::const_iterator it = form_data.begin();
         it != form_data.end(); ++it) {
        if (!body.empty()) body += "&";
        body += url_encode(curl_, it->first) + "=" + url_encode(curl_, it->second);
    }
    return perform_request("POST", url, body, headers, "");
}

HttpResponse HttpClient::request(const std::string& method,
                                 const std::string& url,
                                 const std::string& body,
                                 const std::map<std::string, std::string>& headers,
                                 const std::string& proxy) {
    return perform_request(method, url, body, headers, proxy);
}

size_t HttpClient::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    std::string* response_body = static_cast<std::string*>(userdata);
    response_body->append(ptr, total);
    return total;
}

size_t HttpClient::header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total = size * nitems;
    std::map<std::string, std::string>* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    
    std::string line(buffer, total);
    
    // Remove trailing \r\n
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    
    // Find the colon separator
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        
        // Trim leading whitespace from value
        size_t start = value.find_first_not_of(" \t");
        if (start != std::string::npos) {
            value = value.substr(start);
        }
        
        (*headers)[key] = value;
    }
    
    return total;
}

std::string HttpClient::url_encode(CURL* curl, const std::string& s) {
    if (!curl) {
        // Fallback encoding
        std::string result;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
                result += c;
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
                result += buf;
            }
        }
        return result;
    }
    
    char* encoded = curl_easy_escape(curl, s.c_str(), static_cast<int>(s.length()));
    if (!encoded) return s;
    std::string result(encoded);
    curl_free(encoded);
    return result;
}

HttpResponse HttpClient::perform_request(const std::string& method,
                                         const std::string& url,
                                         const std::string& body,
                                         const std::map<std::string, std::string>& headers,
                                         const std::string& proxy) {
    HttpResponse resp;
    
    if (!curl_) {
        resp.error = "CURL not initialized";
        return resp;
    }
    
    // Reset curl handle for reuse
    curl_easy_reset(curl_);
    
    // Set URL
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    
    // Determine which proxy to use: parameter overrides instance proxy
    std::string effective_proxy = proxy.empty() ? proxy_url_ : proxy;
    
    if (!effective_proxy.empty()) {
        LOG_DEBUG("[HTTP] ▶ OUT %s %s (body: %zu bytes, proxy: %s)", 
                  method.c_str(), url.c_str(), body.size(), effective_proxy.c_str());
        curl_easy_setopt(curl_, CURLOPT_PROXY, effective_proxy.c_str());
    } else {
        LOG_DEBUG("[HTTP] ▶ OUT %s %s (body: %zu bytes)", 
                  method.c_str(), url.c_str(), body.size());
    }
    
    // Set timeout
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, timeout_ms_);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms_ / 2);
    
    // Set method
    if (method == "POST") {
        curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    } else if (method == "PUT") {
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "PUT");
    } else if (method == "DELETE") {
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (method == "PATCH") {
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "PATCH");
    } else if (method == "HEAD") {
        curl_easy_setopt(curl_, CURLOPT_NOBODY, 1L);
    }
    // GET is the default
    
    // Set request body
    if (!body.empty()) {
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.length()));
    }
    
    // Set headers
    struct curl_slist* header_list = nullptr;
    for (std::map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it) {
        std::string header = it->first + ": " + it->second;
        header_list = curl_slist_append(header_list, header.c_str());
    }
    if (header_list) {
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, header_list);
    }
    
    // Set response callbacks
    std::string response_body;
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_body);
    
    std::map<std::string, std::string> response_headers;
    curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl_, CURLOPT_HEADERDATA, &response_headers);
    
    // Follow redirects
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_, CURLOPT_MAXREDIRS, 5L);
    
    // SSL verification (enabled by default)
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);

    // Perform the request
    CURLcode res = curl_easy_perform(curl_);
    
    // Cleanup headers
    if (header_list) {
        curl_slist_free_all(header_list);
    }
    
    if (res != CURLE_OK) {
        LOG_DEBUG("[HTTP] ◀ IN  %s %s FAILED: %s", method.c_str(), url.c_str(), curl_easy_strerror(res));
        resp.error = curl_easy_strerror(res);
        return resp;
    }
    
    // Get status code
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &resp.status_code);
    
    resp.body = response_body;
    resp.headers = response_headers;
    
    LOG_DEBUG("[HTTP] ◀ IN  %s %s -> HTTP %ld (%zu bytes)", 
              method.c_str(), url.c_str(), resp.status_code, resp.body.size());
    
    if (!resp.ok() && resp.error.empty()) {
        std::ostringstream oss;
        oss << "HTTP " << resp.status_code;
        if (!resp.body.empty()) {
            std::string snippet = resp.body;
            if (snippet.size() > 512) {
                snippet.resize(512);
                snippet += "...";
            }
            oss << ": " << snippet;
        }
        resp.error = oss.str();
    }
    
    return resp;
}

} // namespace opencrank
