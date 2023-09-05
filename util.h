#pragma once
#include <thread>
#include <vector>
#include <string>
#include <tuple>

namespace fisher {

uint64_t GetThreadId();

uint64_t GetFiberId();

std::string GetThreadName();

uint64_t GetCurrentMS();

void format_parser(std::vector<std::tuple<std::string, std::string, int>>& vec, std::string pattern);

}