#include "daScript/misc/platform.h"

#include "daScript/ast/ast.h"
#include "daScript/ast/ast_generate.h"
#include "daScript/ast/ast_expressions.h"
#include "daScript/ast/ast_visitor.h"

namespace das {

    das::unordered_set<string> g_cpp_keywords = {
        /* reserved C++ names*/
        "alignas","alignof","and","and_eq","asm","atomic_cancel","atomic_commit","atomic_noexcept","auto"
        ,"bitand","bitor","bool","break","case","catch","char","char8_t","char16_t","char32_t","class"
        ,"compl","concept","const","consteval","constexpr","constinit","const_cast","continue","co_await"
        ,"co_return","co_yield","decltype","default","delete","do","double","dynamic_cast","else","enum"
        ,"explicit","export","extern","false","float","for","friend","goto","if","inline","int","long"
        ,"mutable","namespace","new","noexcept","not","not_eq","nullptr","operator","or","or_eq","private"
        ,"protected","public","reflexpr","register","reinterpret_cast","requires","return","short","signed"
        ,"sizeof","static","static_assert","static_cast","struct","switch","synchronized","template","this"
        ,"thread_local","throw","true","try","typedef","typeid","typename","union","unsigned","using"
        ,"virtual","void","volatile","wchar_t","while","xor","xor_eq"
        /* extra */    
        ,"override","final","import","module","transaction_safe","transaction_safe_dynamic"
    };

    bool isCppKeyword(const string & str) {
        return g_cpp_keywords.find(str) != g_cpp_keywords.end();
    }

    bool exprReturns ( const ExpressionPtr & expr ) {
        if ( expr->rtti_isReturn() ) {
            return true;
        } else if ( expr->rtti_isBlock() ) {
            auto block = static_pointer_cast<ExprBlock>(expr);
            for ( auto & be : block->list ) {
                if ( be->rtti_isBreak() ) {
                    break;
                }
                if ( exprReturns(be) ) {
                    return true;
                }
            }
        } else if ( expr->rtti_isIfThenElse() ) {
            auto ite = static_pointer_cast<ExprIfThenElse>(expr);
            if ( ite->if_false ) {
                return exprReturns(ite->if_true) && exprReturns(ite->if_false);
            }
        } else if ( expr->rtti_isWith() ) {
            auto wth = static_pointer_cast<ExprWith>(expr);
            return exprReturns(wth->body);
        } else if ( expr->rtti_isWhile() ) {
            auto wh = static_pointer_cast<ExprWhile>(expr);
            return exprReturns(wh->body);
        } else if ( expr->rtti_isFor() ) {
            auto fr = static_pointer_cast<ExprFor>(expr);
            return exprReturns(fr->body);
        }
        return false;
    }

