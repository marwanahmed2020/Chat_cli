#include "http.h"

#include <string>

namespace {
std::string build_ok(const std::string& body, const std::string& content_type) {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: " +
           content_type + "\r\n"
                          "Content-Length: " +
           std::to_string(body.size()) + "\r\n"
                                        "Connection: close\r\n"
                                        "\r\n" +
           body;
}

std::string build_not_found() {
    const std::string body = "404 Not Found";
    return "HTTP/1.1 404 Not Found\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: " +
           std::to_string(body.size()) + "\r\n"
                                        "Connection: close\r\n"
                                        "\r\n" +
           body;
}
}  // namespace

std::string get_path(const std::string& request) {
    size_t method_end = request.find(' ');
    if (method_end == std::string::npos) {
        return "/";
    }

    size_t path_start = method_end + 1;
    size_t path_end = request.find(' ', path_start);
    if (path_end == std::string::npos || path_end <= path_start) {
        return "/";
    }

    return request.substr(path_start, path_end - path_start);
}

std::string build_http_response(const std::string& request) {
    const std::string path = get_path(request);

    if (path == "/") {
        const std::string body = "Hello from C++ HTTP Server!";
        return build_ok(body, "text/plain");
    }

    return build_not_found();
}