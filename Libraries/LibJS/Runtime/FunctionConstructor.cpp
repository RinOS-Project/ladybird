/*
 * Copyright (c) 2020-2023, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Lexer.h>
#include <LibJS/Parser.h>
#include <LibJS/Runtime/AbstractOperations.h>
#include <LibJS/Runtime/ECMAScriptFunctionObject.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/FunctionConstructor.h>
#include <LibJS/Runtime/FunctionObject.h>
#include <LibJS/Runtime/GeneratorPrototype.h>
#include <LibJS/Runtime/GlobalEnvironment.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/Realm.h>

namespace JS {

GC_DEFINE_ALLOCATOR(FunctionConstructor);

FunctionConstructor::FunctionConstructor(Realm& realm)
    : NativeFunction(realm.vm().names.Function.as_string(), realm.intrinsics().function_prototype())
{
}

void FunctionConstructor::initialize(Realm& realm)
{
    auto& vm = this->vm();
    Base::initialize(realm);

    // 20.2.2.2 Function.prototype, https://tc39.es/ecma262/#sec-function.prototype
    define_direct_property(vm.names.prototype, realm.intrinsics().function_prototype(), 0);

    define_direct_property(vm.names.length, Value(1), Attribute::Configurable);
}

// 20.2.1.1.1 CreateDynamicFunction ( constructor, newTarget, kind, parameterArgs, bodyArg ), https://tc39.es/ecma262/#sec-createdynamicfunction
ThrowCompletionOr<GC::Ref<ECMAScriptFunctionObject>> FunctionConstructor::create_dynamic_function(VM& vm, FunctionObject& constructor, FunctionObject* new_target, FunctionKind kind, ReadonlySpan<Value> parameter_args, Value body_arg)
{
    // 1. If newTarget is undefined, set newTarget to constructor.
    if (new_target == nullptr)
        new_target = &constructor;

    StringView prefix;
    GC::Ref<Object> (Intrinsics::*fallback_prototype)() = nullptr;

    switch (kind) {
    // 2. If kind is normal, then
    case FunctionKind::Normal:
        // a. Let prefix be "function".
        prefix = "function"sv;

        // b. Let exprSym be the grammar symbol FunctionExpression.
        // c. Let bodySym be the grammar symbol FunctionBody[~Yield, ~Await].
        // d. Let parameterSym be the grammar symbol FormalParameters[~Yield, ~Await].

        // e. Let fallbackProto be "%Function.prototype%".
        fallback_prototype = &Intrinsics::function_prototype;
        break;

    // 3. Else if kind is generator, then
    case FunctionKind::Generator:
        // a. Let prefix be "function*".
        prefix = "function*"sv;

        // b. Let exprSym be the grammar symbol GeneratorExpression.
        // c. Let bodySym be the grammar symbol GeneratorBody.
        // d. Let parameterSym be the grammar symbol FormalParameters[+Yield, ~Await].

        // e. Let fallbackProto be "%GeneratorFunction.prototype%".
        fallback_prototype = &Intrinsics::generator_function_prototype;
        break;

    // 4. Else if kind is async, then
    case FunctionKind::Async:
        // a. Let prefix be "async function".
        prefix = "async function"sv;

        // b. Let exprSym be the grammar symbol AsyncFunctionExpression.
        // c. Let bodySym be the grammar symbol AsyncFunctionBody.
        // d. Let parameterSym be the grammar symbol FormalParameters[~Yield, +Await].

        // e. Let fallbackProto be "%AsyncFunction.prototype%".
        fallback_prototype = &Intrinsics::async_function_prototype;
        break;

    // 5. Else,
    case FunctionKind::AsyncGenerator:
        // a. Assert: kind is async-generator.

        // b. Let prefix be "async function*".
        prefix = "async function*"sv;

        // c. Let exprSym be the grammar symbol AsyncGeneratorExpression.
        // d. Let bodySym be the grammar symbol AsyncGeneratorBody.
        // e. Let parameterSym be the grammar symbol FormalParameters[+Yield, +Await].

        // f. Let fallbackProto be "%AsyncGeneratorFunction.prototype%".
        fallback_prototype = &Intrinsics::async_generator_function_prototype;
        break;

    default:
        VERIFY_NOT_REACHED();
    }

    auto function_data = TRY(compile_dynamic_function(vm, kind, parameter_args, body_arg));
    auto& realm = *vm.current_realm();

    // 25. Let proto be ? GetPrototypeFromConstructor(newTarget, fallbackProto).
    auto* prototype = TRY(get_prototype_from_constructor(vm, *new_target, fallback_prototype));

    auto function = ECMAScriptFunctionObject::create_from_function_data(
        *vm.current_realm(),
        function_data,
        &vm.current_realm()->global_environment(),
        nullptr,
        *prototype);

    // FIXME: Remove the name argument from create() and do this instead.
    // 29. Perform SetFunctionName(F, "anonymous").

    // 30. If kind is generator, then
    if (kind == FunctionKind::Generator) {
        // a. Let prototype be OrdinaryObjectCreate(%GeneratorFunction.prototype.prototype%).
        prototype = Object::create_prototype(realm, realm.intrinsics().generator_function_prototype_prototype());

        // b. Perform ! DefinePropertyOrThrow(F, "prototype", PropertyDescriptor { [[Value]]: prototype, [[Writable]]: true, [[Enumerable]]: false, [[Configurable]]: false }).
        function->define_direct_property(vm.names.prototype, prototype, Attribute::Writable);
    }
    // 31. Else if kind is asyncGenerator, then
    else if (kind == FunctionKind::AsyncGenerator) {
        // a. Let prototype be OrdinaryObjectCreate(%AsyncGeneratorFunction.prototype.prototype%).
        prototype = Object::create_prototype(realm, realm.intrinsics().async_generator_function_prototype_prototype());

        // b. Perform ! DefinePropertyOrThrow(F, "prototype", PropertyDescriptor { [[Value]]: prototype, [[Writable]]: true, [[Enumerable]]: false, [[Configurable]]: false }).
        function->define_direct_property(vm.names.prototype, prototype, Attribute::Writable);
    }
    // 32. Else if kind is normal, perform MakeConstructor(F).
    else if (kind == FunctionKind::Normal) {
        // FIXME: Implement MakeConstructor
        prototype = Object::create_prototype(realm, realm.intrinsics().object_prototype());
        prototype->define_direct_property(vm.names.constructor, function, Attribute::Writable | Attribute::Configurable);
        function->define_direct_property(vm.names.prototype, prototype, Attribute::Writable);
    }

    // 33. NOTE: Functions whose kind is async are not constructible and do not have a [[Construct]] internal method or a "prototype" property.

    // 34. Return F.
    return function;
}

ThrowCompletionOr<GC::Ref<SharedFunctionInstanceData>> FunctionConstructor::compile_dynamic_function(VM& vm, FunctionKind kind, ReadonlySpan<Value> parameter_args, Value body_arg)
{
    StringView prefix;

    switch (kind) {
    case FunctionKind::Normal:
        prefix = "function"sv;
        break;
    case FunctionKind::Generator:
        prefix = "function*"sv;
        break;
    case FunctionKind::Async:
        prefix = "async function"sv;
        break;
    case FunctionKind::AsyncGenerator:
        prefix = "async function*"sv;
        break;
    default:
        VERIFY_NOT_REACHED();
    }

    auto arg_count = parameter_args.size();

    Vector<String> parameter_strings;
    parameter_strings.ensure_capacity(arg_count);
    for (auto const& parameter_value : parameter_args)
        parameter_strings.unchecked_append(TRY(parameter_value.to_string(vm)));

    auto body_string = TRY(body_arg.to_string(vm));
    auto& realm = *vm.current_realm();

    String parameters_string;
    if (arg_count > 0)
        parameters_string = MUST(String::join(',', parameter_strings));

    auto body_parse_string = ByteString::formatted("\n{}\n", body_string);
    auto source_text = ByteString::formatted("{} anonymous({}\n) {{{}}}", prefix, parameters_string, body_parse_string);

    TRY(vm.host_ensure_can_compile_strings(realm, parameter_strings, body_string, source_text, CompilationType::Function, parameter_args, body_arg));

    u8 parse_options = FunctionNodeParseOptions::CheckForFunctionAndName;
    if (kind == FunctionKind::Async || kind == FunctionKind::AsyncGenerator)
        parse_options |= FunctionNodeParseOptions::IsAsyncFunction;
    if (kind == FunctionKind::Generator || kind == FunctionKind::AsyncGenerator)
        parse_options |= FunctionNodeParseOptions::IsGeneratorFunction;

    i32 function_length = 0;
    auto parameters_parser = Parser(Lexer(SourceCode::create({}, Utf16String::from_utf8(parameters_string))));
    auto parameters = parameters_parser.parse_formal_parameters(function_length, parse_options);
    if (parameters_parser.has_errors()) {
        auto error = parameters_parser.errors()[0];
        return vm.throw_completion<SyntaxError>(error.to_string());
    }

    FunctionParsingInsights parsing_insights;
    auto body_parser = Parser::parse_function_body_from_string(body_parse_string, parse_options, parameters, kind, parsing_insights);
    if (body_parser.has_errors()) {
        auto error = body_parser.errors()[0];
        return vm.throw_completion<SyntaxError>(error.to_string());
    }

    auto source_parser = Parser(Lexer(SourceCode::create({}, Utf16String::from_utf8(source_text))));
    auto expr = source_parser.parse_function_node<FunctionExpression>();
    source_parser.run_scope_analysis();
    if (source_parser.has_errors()) {
        auto error = source_parser.errors()[0];
        return vm.throw_completion<SyntaxError>(error.to_string());
    }

    parsing_insights.might_need_arguments_object = true;

    auto function_data = vm.heap().allocate<SharedFunctionInstanceData>(
        vm,
        expr->kind(),
        "anonymous"_utf16_fly_string,
        expr->function_length(),
        expr->parameters(),
        expr->body(),
        Utf16View {},
        expr->is_strict_mode(),
        false,
        parsing_insights,
        expr->local_variables_names());
    function_data->m_source_text_owner = Utf16String::from_utf8(source_text);
    function_data->m_source_text = function_data->m_source_text_owner.utf16_view();

    return function_data;
}

// 20.2.1.1 Function ( p1, p2, … , pn, body ), https://tc39.es/ecma262/#sec-function-p1-p2-pn-body
ThrowCompletionOr<Value> FunctionConstructor::call()
{
    return TRY(construct(*this));
}

// 20.2.1.1 Function ( ...parameterArgs, bodyArg ), https://tc39.es/ecma262/#sec-function-p1-p2-pn-body
ThrowCompletionOr<GC::Ref<Object>> FunctionConstructor::construct(FunctionObject& new_target)
{
    auto& vm = this->vm();

    ReadonlySpan<Value> arguments = vm.running_execution_context().arguments;

    ReadonlySpan<Value> parameter_args = arguments;
    if (!parameter_args.is_empty())
        parameter_args = parameter_args.slice(0, parameter_args.size() - 1);

    // 1. Let C be the active function object.
    auto* constructor = vm.active_function_object();

    // 2. If bodyArg is not present, set bodyArg to the empty String.
    Value body_arg = &vm.empty_string();
    if (!arguments.is_empty())
        body_arg = arguments.last();

    // 3. Return ? CreateDynamicFunction(C, NewTarget, normal, parameterArgs, bodyArg).
    return TRY(create_dynamic_function(vm, *constructor, &new_target, FunctionKind::Normal, parameter_args, body_arg));
}

}