    class LintVisitor : public Visitor {
        bool checkOnlyFastAot;
        bool checkAotSideEffects;
        bool checkNoGlobalHeap;
        bool checkNoGlobalVariables;
        bool checkUnusedArgument;
        bool checkUnsafe;
    public:
        LintVisitor ( const ProgramPtr & prog ) : program(prog) {
            checkOnlyFastAot = program->options.getBoolOption("only_fast_aot", program->policies.only_fast_aot);
            checkAotSideEffects = program->options.getBoolOption("aot_order_side_effects", program->policies.aot_order_side_effects);
            checkNoGlobalHeap = program->options.getBoolOption("no_global_heap", program->policies.no_global_heap);
            checkNoGlobalVariables = program->options.getBoolOption("no_global_variables", program->policies.no_global_variables);
            checkUnusedArgument = program->options.getBoolOption("no_unused_function_arguments", program->policies.no_unused_function_arguments);
            checkUnsafe = program->policies.no_unsafe;
        }
        void error ( const string & err, const LineInfo & at, CompilationError cerr = CompilationError::unspecified ) const {
            program->error(err,at,cerr);
        }
    protected:
        void verifyOnlyFastAot ( Function * func, const LineInfo & at ) {
            if ( !checkOnlyFastAot ) return;
            if ( func && func->builtIn ) {
                auto bif = (BuiltInFunction *) func;
                if ( bif->cppName.empty() ) {
                    program->error(func->describe() + " has no cppName while onlyFastAot option is set", at,
                                   CompilationError::only_fast_aot_no_cpp_name );
                }
            }
        }
        bool isValidEnumName(const string & str) const {
            return !isCppKeyword(str);
        }
        bool isValidEnumValueName(const string & str) const {
            return !isCppKeyword(str);
        }
        void lintType ( TypeDecl * td ) {
            for ( auto & name : td->argNames ) {
                if (!isValidVarName(name)) {
                    program->error("invalid type argument name " + name,
                        td->at, CompilationError::invalid_name );
                }
            }
            if ( td->firstType ) lintType(td->firstType.get());
            if ( td->secondType ) lintType(td->secondType.get());
            for ( auto & arg : td->argTypes ) lintType(arg.get());
        }
        virtual void preVisit ( TypeDecl * td ) override {
            Visitor::preVisit(td);
            lintType(td);
        }
        virtual void preVisit ( Enumeration * enu ) override {
            Visitor::preVisit(enu);
            if (!isValidEnumName(enu->name)) {
                program->error("invalid enumeration name " + enu->name,
                    enu->at, CompilationError::invalid_name );
            }
        }
        virtual void preVisitEnumerationValue ( Enumeration * enu, const string & name, Expression * value, bool last ) override { 
            Visitor::preVisitEnumerationValue(enu,name,value,last);
            if (!isValidEnumValueName(name)) {
                program->error("invalid enumeration value name " + name,
                    enu->at, CompilationError::invalid_name );
            }
        }
        bool isValidStructureName(const string & str) const {
            return !isCppKeyword(str);
        }
        virtual void preVisit ( Structure * var ) override { 
            Visitor::preVisit(var);
            if (!isValidStructureName(var->name)) {
                program->error("invalid structure name " + var->name,
                    var->at, CompilationError::invalid_name );
            }
        }
        bool isValidVarName(const string & str) const {
            return !isCppKeyword(str);
        }
        virtual void preVisitStructureField ( Structure * var, Structure::FieldDeclaration & decl, bool last ) override {
            Visitor::preVisitStructureField(var, decl, last);
            if (!isValidVarName(decl.name)) {
                program->error("invalid structure field name " + decl.name,
                    var->at, CompilationError::invalid_name );
            }
        }
        virtual void preVisitGlobalLet ( const VariablePtr & var ) override {
            Visitor::preVisitGlobalLet(var);
            if (!isValidVarName(var->name)) {
                program->error("invalid variable name " + var->name,
                    var->at, CompilationError::invalid_name );
            }
        }
        virtual void preVisit(ExprLet * expr) override {
            Visitor::preVisit(expr);
            for (const auto & var : expr->variables) {
                if (!isValidVarName(var->name)) {
                    program->error("invalid variable name " + var->name,
                        var->at, CompilationError::invalid_name );
                }
            }
        }
        virtual ExpressionPtr visitGlobalLetInit ( const VariablePtr & var, Expression * init ) override {
            if ( checkNoGlobalHeap ) {
                if ( !init->type->isNoHeapType() ) {
                    program->error("variable " + var->name + " uses heap, which is disabled via option no_global_heap",
                        var->at, CompilationError::no_global_heap );
                }
            }
            if ( checkNoGlobalVariables ) {
                if ( !var->type->isConst() ) {
                    program->error("variable " + var->name + " is not a constant, which is disabled via option no_global_variables",
                        var->at, CompilationError::no_global_variables );
                }
            }
            return Visitor::visitGlobalLetInit(var, init);
        }
        virtual void preVisit ( ExprCall * expr ) override {
            Visitor::preVisit(expr);
            verifyOnlyFastAot(expr->func, expr->at);
            for ( const auto & annDecl : expr->func->annotations ) {
                auto ann = annDecl->annotation;
                if ( ann->rtti_isFunctionAnnotation() ) {
                    auto fnAnn = static_pointer_cast<FunctionAnnotation>(ann);
                    string err;
                    if ( !fnAnn->verifyCall(expr, annDecl->arguments, err) ) {
                        program->error("call annotated by " + fnAnn->name + " failed, " + err,
                                       expr->at, CompilationError::annotation_failed);
                    }
                }
            }
            if ( checkAotSideEffects ) {
                if ( expr->arguments.size()>1 ) {
                    for ( auto & arg : expr->arguments ) {
                        if ( !arg->noNativeSideEffects ) {
                            program->error("side effects may affect function " + expr->func->name + " evaluation order", expr->at,
                                           CompilationError::aot_side_effects );
                            break;
                        }
                    }
                }
            }
        }
        virtual void preVisit ( ExprOp1 * expr ) override {
            Visitor::preVisit(expr);
            verifyOnlyFastAot(expr->func, expr->at);
        }
        virtual void preVisit ( ExprOp2 * expr ) override {
            Visitor::preVisit(expr);
            verifyOnlyFastAot(expr->func, expr->at);
            if ( checkAotSideEffects ) {
                if ( !expr->left->noNativeSideEffects || !expr->right->noNativeSideEffects ) {
                    program->error("side effects may affect evaluation order", expr->at,
                                   CompilationError::aot_side_effects );
                }
            }
        }
        virtual void preVisit ( ExprOp3 * expr ) override {
            Visitor::preVisit(expr);
            verifyOnlyFastAot(expr->func, expr->at);
            if ( checkAotSideEffects ) {
                if ( !expr->subexpr->noNativeSideEffects || !expr->left->noNativeSideEffects || !expr->right->noNativeSideEffects ) {
                    program->error("side effects may affect evaluation order", expr->at,
                                   CompilationError::aot_side_effects );
                }
            }
        }
        virtual void preVisit ( ExprCopy * expr ) override {
            Visitor::preVisit(expr);
            verifyOnlyFastAot(expr->func, expr->at);
            // @E1 = E2, E2 side effects are before E1 side effects
            /*
            if ( checkAotSideEffects ) {
                if ( !expr->left->noNativeSideEffects || !expr->right->noNativeSideEffects ) {
                    program->error("side effects may affect copy evaluation order", expr->at,
                                   CompilationError::aot_side_effects );
                }
            }
            */
        }
        virtual void preVisit ( ExprMove * expr ) override {
            Visitor::preVisit(expr);
            verifyOnlyFastAot(expr->func, expr->at);
            if ( checkAotSideEffects ) {
                if ( !expr->left->noNativeSideEffects || !expr->right->noNativeSideEffects ) {
                    program->error("side effects may affect move evaluation order", expr->at,
                                   CompilationError::aot_side_effects );
                }
            }
        }
        virtual void preVisit ( ExprClone * expr ) override {
            Visitor::preVisit(expr);
            verifyOnlyFastAot(expr->func, expr->at);
            if ( checkAotSideEffects ) {
                if ( !expr->left->noNativeSideEffects || !expr->right->noNativeSideEffects ) {
                    program->error("side effects may affect clone evaluation order", expr->at,
                                   CompilationError::aot_side_effects );
                }
            }
        }
        virtual void preVisit ( ExprAssert * expr ) override {
            Visitor::preVisit(expr);
            if ( !expr->isVerify && !expr->arguments[0]->noSideEffects ) {
                error("assert expressions can't have side-effects (use verify instead)", expr->at,
                      CompilationError::assert_with_side_effects);
            }
        }
        bool isValidFunctionName(const string & str) const {
            return !isCppKeyword(str);
        }

