#include "std_alias.h"
#include "parser.h"
#include <typeinfo>
#include <sched.h>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <set>
#include <iterator>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <stdint.h>
#include <assert.h>
#include <fstream>

#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/analyze.hpp>
#include <tao/pegtl/contrib/raw_string.hpp>
#include <tao/pegtl/contrib/parse_tree.hpp>
#include <tao/pegtl/contrib/parse_tree_to_dot.hpp>

namespace pegtl = TAO_PEGTL_NAMESPACE;

namespace La::parser {
	using namespace std_alias;

	namespace rules {
		// for convenience of reading the rules
		using namespace pegtl;

		// for convenience of adding whitespace
		template<typename Result, typename Separator, typename...Rules>
		struct interleaved_impl;
		template<typename... Results, typename Separator, typename Rule0, typename... RulesRest>
		struct interleaved_impl<seq<Results...>, Separator, Rule0, RulesRest...> :
			interleaved_impl<seq<Results..., Rule0, Separator>, Separator, RulesRest...>
		{};
		template<typename... Results, typename Separator, typename Rule0>
		struct interleaved_impl<seq<Results...>, Separator, Rule0> {
			using type = seq<Results..., Rule0>;
		};
		template<typename Separator, typename... Rules>
		using interleaved = typename interleaved_impl<seq<>, Separator, Rules...>::type;

		struct CommentRule :
			disable<
				TAO_PEGTL_STRING("//"),
				until<eolf>
			>
		{};

		struct SpaceRule :
			sor<one<' '>, one<'\t'>>
		{};

		struct SpacesRule :
			star<SpaceRule>
		{};

		template<typename... Rules>
		using spaces_interleaved = interleaved<SpacesRule, Rules...>;

		struct LineSeparatorsRule :
			star<seq<SpacesRule, eol>>
		{};

		struct LineSeparatorsWithCommentsRule :
			star<
				seq<
					SpacesRule,
					sor<eol, CommentRule>
				>
			>
		{};

		struct SpacesOrNewLines :
			star<sor<SpaceRule, eol>>
		{};

		struct NameRule :
			ascii::identifier // the rules for LA names are the same as for C identifiers
		{};

		struct LabelRule :
			seq<one<':'>, NameRule>
		{};

		struct OperatorRule :
			sor<
				TAO_PEGTL_STRING("+"),
				TAO_PEGTL_STRING("-"),
				TAO_PEGTL_STRING("*"),
				TAO_PEGTL_STRING("&"),
				TAO_PEGTL_STRING("<<"),
				TAO_PEGTL_STRING("<="),
				TAO_PEGTL_STRING("<"),
				TAO_PEGTL_STRING(">>"),
				TAO_PEGTL_STRING(">="),
				TAO_PEGTL_STRING(">"),
				TAO_PEGTL_STRING("=")
			>
		{};

		struct NumberRule :
			sor<
				seq<
					opt<sor<one<'-'>, one<'+'>>>,
					range<'1', '9'>,
					star<digit>
				>,
				one<'0'>
			>
		{};

		struct InexplicableTRule :
			sor<
				NameRule,
				NumberRule
			>
		{};

		struct CallArgsRule :
			opt<list<
				InexplicableTRule,
				one<','>,
				SpaceRule
			>>
		{};

		struct Int64TypeRule : TAO_PEGTL_STRING("int64") {};
		struct ArrayTypeIndicator : TAO_PEGTL_STRING("[]") {};
		struct TupleTypeRule : TAO_PEGTL_STRING("tuple") {};
		struct CodeTypeRule : TAO_PEGTL_STRING("code") {};
		struct VoidTypeRule : TAO_PEGTL_STRING("void") {};

		struct TypeRule :
			sor<
				seq<
					Int64TypeRule,
					star<ArrayTypeIndicator>
				>,
				TupleTypeRule,
				CodeTypeRule,
				VoidTypeRule
			>
		{};

		struct NonVoidTypeRule :
			minus<TypeRule, VoidTypeRule>
		{};

		struct IndexingExpressionRule :
			spaces_interleaved<
				NameRule,
				star<
					one<'['>,
					InexplicableTRule,
					one<']'>
				>
			>
		{};

		struct CallingExpressionRule :
			spaces_interleaved<
				NameRule,
				one<'('>,
				CallArgsRule,
				one<')'>
			>
		{};

		struct InstructionDeclarationRule :
			spaces_interleaved<
				NonVoidTypeRule,
				NameRule
			>
		{};

		struct ArrowSymbolRule : TAO_PEGTL_STRING("\x3c-") {};

		struct InstructionOpAssignmentRule :
			spaces_interleaved<
				NameRule,
				ArrowSymbolRule,
				InexplicableTRule,
				OperatorRule,
				InexplicableTRule
			>
		{};

