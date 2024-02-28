#include "hir_to_mir.h"
#include "std_alias.h"
#include <assert.h>

namespace La::hir_to_mir {
	using namespace std_alias;

	Uptr<mir::Operand> convert_expr_to_operand(
		const Uptr<hir::Expr> &expr,
		const Map<hir::LaFunction *, mir::FunctionDef *> &func_map,
		const Map<hir::Variable *, mir::LocalVar *> &var_map
	) {
		if (const hir::ItemRef<hir::Nameable> *item_ref = dynamic_cast<hir::ItemRef<hir::Nameable> *>(expr.get())) {
			hir::Nameable *referent = item_ref->get_referent().value();
			if (hir::Variable *hir_var = dynamic_cast<hir::Variable *>(referent)) {
				mir::LocalVar *mir_var = var_map.at(hir_var);
				return mkuptr<mir::Place>(
					mir_var,
					Vec<Uptr<mir::Operand>> {}
				);
			} else if (hir::LaFunction *hir_func = dynamic_cast<hir::LaFunction *>(referent)) {
				mir::FunctionDef *mir_func = func_map.at(hir_func);
				return mkuptr<mir::CodeConstant>(mir_func);
			} else {
				std::cerr << "Logic Error: probably forgot to handle case of hir::ExternalFunction\n";
				exit(1);
			}
		} else if (const hir::NumberLiteral *num_lit = dynamic_cast<hir::NumberLiteral *>(expr.get())) {
			return mkuptr<mir::Int64Constant>(num_lit->value);
		} else {
			std::cerr << "Logic Error: this expression is too complex to be converted to an mir::Operand\n";
			exit(1);
		}
	}

	class InstructionAdder : public hir::InstructionVisitor {
		mir::FunctionDef &mir_function;
		const Map<hir::LaFunction *, mir::FunctionDef *> &func_map;
		Map<hir::Variable *, mir::LocalVar *> &var_map;
		Map<std::string, mir::BasicBlock *> block_map;

		// null if the previous BasicBlock already has a terminator or there are no BasicBlocks yet
		mir::BasicBlock *active_basic_block_nullable;

		public:

		InstructionAdder(mir::FunctionDef &mir_function, const Map<hir::LaFunction *, mir::FunctionDef *> &func_map, Map<hir::Variable *, mir::LocalVar *> &var_map) :
			mir_function { mir_function },
			func_map { func_map },
			var_map { var_map },
			block_map {},
			active_basic_block_nullable { nullptr }
		{}

		void visit(hir::InstructionDeclaration &inst) override {
			// do nothing; variables were already added
		}
		void visit(hir::InstructionAssignment &inst) override {
			this->ensure_active_basic_block();
			// TODO add decoding and shit
		}
		void visit(hir::InstructionLabel &inst) override {
			// must start a new basic block
			mir::BasicBlock *old_block = this->active_basic_block_nullable;
			this->enter_basic_block(inst.label_name);

			if (old_block) {
				// the old block falls through
				assert(std::holds_alternative<mir::BasicBlock::ReturnVoid>(old_block->terminator));
				old_block->terminator = mir::BasicBlock::Goto { this->active_basic_block_nullable };
			}
		}
		void visit(hir::InstructionReturn &inst) override {
			this->ensure_active_basic_block();
			mir::BasicBlock::Terminator terminator;
			if (inst.return_value.has_value()) {
				terminator = mir::BasicBlock::ReturnVal {
					convert_expr_to_operand(*inst.return_value, this->func_map, this->var_map)
				};
			} else {
				terminator = mir::BasicBlock::ReturnVoid {};
			}
			this->active_basic_block_nullable->terminator = mv(terminator);
			this->active_basic_block_nullable = nullptr;
		}
		void visit(hir::InstructionBranchUnconditional &inst) override {
			this->ensure_active_basic_block();
			this->active_basic_block_nullable->terminator = mir::BasicBlock::Goto {
				this->get_basic_block_by_name(inst.label_name)
			};
			this->active_basic_block_nullable = nullptr;
		}
		void visit(hir::InstructionBranchConditional &inst) override {
			this->ensure_active_basic_block();
			this->active_basic_block_nullable->terminator = mir::BasicBlock::Branch {
				convert_expr_to_operand(inst.condition, this->func_map, this->var_map),
				this->get_basic_block_by_name(inst.then_label_name),
				this->get_basic_block_by_name(inst.else_label_name)
			};
			this->active_basic_block_nullable = nullptr;
		}

