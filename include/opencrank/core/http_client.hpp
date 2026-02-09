#ifndef opencrank_CORE_HTTP_CLIENT_HPP
#define opencrank_CORE_HTTP_CLIENT_HPP

#include "json.hpp"
#include <string>
#include <vector>
#include <map>
#include <curl/curl.h>

namespace opencrank {

// HTTP response structure
struct HttpResponse {
    long status_code;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string error;
    
    HttpResponse() : status_code(0) {}
    
    bool ok() const { return status_code >= 200 && status_code < 300; }
    
    Json json() const {
        try {
            return Json::parse(body);
        } catch (...) {
            return Json();
        }
    }
};

// HTTP client using libcurl
class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    
    void set_timeout(long ms);
    long timeout() const { return timeout_ms_; }
    
    void set_proxy(const std::string& proxy_url);
    void clear_proxy();
    
    // GET request
    HttpResponse get(const std::string& url, 
                     const std::map<std::string, std::string>& headers = std::map<std::string, std::string>(),
                     const std::string& proxy = "");
    
    // POST request with JSON body
    HttpResponse post_json(const std::string& url, 
                           const Json& body,
                           const std::map<std::string, std::string>& extra_headers = std::map<std::string, std::string>());
    
    // POST request with string body
    HttpResponse post_json(const std::string& url, 
                           const std::string& body,
                           const std::map<std::string, std::string>& extra_headers = std::map<std::string, std::string>());
    
    // POST request with form data
    HttpResponse post_form(const std::string& url,
                           const std::map<std::string, std::string>& form_data,
                           const std::map<std::string, std::string>& extra_headers = std::map<std::string, std::string>());

private:
    CURL* curl_;
    long timeout_ms_;
    std::string proxy_url_;
    
    HttpResponse perform_request(const std::string& method,
                                 const std::string& url,
                                 const std::string& body,
                                 const std::map<std::string, std::string>& headers,
                                 const std::string& proxy = "");
    
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata);
    static std::string url_encode(CURL* curl, const std::string& s);
};

} // namespace opencrank

#endif // opencrank_CORE_HTTP_CLIENT_HPP
