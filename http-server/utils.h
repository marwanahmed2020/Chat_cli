#ifndef UTILS_H
#define UTILS_H

#include <string>

void log_request(const std::string& request);
std::string read_file(const std::string& filename);

#endif