        virtual void preVisit ( Function * fn ) override {
            Visitor::preVisit(fn);
            if (!isValidFunctionName(fn->name)) {
                program->error("invalid function name " + fn->name,
                    fn->at, CompilationError::invalid_name );
            }
            auto fnMod = fn->fromGeneric ? fn->fromGeneric->module : fn->module;
            if ( fnMod == program->thisModule.get() ) {
                anyUnsafe |= fn->unsafe;
                if ( fn->unsafe && checkUnsafe ) {
                    error("unsafe function " + fn->getMangledName() +
                            "\nunsafe functions are prohibited by CodeOfPolicies", fn->at,
                            CompilationError::unsafe_function);
                }
            }
        }
        virtual void preVisitArgument ( Function * fn, const VariablePtr & var, bool lastArg ) override {
            Visitor::preVisitArgument(fn, var, lastArg);
            if (!isValidVarName(var->name)) {
                program->error("invalid argument variable name " + var->name,
                    var->at, CompilationError::invalid_name );
            }
            if ( checkUnusedArgument ) {
                if ( !var->marked_used && var->isAccessUnused() ) {
                    error("unused function argument " + var->name +
                          "; use [unused_argument(" + var->name + ")] if intentional", var->at,
                            CompilationError::unused_function_argument);
                }
            }
        }
        virtual void preVisitBlockArgument ( ExprBlock * block, const VariablePtr & var, bool lastArg ) override {
            Visitor::preVisitBlockArgument(block, var, lastArg);
            if (!isValidVarName(var->name)) {
                program->error("invalid block argument variable name " + var->name,
                    var->at, CompilationError::invalid_name );
            }
        }
    public:
        ProgramPtr program;
        bool anyUnsafe = false;
    };