		struct InstructionReadTensorRule :
			spaces_interleaved<
				NameRule,
				ArrowSymbolRule,
				IndexingExpressionRule
			>
		{};

		struct InstructionWriteTensorRule :
			spaces_interleaved<
				IndexingExpressionRule,
				ArrowSymbolRule,
				InexplicableTRule
			>
		{};

		struct InstructionGetLengthRule :
			spaces_interleaved<
				NameRule,
				ArrowSymbolRule,
				TAO_PEGTL_STRING("length"),
				NameRule,
				opt<InexplicableTRule>
			>
		{};

		struct InstructionCallVoidRule :
			spaces_interleaved<
				CallingExpressionRule
			>
		{};

		struct InstructionCallValRule :
			spaces_interleaved<
				NameRule,
				ArrowSymbolRule,
				CallingExpressionRule
			>
		{};

		struct InstructionNewArrayRule :
			spaces_interleaved<
				NameRule,
				ArrowSymbolRule,
				TAO_PEGTL_STRING("new"),
				TAO_PEGTL_STRING("Array"),
				one<'('>,
				CallArgsRule,
				one<')'>
			>
		{};

		struct InstructionNewTupleRule :
			spaces_interleaved<
				NameRule,
				ArrowSymbolRule,
				TAO_PEGTL_STRING("new"),
				TAO_PEGTL_STRING("Tuple"),
				one<'('>,
				CallArgsRule,
				one<')'>
			>
		{};

		struct InstructionLabelRule :
			spaces_interleaved<
				LabelRule
			>
		{};

		struct InstructionBranchUncondRule :
			spaces_interleaved<
				TAO_PEGTL_STRING("br"),
				LabelRule
			>
		{};

		struct InstructionBranchCondRule :
			spaces_interleaved<
				TAO_PEGTL_STRING("br"),
				InexplicableTRule,
				LabelRule,
				LabelRule
			>
		{};

		struct InstructionReturnRule :
			spaces_interleaved<
				TAO_PEGTL_STRING("return"),
				opt<InexplicableTRule>
			>
		{};

		struct InstructionRule :
			sor<
				InstructionDeclarationRule,
				InstructionGetLengthRule,
				InstructionNewArrayRule,
				InstructionNewTupleRule,
				InstructionCallVoidRule,
				InstructionCallValRule,
				InstructionOpAssignmentRule,
				InstructionReadTensorRule,
				InstructionWriteTensorRule,
				InstructionLabelRule,
				InstructionBranchUncondRule,
				InstructionBranchCondRule,
				InstructionReturnRule
			>
		{};

		struct InstructionsRule :
			opt<list<
				seq<bol, SpacesRule, InstructionRule>,
				LineSeparatorsWithCommentsRule
			>>
		{};

		struct DefArgRule :
			spaces_interleaved<
				NonVoidTypeRule,
				NameRule
			>
		{};

		struct DefArgsRule :
			opt<list<
				DefArgRule,
				one<','>,
				SpaceRule
			>>
		{};

		struct FunctionDefinitionRule :
			interleaved<
				LineSeparatorsWithCommentsRule,
				interleaved<
					SpacesOrNewLines,
					TypeRule,
					NameRule,
					one<'('>,
					DefArgsRule,
					one<')'>,
					one<'{'>
				>,
				InstructionsRule,
				seq<SpacesRule, one<'}'>>
			>
		{};

		struct ProgramRule :
			list<
				seq<SpacesRule, FunctionDefinitionRule>,
				LineSeparatorsWithCommentsRule
			>
		{};

		struct ProgramFile :
			seq<
				bof,
				LineSeparatorsWithCommentsRule,
				ProgramRule,
				LineSeparatorsWithCommentsRule,
				eof
			>
		{};

		template<typename Rule>
		struct Selector : pegtl::parse_tree::selector<
			Rule,
			pegtl::parse_tree::store_content::on<
				NameRule,
				OperatorRule,
				NumberRule
			>,
			pegtl::parse_tree::remove_content::on<
				LabelRule,
				CallArgsRule,
				ArrayTypeIndicator,
				TypeRule,
				VoidTypeRule,
				Int64TypeRule,
				ArrayTypeIndicator,
				TupleTypeRule,
				CodeTypeRule,
				IndexingExpressionRule,
				CallingExpressionRule,
				DefArgRule,
				DefArgsRule,
				InstructionDeclarationRule,
				InstructionOpAssignmentRule,
				InstructionReadTensorRule,
				InstructionWriteTensorRule,
				InstructionGetLengthRule,
				InstructionCallVoidRule,
				InstructionCallValRule,
				InstructionNewArrayRule,
				InstructionNewTupleRule,
				InstructionLabelRule,
				InstructionBranchUncondRule,
				InstructionBranchCondRule,
				InstructionReturnRule,
				InstructionsRule,
				FunctionDefinitionRule,
				ProgramRule
			>
		> {};
	}

