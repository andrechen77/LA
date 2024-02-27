#include "mir.h"
#include <iostream>

namespace mir {
	std::string Type::to_string() const {
		const Variant *x = &this->type;
		if (std::get_if<VoidType>(x)) {
			return "void";
		} else if (const ArrayType *array_type = std::get_if<ArrayType>(x)) {
			std::string result = "int64";
			for (int i = 0; i < array_type->num_dimensions; ++i) {
				result += "[]";
			}
			return result;
		} else if (std::get_if<TupleType>(x)) {
			return "tuple";
		} else if (std::get_if<CodeType>(x)) {
			return "code";
		} else {
			std::cerr << "Logic error: inexhaustive Type variant\n";
			exit(1);
		}
	}
}