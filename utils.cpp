#include "utils.h"

#include <fstream>
#include <iostream>
#include <sstream>

void log_request(const std::string& request) {
    std::cout << "----- Incoming Request -----" << std::endl;
    std::cout << request << std::endl;
    std::cout << "----------------------------" << std::endl;
}

std::string read_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}