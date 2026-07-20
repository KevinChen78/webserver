#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace webserver {
namespace net {
namespace http {

// HTTP Method enum
enum class Method {
    GET,
    POST,
    PUT,
    DELETE,
    HEAD,
    OPTIONS,
    PATCH,
    TRACE,
    CONNECT,
    UNKNOWN
};

// Parse method from string
inline Method parse_method(const std::string& method_str) {
    if (method_str == "GET") return Method::GET;
    if (method_str == "POST") return Method::POST;
    if (method_str == "PUT") return Method::PUT;
    if (method_str == "DELETE") return Method::DELETE;
    if (method_str == "HEAD") return Method::HEAD;
    if (method_str == "OPTIONS") return Method::OPTIONS;
    if (method_str == "PATCH") return Method::PATCH;
    if (method_str == "TRACE") return Method::TRACE;
    if (method_str == "CONNECT") return Method::CONNECT;
    return Method::UNKNOWN;
}

// Convert method to string
inline std::string method_to_string(Method method) {
    switch (method) {
        case Method::GET: return "GET";
        case Method::POST: return "POST";
        case Method::PUT: return "PUT";
        case Method::DELETE: return "DELETE";
        case Method::HEAD: return "HEAD";
        case Method::OPTIONS: return "OPTIONS";
        case Method::PATCH: return "PATCH";
        case Method::TRACE: return "TRACE";
        case Method::CONNECT: return "CONNECT";
        default: return "UNKNOWN";
    }
}

// HTTP Request structure
struct Request {
    Method method = Method::GET;
    std::string uri;
    std::string version = "HTTP/1.1";
    std::unordered_map<std::string, std::string> headers;
    std::vector<uint8_t> body;

    // Get request path (alias for uri)
    std::string path() const { return uri; }

    // Check if header exists
    bool has_header(const std::string& name) const {
        return !get_header(name).empty();
    }

    // Get header value (case-insensitive)
    std::string get_header(const std::string& name) const {
        for (const auto& [key, value] : headers) {
            if (key.size() == name.size()) {
                bool match = true;
                for (size_t i = 0; i < key.size(); ++i) {
                    if (std::tolower(key[i]) != std::tolower(name[i])) {
                        match = false;
                        break;
                    }
                }
                if (match) return value;
            }
        }
        return "";
    }

    // Check if connection should be kept alive
    bool keep_alive() const {
        auto conn = get_header("Connection");
        if (version == "HTTP/1.1") {
            return conn != "close";
        }
        return conn == "keep-alive";
    }

    // Parse HTTP request from raw string
    bool parse(const std::string& raw) {
        std::istringstream stream(raw);
        std::string line;

        // Parse request line
        if (!std::getline(stream, line)) return false;

        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::istringstream req_line(line);
        std::string method_str;
        req_line >> method_str >> uri >> version;

        method = parse_method(method_str);

        // Parse headers
        while (std::getline(stream, line)) {
            // Remove \r if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // Empty line marks end of headers
            if (line.empty()) break;

            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = line.substr(0, colon_pos);
                std::string value = line.substr(colon_pos + 1);

                // Trim whitespace from value
                size_t start = value.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    value = value.substr(start);
                }

                headers[key] = value;
            }
        }

        return true;
    }
};

} // namespace http
} // namespace net
} // namespace webserver
