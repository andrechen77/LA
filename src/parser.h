#pragma once

#include "std_alias.h"
#include "program.h"
#include <memory>
#include <optional>

namespace La::parser {
	using namespace std_alias;

	Uptr<La::program::Program> parse_file(char *fileName, Opt<std::string> parse_tree_output);
}