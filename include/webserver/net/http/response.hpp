#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace webserver {
namespace net {
namespace http {

// HTTP Status codes
enum class Status {
    OK = 200,
    Created = 201,
    Accepted = 202,
    NoContent = 204,
    MovedPermanently = 301,
    Found = 302,
    NotModified = 304,
    BadRequest = 400,
    Unauthorized = 401,
    Forbidden = 403,
    NotFound = 404,
    MethodNotAllowed = 405,
    InternalServerError = 500,
    NotImplemented = 501,
    BadGateway = 502,
    ServiceUnavailable = 503
};

// Convert status to string
inline std::string status_to_string(Status status) {
    switch (status) {
        case Status::OK: return "OK";
        case Status::Created: return "Created";
        case Status::Accepted: return "Accepted";
        case Status::NoContent: return "No Content";
        case Status::MovedPermanently: return "Moved Permanently";
        case Status::Found: return "Found";
        case Status::NotModified: return "Not Modified";
        case Status::BadRequest: return "Bad Request";
        case Status::Unauthorized: return "Unauthorized";
        case Status::Forbidden: return "Forbidden";
        case Status::NotFound: return "Not Found";
        case Status::MethodNotAllowed: return "Method Not Allowed";
        case Status::InternalServerError: return "Internal Server Error";
        case Status::NotImplemented: return "Not Implemented";
        case Status::BadGateway: return "Bad Gateway";
        case Status::ServiceUnavailable: return "Service Unavailable";
        default: return "Unknown";
    }
}

// HTTP Response structure
struct Response {
    std::string version = "HTTP/1.1";
    Status status_code = Status::OK;
    std::unordered_map<std::string, std::string> headers;
    std::vector<uint8_t> body_data;

    // Set content type header (chainable)
    Response& content_type(const std::string& mime_type) {
        headers["Content-Type"] = mime_type;
        return *this;
    }

    // Set content length header (chainable)
    Response& content_length(size_t length) {
        headers["Content-Length"] = std::to_string(length);
        return *this;
    }

    // Set status (chainable)
    Response& set_status(Status s) {
        status_code = s;
        return *this;
    }

    // Set header (chainable)
    Response& set_header(const std::string& name, const std::string& value) {
        headers[name] = value;
        return *this;
    }

    // Set body from string (chainable)
    Response& set_body(const std::string& str) {
        body_data.assign(str.begin(), str.end());
        content_length(body_data.size());
        return *this;
    }

    // Set HTML body (chainable)
    Response& html(const std::string& str) {
        content_type("text/html");
        set_body(str);
        return *this;
    }

};

} // namespace http
} // namespace net
} // namespace webserver
