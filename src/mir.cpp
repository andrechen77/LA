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

	std::string LocalVar::get_unambiguous_name() const {
		return "var_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "_" + this->user_given_name;
	}

	std::string Place::to_string() const {
		std::string result = this->target->get_unambiguous_name();
		for (const Uptr<Operand> &index : this->indices) {
			result += "[" + index->to_string() + "]";
		}
		return result;
	}

	std::string Int64Constant::to_string() const {
		return std::to_string(this->value);
	}

	std::string CodeConstant::to_string() const {
		return this->value->get_unambiguous_name();
	}

	std::string Instruction::to_string() const {
		// TODO fill out
		return "mir::Instruction here";
	}

	std::string BasicBlock::to_string() const {
		std::string result;
		result += "\t:" + this->get_unambiguous_name() + "\n";
		for (const Uptr<Instruction> &inst : this->instructions) {
			result += "\t" + inst->to_string() + "\n";
		}

		if (std::get_if<ReturnVoid>(&this->terminator)) {
			result += "\treturn\n";
		} else if (const ReturnVal *term = std::get_if<ReturnVal>(&this->terminator)) {
			result += "\treturn " + term->return_value->to_string() + "\n";
		} else if (const Goto *term = std::get_if<Goto>(&this->terminator)) {
			result += "\tbr :" + term->successor->user_given_label_name + "\n";
		} else if (const Branch *term = std::get_if<Branch>(&this->terminator)) {
			result += "\tbr "
				+ term->condition->to_string()
				+ " :" + term->then_block->get_unambiguous_name()
				+ " :" + term->else_block->get_unambiguous_name()
				+ "\n";
		} else {
			std::cerr << "Logic error: inexhaustive match on Terminator variant\n";
			exit(1);
		}

		return result;
	}
	std::string BasicBlock::get_unambiguous_name() const {
		return "block_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "_" + this->user_given_label_name;
	}

	std::string FunctionDef::to_string() const {
		std::string result = this->return_type.to_string() + " " + this->user_given_name + "(";
		for (const LocalVar *parameter_var : this->parameter_vars) {
			result += parameter_var->user_given_name + ", ";
		}
		result += ") {\n";

		// output a fake "basic block" just to hold the declarations
		result += "\t:block_entry\n";
		for (const Uptr<LocalVar> &local_var : this->local_vars) {
			result += "\t" + local_var->type.to_string() + " " + local_var->get_unambiguous_name() + "\n";
		}
		result += "\tbr :" + this->basic_blocks.at(0)->get_unambiguous_name() + "\n\n";

		for (const Uptr<BasicBlock> &block : this->basic_blocks) {
			result += block->to_string() + "\n";
		}
		result += "}\n";
		return result;
	}
	std::string FunctionDef::get_unambiguous_name() const {
		// the user-given name is already unambiguous
		return this->user_given_name;
	}

	std::string Program::to_string() const {
		std::string result;
		for (const Uptr<FunctionDef> &function_def : this->function_defs) {
			result += function_def->to_string() + "\n";
		}
		return result;
	}
}