	struct ParseNode {
		// members
		Vec<Uptr<ParseNode>> children;
		pegtl::internal::inputerator begin;
		pegtl::internal::inputerator end;
		Opt<pegtl::position> position;
		const std::type_info *rule; // which rule this node matched on
		std::string_view type;// only used for displaying parse tree

		// special methods
		ParseNode() = default;
		ParseNode(const ParseNode &) = delete;
		ParseNode(ParseNode &&) = delete;
		ParseNode &operator=(const ParseNode &) = delete;
		ParseNode &operator=(ParseNode &&) = delete;
		~ParseNode() = default;

		// methods used for parsing

		template<typename Rule, typename ParseInput, typename... States>
		void start(const ParseInput &in, States &&...) {
			this->begin = in.inputerator();
		}

		template<typename Rule, typename ParseInput, typename... States>
		void success(const ParseInput &in, States &&...) {
			this->end = in.inputerator();
			this->position = in.position();
			this->rule = &typeid(Rule);
			this->type = pegtl::demangle<Rule>();
			this->type.remove_prefix(this->type.find_last_of(':') + 1);
		}

		template<typename Rule, typename ParseInput, typename... States>
		void failure(const ParseInput &in, States &&...) {}

		template<typename... States>
		void emplace_back(Uptr<ParseNode> &&child, States &&...) {
			children.emplace_back(mv(child));
		}

		std::string_view string_view() const {
			return {
				this->begin.data,
				static_cast<std::size_t>(this->end.data - this->begin.data)
			};
		}

		const ParseNode &operator[](int index) const {
			return *this->children.at(index);
		}

		// methods used to display the parse tree

		bool has_content() const noexcept {
			return this->end.data != nullptr;
		}

		template<typename... States>
		void remove_content(States &&... /*unused*/) {
			this->end = TAO_PEGTL_NAMESPACE::internal::inputerator();
		}

		bool is_root() const noexcept {
			return static_cast<bool>(this->rule);
		}
	};


	namespace node_processor {
		using namespace La::program;

		// TODO

		std::string_view extract_name(const ParseNode &n) {
			assert(*n.rule == typeid(rules::NameRule));
			return n.string_view();
		}

		Type make_type(const ParseNode &n) {
			assert(*n.rule == typeid(rules::TypeRule));
			const std::type_info &type_rule = *n[0].rule;
			if (type_rule == typeid(rules::VoidTypeRule)) {
				return { Type::VoidType {} };
			} else if (type_rule == typeid(rules::Int64TypeRule)) {
				// the rest of the children must be ArrayTypeIndicators
				return { Type::ArrayType { static_cast<int>(n.children.size() - 1) } };
			} else if (type_rule == typeid(rules::TupleTypeRule)) {
				return { Type::TupleType {} };
			} else if (type_rule == typeid(rules::CodeTypeRule)) {
				return { Type::CodeType {} };
			} else {
				std::cerr << "Logic error: inexhaustive over TypeRule node's children\n";
				exit(1);
			}
		}

		Uptr<LaFunction> make_la_function(const ParseNode &n) {
			assert(*n.rule == typeid(rules::FunctionDefinitionRule));

			Type return_type = make_type(n[0]);
			std::string_view name = extract_name(n[1]);
			Uptr<LaFunction> function = mkuptr<LaFunction>(std::string(name), return_type);

			return function;
		}

		Uptr<Program> make_program(const ParseNode &n) {
			assert(*n.rule == typeid(rules::ProgramRule));
			Uptr<Program> program = mkuptr<Program>();
			for (const Uptr<ParseNode> &child : n.children) {
				program->add_la_function(make_la_function(*child));
			}
			return program;
		}
	}

	Uptr<La::program::Program> parse_file(char *fileName, Opt<std::string> parse_tree_output) {
		using EntryPointRule = pegtl::must<rules::ProgramFile>;

		// Check the grammar for some possible issues.
		// TODO move this to a separate file bc it's performance-intensive
		if (pegtl::analyze<EntryPointRule>() != 0) {
			std::cerr << "There are problems with the grammar" << std::endl;
			exit(1);
		}

		// Parse
		pegtl::file_input<> fileInput(fileName);
		auto root = pegtl::parse_tree::parse<EntryPointRule, ParseNode, rules::Selector>(fileInput);
		if (!root) {
			std::cerr << "ERROR: Parser failed" << std::endl;
			exit(1);
		}
		if (parse_tree_output) {
			std::ofstream output_fstream(*parse_tree_output);
			if (output_fstream.is_open()) {
				pegtl::parse_tree::print_dot(output_fstream, *root);
				output_fstream.close();
			}
		}

		Uptr<La::program::Program> ptr = node_processor::make_program((*root)[0]);
		return ptr;
	}
}