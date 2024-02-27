#pragma once

#include <variant>
#include <string>

// The MIR, or "mid-level intermediate representation", describes the imperative
// instructions of the LA program at a type-aware level. Each function is
// a control flow graph of BasicBlocks which contain lists of elementary
// type-aware operations as well as transitions to other BasicBlocks.
namespace mir {
	struct Type {
		struct VoidType {};
		struct ArrayType { int num_dimensions; };
		struct TupleType {};
		struct CodeType {};
		using Variant = std::variant<VoidType, ArrayType, TupleType, CodeType>;
		Variant type;

		std::string to_string() const;
	};
}