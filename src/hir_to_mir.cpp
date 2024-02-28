#include "hir_to_mir.h"
#include "std_alias.h"
#include <assert.h>

namespace La::hir_to_mir {
	using namespace std_alias;

	class InstructionAdder : public hir::InstructionVisitor {
		mir::FunctionDef &mir_function;
		const Map<hir::ExternalFunction *, mir::ExternalFunction *> &ext_func_map;
		const Map<hir::LaFunction *, mir::FunctionDef *> &func_map;
		Map<hir::Variable *, mir::LocalVar *> &var_map;
		Map<std::string, mir::BasicBlock *> block_map;

		// null if the previous BasicBlock already has a terminator or there are no BasicBlocks yet
		mir::BasicBlock *active_basic_block_nullable;

		public:

		InstructionAdder(
			mir::FunctionDef &mir_function,
			const Map<hir::ExternalFunction *, mir::ExternalFunction *> &ext_func_map,
			const Map<hir::LaFunction *, mir::FunctionDef *> &func_map,
			Map<hir::Variable *, mir::LocalVar *> &var_map
		) :
			mir_function { mir_function },
			ext_func_map { ext_func_map },
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
			if (inst.maybe_dest.has_value()) {
				this->evaluate_expr_into_existing_place(
					inst.source,
					this->evaluate_indexing_expr(*inst.maybe_dest.value())
				);
			} else {
				this->evaluate_expr_into_existing_place(
					inst.source,
					{}
				);
			}
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
					this->evaluate_expr(*inst.return_value)
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
				this->evaluate_expr(inst.condition),
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

		// stores in the given place the result of the hir::Expr, adding
		// mir::Instructions (and possibly temporaries) to the active basic
		// block if necessary in order to evaluate the given expression
		// (including its side effects)
		// see also evaluate_expr
		void evaluate_expr_into_existing_place(const Uptr<hir::Expr> &expr, Opt<Uptr<mir::Place>> place) {
			Vec<Uptr<mir::Instruction>> &instructions = this->active_basic_block_nullable->instructions;
			if (const hir::BinaryOperation *bin_op = dynamic_cast<hir::BinaryOperation *>(expr.get())) {
				instructions.push_back(mkuptr<mir::Instruction>(
					mv(place),
					mkuptr<mir::BinaryOperation>(
						this->evaluate_expr(bin_op->lhs),
						this->evaluate_expr(bin_op->rhs),
						bin_op->op
					)
				));
			} else if (const hir::LengthGetter *length_getter = dynamic_cast<hir::LengthGetter *>(expr.get())) {
				Opt<Uptr<mir::Operand>> dimension;
				if (length_getter->dimension.has_value()) {
					dimension = evaluate_expr(length_getter->dimension.value());
				}
				instructions.push_back(mkuptr<mir::Instruction>(
					mv(place),
					mkuptr<mir::LengthGetter>(
						this->evaluate_expr(length_getter->target),
						mv(dimension)
					)
				));
			} else if (const hir::FunctionCall *call = dynamic_cast<hir::FunctionCall *>(expr.get())) {
				Vec<Uptr<mir::Operand>> arguments;
				for (const Uptr<hir::Expr> &hir_arg : call->arguments) {
					arguments.push_back(this->evaluate_expr(hir_arg));
				}
				instructions.push_back(mkuptr<mir::Instruction>(
					mv(place),
					mkuptr<mir::FunctionCall>(
						this->evaluate_expr(call->callee),
						mv(arguments)
					)
				));
			} else { // TODO add more cases
				instructions.push_back(mkuptr<mir::Instruction>(
					mv(place),
					this->evaluate_expr(expr)
				));
			}
		}

