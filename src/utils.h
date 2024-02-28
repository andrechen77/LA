#pragma once

#include "std_alias.h"
#include <charconv>

namespace utils {
    using namespace std_alias;

	template<typename T>
    T string_view_to_int(std::string_view view) {
        T result;
        auto start = view.data();
        auto end = view.data() + view.size();
        if (view.front() == '+') {
            start += 1;
        }
        auto [ptr, ec] = std::from_chars(start, end, result);
        // TODO check for error
        return result;
    }

    template<typename T, std::string to_str(const T &)>
    std::string to_string(const Opt<T> &val) {
        if (val) {
            return to_str(*val);
        } else {
            return "None";
        }
    }

    template<typename Iterable, typename ToString>
    std::string format_comma_delineated_list(const Iterable &list, ToString to_string) {
        std::string result;
        bool first = true;
		for (const auto &element : list) {
			if (first) {
				first = false;
			} else {
				result += ", ";
			}
			result += to_string(element);
		}
        return result;
    }
}
