#include "util.h"
#include "fiber.h"
#include "scheduler.h"

namespace fisher {

uint64_t GetThreadId() { return Scheduler::GetThreadId(); }

uint64_t GetFiberId() { return Fiber::GetFiberId(); }

std::string GetThreadName() { return Scheduler::GetThreadName(); }

uint64_t GetCurrentMS() {
  auto now = std::chrono::system_clock::now();
  auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
  auto value = now_ms.time_since_epoch().count();
  return value;
}

void format_parser(std::vector<std::tuple<std::string, std::string, int>>& vec, std::string pattern) {
    std::string nstr;
    for(size_t i = 0; i < pattern.size(); ++i) {
        if(pattern[i] != '%') {
            nstr.append(1, pattern[i]);
            continue;
        }

        if((i + 1) < pattern.size()) {
            if(pattern[i + 1] == '%') {
                nstr.append(1, '%');
                continue;
            }
        }

        size_t n = i + 1;
        int fmt_status = 0;
        size_t fmt_begin = 0;

        std::string str;
        std::string fmt;
        while(n < pattern.size()) {
            if(!fmt_status && (!isalpha(pattern[n]) && pattern[n] != '{'
                    && pattern[n] != '}')) {
                str = pattern.substr(i + 1, n - i - 1);
                break;
            }
            if(fmt_status == 0) {
                if(pattern[n] == '{') {
                    str = pattern.substr(i + 1, n - i - 1);
                    //std::cout << "*" << str << std::endl;
                    fmt_status = 1; //解析格式
                    fmt_begin = n;
                    ++n;
                    continue;
                }
            } else if(fmt_status == 1) {
                if(pattern[n] == '}') {
                    fmt = pattern.substr(fmt_begin + 1, n - fmt_begin - 1);
                    //std::cout << "#" << fmt << std::endl;
                    fmt_status = 0;
                    ++n;
                    break;
                }
            }
            ++n;
            if(n == pattern.size()) {
                if(str.empty()) {
                    str = pattern.substr(i + 1);
                }
            }
        }

        if(fmt_status == 0) {
            if(!nstr.empty()) {
                vec.push_back(std::make_tuple(nstr, std::string(), 0));
                nstr.clear();
            }
            vec.push_back(std::make_tuple(str, fmt, 1));
            i = n - 1;
        } else if(fmt_status == 1) {
            vec.push_back(std::make_tuple("<<pattern_error>>", fmt, 0));
        }
    }

    if(!nstr.empty()) {
        vec.push_back(std::make_tuple(nstr, "", 0));
    }
}
}