		// returns an mir::Operand which refers to the result of the hir::Expr,
		// adding mir::Instructions (and possibly temporaries) to the active
		// basic block if necessary in order to evaluate the given expression
		// (including its side effects)
		// see also evaluate_expr_into_existing_place
		Uptr<mir::Operand> evaluate_expr(const Uptr<hir::Expr> &expr) {
			if (const hir::ItemRef<hir::Nameable> *item_ref = dynamic_cast<hir::ItemRef<hir::Nameable> *>(expr.get())) {
				if (!item_ref->get_referent().has_value()) {
					std::cerr << "Compiler error: unbound name `" + item_ref->get_ref_name() + "`\n";
					exit(1);
				}
				hir::Nameable *referent = item_ref->get_referent().value();
				if (hir::Variable *hir_var = dynamic_cast<hir::Variable *>(referent)) {
					mir::LocalVar *mir_var = this->var_map.at(hir_var);
					return mkuptr<mir::Place>(
						mir_var,
						Vec<Uptr<mir::Operand>> {}
					);
				} else if (hir::LaFunction *hir_func = dynamic_cast<hir::LaFunction *>(referent)) {
					mir::FunctionDef *mir_func = this->func_map.at(hir_func);
					return mkuptr<mir::CodeConstant>(mir_func);
				} else if (hir::ExternalFunction *hir_func = dynamic_cast<hir::ExternalFunction *>(referent)) {
					mir::ExternalFunction *mir_func = this->ext_func_map.at(hir_func);
					return mkuptr<mir::ExtCodeConstant>(mir_func);
				} else {
					std::cerr << "Logic error: inexhaustive match on subclasses of Nameable\n";
					exit(1);
				}
			} else if (const hir::NumberLiteral *num_lit = dynamic_cast<hir::NumberLiteral *>(expr.get())) {
				return mkuptr<mir::Int64Constant>(num_lit->value);
			} else if (const hir::IndexingExpr *indexing_expr = dynamic_cast<hir::IndexingExpr *>(expr.get())) {
				return evaluate_indexing_expr(*indexing_expr);
			} else {
				// FUTURE: LA doesn't allow expressions this complex, but if it
				// did then this is where we could add logic that:
				// - calls evaluate_expr_into_existing_local_var to evaluate the
				//   more complex expression
				// - creates a new LocalVar to act as a temporary to store the
				//   intermediate results
				std::cerr << "Logic Error: this expression is too complex to be converted to an mir::Operand\n";
				// TODO this should exit
				// exit(1);
				return mkuptr<mir::Int64Constant>(696969);
			}
		}

		Uptr<mir::Place> evaluate_indexing_expr(const hir::IndexingExpr &indexing_expr) {
			// FUTURE even though the HIR allows it, the LA language allows us
			// to safely assume that the target of an indexing expression just
			// refers to a local variable
			const hir::ItemRef<hir::Nameable> &item_ref = dynamic_cast<const hir::ItemRef<hir::Nameable> &>(*indexing_expr.target);
			if (!item_ref.get_referent().has_value()) {
				std::cerr << "Compiler error: unbound name `" + item_ref.get_ref_name() + "`\n";
				exit(1);
			}
			hir::Variable *hir_var = dynamic_cast<hir::Variable *>(item_ref.get_referent().value());
			assert(hir_var != nullptr);
			mir::LocalVar *mir_var = this->var_map.at(hir_var);

			Vec<Uptr<mir::Operand>> mir_indices;
			for (const Uptr<hir::Expr> &hir_index : indexing_expr.indices) {
				mir_indices.push_back(this->evaluate_expr(hir_index));
			}

			return mkuptr<mir::Place>(
				mir_var,
				mv(mir_indices)
			);
		}
	};

	// fills in the given mir::FunctionDef with the information in the given
	// hir::FunctionDef
	void fill_mir_function(
		mir::FunctionDef &mir_function,
		const hir::LaFunction &hir_function,
		const Map<hir::LaFunction *, mir::FunctionDef *> &func_map,
		const Map<hir::ExternalFunction *, mir::ExternalFunction *> &ext_func_map
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
		InstructionAdder inst_adder(mir_function, ext_func_map, func_map, var_map);
		for (const Uptr<hir::Instruction> &hir_inst : hir_function.instructions) {
			hir_inst->accept(inst_adder);
		}
	}

	Uptr<mir::Program> make_mir_program(const hir::Program &hir_program) {
		auto mir_program = mkuptr<mir::Program>();

		Map<hir::ExternalFunction *, mir::ExternalFunction *> ext_func_map;
		for (const Uptr<hir::ExternalFunction> &hir_ext_func : hir_program.external_functions) {
			auto mir_ext_func = mkuptr<mir::ExternalFunction>(hir_ext_func->value); // copy initialization
			ext_func_map.insert_or_assign(hir_ext_func.get(), mir_ext_func.get());
			mir_program->external_functions.push_back(mv(mir_ext_func));
		}

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
			fill_mir_function(*mir_function, *hir_function, func_map, ext_func_map);
		}

		return mir_program;
	}
};