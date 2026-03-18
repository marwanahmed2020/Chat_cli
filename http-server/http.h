#ifndef HTTP_H
#define HTTP_H

#include <string>

std::string get_path(const std::string& request);
std::string build_http_response(const std::string& request);

#endif