		private:
		// empty label name if anonymous block
		// sets the new basic block to be the current basic block
		void enter_basic_block(std::string_view label_name) {
			if (label_name.size() > 0) {
				this->active_basic_block_nullable = this->get_basic_block_by_name(label_name);
			} else {
				this->active_basic_block_nullable = this->create_basic_block("");
			}
		}
		// makes sure that there is an active basic_block
		// should be called right before adding an instruction
		void ensure_active_basic_block() {
			if (!this->active_basic_block_nullable) {
				this->enter_basic_block("");
			}
		}
		// will create a basic block if it doesn't already exist
		mir::BasicBlock *get_basic_block_by_name(std::string_view label_name) {
			assert(label_name.length() > 0);
			auto it = this->block_map.find(label_name);
			if (it == this->block_map.end()) {
				return this->create_basic_block(label_name);
			} else {
				return it->second;
			}
		}
		// empty label name if anonymous
		mir::BasicBlock *create_basic_block(std::string_view label_name) {
			Uptr<mir::BasicBlock> block = mkuptr<mir::BasicBlock>(std::string(label_name));
			mir::BasicBlock *block_ptr = block.get();
			this->mir_function.basic_blocks.push_back(mv(block));
			if (label_name.size() > 0) {
				// add the basic block to the mapping for label names
				auto [_, entry_is_new] = this->block_map.insert_or_assign(std::string(label_name), block_ptr);
				if (!entry_is_new) {
					std::cerr << "Logic error: creating basic block that already exists.\n";
					exit(1);
				}
			}
			return block_ptr;
		}
	};

	// fills in the given mir::FunctionDef with the information in the given
	// hir::FunctionDef
	void fill_mir_function(
		mir::FunctionDef &mir_function,
		const hir::LaFunction &hir_function,
		const Map<hir::LaFunction *, mir::FunctionDef *> &func_map
	) {
		Map<hir::Variable *, mir::LocalVar *> var_map;

		// transfer the user-declared local variables and parameters
		for (const Uptr<hir::Variable> &hir_var : hir_function.vars) {
			Uptr<mir::LocalVar> mir_var = mkuptr<mir::LocalVar>(
				hir_var->name,
				hir_var->type
			);
			var_map.insert_or_assign(hir_var.get(), mir_var.get());
			mir_function.local_vars.push_back(mv(mir_var));
		}
		for (hir::Variable *parameter_var : hir_function.parameter_vars) {
			mir_function.parameter_vars.push_back(var_map.at(parameter_var));
		}

		// transfer over each instruction into the basic blocks
		InstructionAdder inst_adder(mir_function, func_map, var_map);
		for (const Uptr<hir::Instruction> &hir_inst : hir_function.instructions) {
			hir_inst->accept(inst_adder);
		}
	}

	Uptr<mir::Program> make_mir_program(const hir::Program &hir_program) {
		auto mir_program = mkuptr<mir::Program>();

		// make two passes through the HIR: first, create all the function
		// definitions and track how the hir functions are being mapped to
		// mir::FunctionDefs. second, fill in the function definition using
		// the HIR.
		Map<hir::LaFunction *, mir::FunctionDef *> func_map;
		for (const Uptr<hir::LaFunction> &hir_function : hir_program.la_functions) {
			auto mir_function = mkuptr<mir::FunctionDef>(hir_function->name, hir_function->return_type);
			func_map.insert_or_assign(hir_function.get(), mir_function.get());
			mir_program->function_defs.push_back(mv(mir_function));
		}
		for (const auto [hir_function, mir_function] : func_map) {
			fill_mir_function(*mir_function, *hir_function, func_map);
		}

		return mir_program;
	}
};