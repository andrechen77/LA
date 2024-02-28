#include "mir.h"
#include <iostream>
#include <algorithm>

namespace mir {
	std::string Type::to_ir_syntax() const {
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

	std::string LocalVar::to_ir_syntax() const {
		return "%" + this->get_unambiguous_name();
	}
	std::string LocalVar::get_unambiguous_name() const {
		return "var_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "_" + this->user_given_name;
	}
	std::string LocalVar::get_declaration() const {
		return this->type.to_ir_syntax() + " " + this->to_ir_syntax();
	}

	std::string Place::to_ir_syntax() const {
		std::string result = this->target->to_ir_syntax();
		for (const Uptr<Operand> &index : this->indices) {
			result += "[" + index->to_ir_syntax() + "]";
		}
		return result;
	}

	std::string Int64Constant::to_ir_syntax() const {
		return std::to_string(this->value);
	}

	std::string CodeConstant::to_ir_syntax() const {
		return "@" + this->value->get_unambiguous_name();
	}

	std::string to_string(Operator op) {
		static const std::string map[] = {
			"<", "<=", "=", ">=", ">", "+", "-", "*", "&", "<<", ">>"
		};
		return map[static_cast<int>(op)];
	}

	std::string BinaryOperation::to_ir_syntax() const {
		return this->lhs->to_ir_syntax() + " "
			+ mir::to_string(this->op) + " "
			+ this->rhs->to_ir_syntax();
	}

	std::string LengthGetter::to_ir_syntax() const {
		std::string result = "length " + this->target->to_ir_syntax();
		if (this->dimension.has_value()) {
			result += " " + this->dimension.value()->to_ir_syntax();
		}
		return result;
	}

	std::string Instruction::to_ir_syntax() const {
		std::string result;
		if (this->destination.has_value()) {
			result += this->destination.value()->to_ir_syntax() + " <- ";
		}
		result += this->rvalue->to_ir_syntax();
		return result;
	}

	std::string FunctionCall::to_ir_syntax() const {
		std::string result = this->callee->to_ir_syntax() + "(";
		for (const Uptr<Operand> &arg : this->arguments) {
			result += arg->to_ir_syntax() + ", ";
		}
		result += ")";
		return result;
	}

	std::string BasicBlock::to_ir_syntax() const {
		std::string result;
		result += "\t:" + this->get_unambiguous_name() + "\n";
		for (const Uptr<Instruction> &inst : this->instructions) {
			result += "\t" + inst->to_ir_syntax() + "\n";
		}

		if (std::get_if<ReturnVoid>(&this->terminator)) {
			result += "\treturn\n";
		} else if (const ReturnVal *term = std::get_if<ReturnVal>(&this->terminator)) {
			result += "\treturn " + term->return_value->to_ir_syntax() + "\n";
		} else if (const Goto *term = std::get_if<Goto>(&this->terminator)) {
			result += "\tbr :" + term->successor->user_given_label_name + "\n";
		} else if (const Branch *term = std::get_if<Branch>(&this->terminator)) {
			result += "\tbr "
				+ term->condition->to_ir_syntax()
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

	std::string FunctionDef::to_ir_syntax() const {
		std::string result = this->return_type.to_ir_syntax() + " " + this->user_given_name + "(";
		for (const LocalVar *parameter_var : this->parameter_vars) {
			result += parameter_var->get_declaration() + ", ";
		}
		result += ") {\n";

		// output a fake "basic block" just to hold the declarations
		// TODO just have these declarations be output in the first basic block
		result += "\t:block_entry\n";
		for (const Uptr<LocalVar> &local_var : this->local_vars) {
			if (std::find(this->parameter_vars.begin(), this->parameter_vars.end(), local_var.get()) != this->parameter_vars.end()) continue;
			result += "\t" + local_var->get_declaration() + "\n";
		}
		result += "\tbr :" + this->basic_blocks.at(0)->get_unambiguous_name() + "\n\n";

		for (const Uptr<BasicBlock> &block : this->basic_blocks) {
			result += block->to_ir_syntax() + "\n";
		}
		result += "}\n";
		return result;
	}
	std::string FunctionDef::get_unambiguous_name() const {
		// the user-given name is already unambiguous
		return this->user_given_name;
	}

	std::string Program::to_ir_syntax() const {
		std::string result;
		for (const Uptr<FunctionDef> &function_def : this->function_defs) {
			result += function_def->to_ir_syntax() + "\n";
		}
		return result;
	}
}