    struct Option {
        const char *    name;
        Type            type;
    } g_allOptions [] = {
    // lint
        "lint",                         Type::tBool,
        "only_fast_aot",                Type::tBool,
        "aot_order_side_effects",       Type::tBool,
        "no_global_heap",               Type::tBool,
        "no_global_variables",          Type::tBool,
        "no_unused_function_arguments", Type::tBool,
    // memory
        "heap",                         Type::tInt,
        "string_heap",                  Type::tInt,
        "stack",                        Type::tInt,
        "intern_const_strings",         Type::tBool,
        "intern_strings",               Type::tBool,
    // aot
        "no_aot",                       Type::tBool,
        "aot_prologue",                 Type::tBool,
    // plotting
        "plot",                         Type::tBool,
        "plot_read_write",              Type::tBool,
    // logging
        "log",                          Type::tBool,
        "log_optimization_passes",      Type::tBool,
        "log_stack",                    Type::tBool,
        "log_nodes",                    Type::tBool,
        "log_mem",                      Type::tBool,
        "log_cpp",                      Type::tBool,
        "log_aot",                      Type::tBool,
        "log_infer_passes",             Type::tBool,
        "log_require",                  Type::tBool,
        "log_compile_time",             Type::tBool,
        "log_generics",                 Type::tBool,
        "log_mn_hash",                  Type::tBool,
        "log_ad_hash",                  Type::tBool,
        "print_ref",                    Type::tBool,
        "print_var_access",             Type::tBool,
        "print_c_style",                Type::tBool,
    // rtti
        "rtti",                         Type::tBool,
    // optimization
        "optimize",                     Type::tBool,
        "fusion",                       Type::tBool,
        "remove_unused_symbols",        Type::tBool,
    // language
        "always_export_initializer",    Type::tBool,
        "infer_time_folding",           Type::tBool,
        "disable_run",                  Type::tBool,
        "max_infer_passes",             Type::tInt,
        "indenting",                    Type::tInt
    };

    void verifyOptions() {
        bool failed = false;
        for ( const auto & opt : g_allOptions ) {
            if ( !isValidBuiltinName(opt.name) ) {
                DAS_FATAL_LOG("%s - invalid option. expecting snake_case\n", opt.name);
                failed = true;
            }
        }
        DAS_VERIFYF(!failed, "verifyOptions failed");
    }

    void Program::lint ( ModuleGroup & libGroup ) {
        if (!options.getBoolOption("lint", true)) {
            return;
        }
        TextWriter logs;
        buildAccessFlags(logs);
        checkSideEffects();
        // lint it
        LintVisitor lintV(shared_from_this());
        visit(lintV);
        unsafe = lintV.anyUnsafe;
        // all control paths return something
        for ( auto & fnT : thisModule->functions ) {
            auto fn = fnT.second;
            if ( !fn->result->isVoid() && !fn->result->isAuto() ) {
                if ( !exprReturns(fn->body) ) {
                    error("not all control paths return value", fn->at,
                          CompilationError::not_all_paths_return_value);
                }
            }
        }
        // check for invalid options
        das_map<string,Type> ao;
        for ( const auto & opt : g_allOptions ) {
            ao[opt.name] = opt.type;
        }
        for ( const auto & opt : options ) {
            Type optT = Type::none;
            auto it = ao.find(opt.name);
            if ( it != ao.end() ) {
                optT = it->second;
            } else {
                optT = Module::findOption(opt.name);
            }
            if ( optT!=Type::none && optT!=opt.type ) {
                error("invalid option type for " + opt.name
                      + ", unexpected " + das_to_string(opt.type)
                      + ", expecting " + das_to_string(optT),
                        LineInfo(), CompilationError::invalid_option);
            } else if ( optT==Type::none ){
                error("invalid option " + opt.name, LineInfo(),
                      CompilationError::invalid_option);
            }
        }
        libGroup.foreach([&](Module * mod) -> bool {
            for ( const auto & pm : mod->lintMacros ) {
                this->visit(*pm);
            }
            return true;
        },"*");
    }
}

