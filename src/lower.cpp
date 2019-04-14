#include "lower.h"

#include <fstream>

#include "logger.h"
#include "types.h"

#define assert_not_impl()                                                      \
  do {                                                                         \
    std::cout << llvm_print_module(*llvm_get_module(builder)) << std::endl;    \
    assert(false);                                                             \
  } while (0)

namespace lower {

using lower_env_t = std::map<
    std::string,
    std::map<types::type_t::ref, llvm::Value *, types::compare_type_t>>;

llvm::Value *maybe_get_llvm_value(const lower_env_t &env,
                                  std::string name,
                                  types::type_t::ref type) {
  type = types::unitize(type);
  return get(env, name, type, static_cast<llvm::Value *>(nullptr));
}

llvm::Value *get_llvm_value(const lower_env_t &env,
                            std::string name,
                            types::type_t::ref type) {
  auto llvm_value = maybe_get_llvm_value(env, name, type);
  if (llvm_value == nullptr) {
    auto error = user_error(INTERNAL_LOC(),
                            "we need an llvm definition for %s :: %s",
                            name.c_str(), type->str().c_str());
    for (auto pair : env) {
      for (auto overload : pair.second) {
        error.add_info(INTERNAL_LOC(), "%s :: %s = %s", pair.first.c_str(),
                       overload.first->str().c_str(),
                       llvm_print(overload.second).c_str());
      }
    }
    print_exception(error, 10);
    dbg();
    throw error;
  }
  return llvm_value;
}

llvm::Value *set_llvm_value(lower_env_t &env,
                            std::string name,
                            types::type_t::ref type,
                            llvm::Value *llvm_value,
                            bool allow_shadowing=false) {
  debug_above(5, log("setting env[%s][%s] = %s", name.c_str(),
                     type->str().c_str(), llvm_print(llvm_value).c_str()));
  assert(name.size() != 0);
  auto existing_llvm_value = get(env, name, type,
                                 static_cast<llvm::Value *>(nullptr));
  if (existing_llvm_value == nullptr) {
    env[name][type] = llvm_value;
  } else {
    if (allow_shadowing) {
      env[name][type] = llvm_value;
    } else {
      /* what now? probably shouldn't have happened */
      log_location(
          log_error, INTERNAL_LOC(),
          "uh-oh. %s :: %s is already set to %s when trying to set it to %s",
          name.c_str(), type->str().c_str(),
          llvm_print(existing_llvm_value).c_str(),
          llvm_print(llvm_value).c_str());
      assert(false);
    }
  }
  return llvm_value;
}

llvm::Constant *lower_decl(std::string name,
                           types::type_t::ref type,
                           llvm::IRBuilder<> &builder,
                           llvm::Module *llvm_module,
                           gen::value_t::ref value,
                           lower_env_t &lower_env) {
  // TODO: if this assertion holds, we can remove the type param, and just get
  // it from value
  assert(type_equality(value->type, type));

  debug_above(4, log("lower_decl(%s, ..., %s :: %s, ...)", name.c_str(),
                     value->str().c_str(), value->type->str().c_str()));

  assert(value != nullptr);
  value = gen::resolve_proxy(value);
  assert(value != nullptr);

  llvm::Value *llvm_value = maybe_get_llvm_value(lower_env, name, value->type);
  if (llvm_value != nullptr) {
    llvm::Constant *llvm_constant = llvm::dyn_cast<llvm::Constant>(llvm_value);
    assert(llvm_constant != nullptr);
    return llvm_constant;
  }

  if (auto unit = dyncast<gen::unit_t>(value)) {
    return llvm::cast<llvm::Constant>(set_llvm_value(
        lower_env, name, type,
        llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo())));
  } else if (auto literal = dyncast<gen::literal_t>(value)) {
    assert(false);
    return nullptr;
  } else if (auto phi_node = dyncast<gen::phi_node_t>(value)) {
    assert(false);
    return nullptr;
  } else if (auto cast = dyncast<gen::cast_t>(value)) {
    auto llvm_inner_value = lower_decl(cast->value->name, cast->value->type, builder,
                                       llvm_module, cast->value, lower_env);
    auto llvm_value = builder.CreateBitCast(llvm_inner_value,
                                            get_llvm_type(builder, cast->type));
    return llvm::cast<llvm::Constant>(set_llvm_value(
        lower_env, name, type, llvm::dyn_cast<llvm::Constant>(llvm_value)));
  } else if (auto function = dyncast<gen::function_t>(value)) {
    types::type_t::refs type_terms;
    unfold_binops_rassoc(ARROW_TYPE_OPERATOR, function->type, type_terms);
    /* this function will not be called directly, it will be packaged into a
     * closure. the type system does not reflect the difference between
     * functions or closures, but here when we are lowering, we need to be
     * honest with LLVM. all functions in Zion are capable of taking closure
     * environments, but not all use them. */
    assert(type_terms.size() >= 1);
    auto return_type = type_arrows(type_terms, 1);
    types::type_t::refs actual_type_terms = {
        type_terms[0], type_id(make_iid("__closure_t")), return_type};
    return llvm::cast<llvm::Constant>(set_llvm_value(
        lower_env, name, type,
        llvm_start_function(builder, llvm_module, actual_type_terms,
                            name + " :: " + function->type->repr())));
  } else if (auto builtin = dyncast<gen::builtin_t>(value)) {
    assert(false);
    return nullptr;
  } else if (auto argument = dyncast<gen::argument_t>(value)) {
    assert(false);
    return nullptr;
  } else if (auto goto_ = dyncast<gen::goto_t>(value)) {
    assert(false);
    return nullptr;
  } else if (auto cond_branch = dyncast<gen::cond_branch_t>(value)) {
    assert(false);
    return nullptr;
  } else if (auto callsite = dyncast<gen::callsite_t>(value)) {
    assert(false);
    return nullptr;
  } else if (auto return_ = dyncast<gen::return_t>(value)) {
    assert(false);
    return nullptr;
  } else if (auto load = dyncast<gen::load_t>(value)) {
    assert(false);
    return nullptr;
  } else if (auto store = dyncast<gen::store_t>(value)) {
    assert(false);
    return nullptr;
  } else if (auto tuple = dyncast<gen::gen_tuple_t>(value)) {
    log("lower_decl of a tuple %s :: %s", name.c_str(),
        value->type->str().c_str());
    if (tuple->parent.lock() == nullptr) {
      /* we are in global scope, creating a constant */
      return llvm::cast<llvm::Constant>(set_llvm_value(
          lower_env, name, type,
          lower_tuple_global(name, builder, llvm_module, tuple, lower_env)));
    } else {
      // TODO: handle make_tuple calls onto the heap
      assert_not_impl();
      return nullptr;
    }
  } else if (auto tuple_deref = dyncast<gen::gen_tuple_deref_t>(value)) {
    assert(false);
    return nullptr;
  }

  dbg();
  throw user_error(value->get_location(), "unhandled lower for %s",
                   value->str().c_str());
}

llvm::Value *lower_literal(llvm::IRBuilder<> &builder,
                           types::type_t::ref type,
                           const token_t &token) {
  debug_above(6, log("emitting literal %s :: %s", token.str().c_str(),
                     type->str().c_str()));
  if (type_equality(type, type_id(make_iid(INT_TYPE)))) {
    return builder.getZionInt(atoll(token.text.c_str()));
  } else if (type_equality(type,
                           type_operator({type_id(make_iid(PTR_TYPE_OPERATOR)),
                                          type_id(make_iid(CHAR_TYPE))}))) {
    /* char * */
    auto llvm_literal = llvm_create_global_string_constant(
        builder, *llvm_get_module(builder), unescape_json_quotes(token.text));
    debug_above(
        6, log("emitting llvm literal %s", llvm_print(llvm_literal).c_str()));
    return llvm_literal;
  }

  assert_not_impl();
  return nullptr;
}

llvm::Value *lower_tuple_alloc(
    llvm::IRBuilder<> &builder,
    gen::gen_tuple_t::ref tuple,
    std::map<std::string, llvm::Value *> &locals,
    const std::map<gen::block_t::ref,
                   llvm::BasicBlock *,
                   gen::block_t::comparator_t> &block_map,
    std::map<gen::block_t::ref, bool, gen::block_t::comparator_t>
        &blocks_visited,
    lower_env_t &env) {
  std::vector<llvm::Value *> llvm_dims;
  int i = 0;
  for (auto dim : tuple->dims) {
    llvm_dims.push_back(
        lower_value(builder, dim, locals, block_map, blocks_visited, env));
    debug_above(7, log("llvm_dims[%d] == %s", i,
                       llvm_print(llvm_dims[i]->getType()).c_str()));
    ++i;
  }
  auto llvm_tuple_pointer_type = get_llvm_type(builder, tuple->type);
  auto llvm_tuple_type = llvm_tuple_pointer_type->getPointerElementType();
  std::vector<llvm::Type *> alloc_terms{builder.getInt64Ty()};
  debug_above(6, log("need to allocate a tuple of type %s",
                     llvm_print(llvm_tuple_pointer_type).c_str()));
  auto llvm_module = llvm_get_module(builder);
  auto llvm_alloc_func_decl = llvm::cast<llvm::Function>(
      llvm_module->getOrInsertFunction(
          "malloc",
          llvm_create_function_type(builder, alloc_terms,
                                    builder.getInt8Ty()->getPointerTo())));
  llvm::Value *llvm_allocated_tuple = builder.CreateBitCast(
      builder.CreateCall(llvm_alloc_func_decl,
                         std::vector<llvm::Value *>{
                             llvm_sizeof_type(builder, llvm_tuple_type)}),
      llvm_tuple_pointer_type);

  llvm::Value *llvm_zero = llvm::ConstantInt::get(
      llvm::Type::getInt32Ty(builder.getContext()), 0);

  debug_above(7,
              log("llvm_tuple_type = %s", llvm_print(llvm_tuple_type).c_str()));
  /* actually copy the dims into the allocated space */
  for (int i = 0; i < llvm_dims.size(); ++i) {
    llvm::Value *llvm_index = llvm::ConstantInt::get(
        llvm::Type::getInt32Ty(builder.getContext()), i);
    llvm::Value *llvm_gep_args[] = {llvm_zero, llvm_index};
    debug_above(7, log("builder.CreateStore(%s, builder.CreateInBoundsGEP(%s, "
                       "%s, {0, %d}))",
                       llvm_print(llvm_dims[i]->getType()).c_str(),
                       llvm_print(llvm_tuple_type).c_str(),
                       llvm_print(llvm_allocated_tuple->getType()).c_str(), i));
    llvm::Value *llvm_member_address = builder.CreateInBoundsGEP(
        llvm_tuple_type, llvm_allocated_tuple, llvm_gep_args);
    debug_above(7, log("GEP returned %s with type %s",
                       llvm_print(llvm_member_address).c_str(),
                       llvm_print(llvm_member_address->getType()).c_str()));

    builder.CreateStore(
        builder.CreateBitCast(
            llvm_dims[i],
            llvm_member_address->getType()->getPointerElementType()),
        llvm_member_address);
  }
  return llvm_allocated_tuple;
}

llvm::Constant *lower_tuple_global(std::string name,
                                   llvm::IRBuilder<> &builder,
                                   llvm::Module *llvm_module,
                                   gen::gen_tuple_t::ref tuple,
                                   lower_env_t &env) {
  llvm::Type *llvm_type = get_llvm_type(builder, tuple->type);
  llvm::StructType *llvm_struct_type = llvm::dyn_cast<llvm::StructType>(
      llvm_type->getPointerElementType());
  assert(llvm_struct_type != nullptr);
  debug_above(
      5, log("lower_tuple_global wants to create a tuple of type %s or %s",
             tuple->type->str().c_str(), llvm_print(llvm_struct_type).c_str()));

  std::vector<llvm::Constant *> llvm_struct_data;
  for (auto dim : tuple->dims) {
    /* for each element in the tuple, let's get its Value */
    llvm::Value *llvm_value = maybe_get_llvm_value(env, dim->name, dim->type);
    if (llvm_value == nullptr) {
      debug_above(
          6,
          log("%s does not exist, normally this would have been registered by "
              "a call to lower::set_llvm_value. Going to try to recurse for "
              "it...",
              dim->name.c_str()));
      llvm_value = lower_decl(dim->name, dim->type, builder, llvm_module, dim, env);
      debug_above(5, log("dim's llvm_value is %s", llvm_print(llvm_value).c_str()));
    }

    if (llvm_value != nullptr) {
      llvm::Constant *llvm_dim_const = llvm::dyn_cast<llvm::Constant>(
          llvm_value);
      if (llvm_dim_const == nullptr) {
        throw user_error(dim->get_location(),
                         "non-constant global dim element found %s",
                         dim->name.c_str());
      }
      llvm_struct_data.push_back(llvm_dim_const);
    } else {
      throw user_error(dim->get_location(), "unable to find llvm_value for %s",
                       dim->str().c_str());
    }
  }

  debug_above(
      5, log("found %d elements for struct", (int)llvm_struct_data.size()));
  return llvm_create_struct_instance(name, llvm_module, llvm_struct_type,
                                     llvm_struct_data);
}

llvm::Value *lower_value(
    llvm::IRBuilder<> &builder,
    gen::value_t::ref value,
    std::map<std::string, llvm::Value *> &locals,
    const std::map<gen::block_t::ref,
                   llvm::BasicBlock *,
                   gen::block_t::comparator_t> &block_map,
    std::map<gen::block_t::ref, bool, gen::block_t::comparator_t>
        &blocks_visited,
    lower_env_t &env) {
  value = resolve_proxy(value);
  assert(value != nullptr);

  llvm::Value *llvm_previously_computed_value = get(locals, value->name,
                                                    (llvm::Value *)nullptr);

  if (llvm_previously_computed_value != nullptr) {
    return llvm_previously_computed_value;
  }

  /* make sure that the block that this value is defined in has been evaluated
   */
  lower_block(builder, value->parent.lock(), locals, block_map, blocks_visited,
              env);

  std::stringstream ss;
  value->render(ss);
  debug_above(5, log("Lowering value %s", ss.str().c_str()));
  if (auto unit = dyncast<gen::unit_t>(value)) {
    return llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo());
  } else if (auto literal = dyncast<gen::literal_t>(value)) {
    return lower_literal(builder, literal->type, literal->token);
  } else if (auto phi_node = dyncast<gen::phi_node_t>(value)) {
    assert_not_impl();
    return nullptr;
  } else if (auto cast = dyncast<gen::cast_t>(value)) {
    auto llvm_inner_value = lower_value(builder, cast->value, locals, block_map,
                                        blocks_visited, env);
    auto llvm_value = builder.CreateBitCast(llvm_inner_value,
                                            get_llvm_type(builder, cast->type));
    locals[cast->name] = llvm_value;
    return llvm_value;
  } else if (auto function = dyncast<gen::function_t>(value)) {
    auto llvm_value = get(env, function->name, function->type,
                          (llvm::Value *)nullptr);
    assert(llvm_value != nullptr);
    return llvm_value;
  } else if (auto builtin = dyncast<gen::builtin_t>(value)) {
    std::vector<llvm::Value *> params;
    for (auto param : builtin->params) {
      params.push_back(
          lower_value(builder, param, locals, block_map, blocks_visited, env));
    }

    return lower_builtin(builder, builtin->id.name, params);
  } else if (auto argument = dyncast<gen::argument_t>(value)) {
    int i = 0;
    for (auto &arg : builder.GetInsertBlock()->getParent()->args()) {
      if (i++ == argument->index) {
        return &arg;
      }
    }
    throw user_error(
        INTERNAL_LOC(), "failed to find argument %d in function %s",
        argument->index,
        builder.GetInsertBlock()->getParent()->getName().str().c_str());
  } else if (auto goto_ = dyncast<gen::goto_t>(value)) {
    assert_not_impl();
    return nullptr;
  } else if (auto cond_branch = dyncast<gen::cond_branch_t>(value)) {
    assert_not_impl();
    return nullptr;
  } else if (auto callsite = dyncast<gen::callsite_t>(value)) {
    /* all callsites are presumed to be closures */
    debug_above(5, log("lowering callsite: %s", callsite->str().c_str()));
    std::vector<llvm::Value *> llvm_params;
    for (auto param : callsite->params) {
      llvm_params.push_back(
          lower_value(builder, param, locals, block_map, blocks_visited, env));
      debug_above(
          8, log("llvm_param is %s", llvm_print(llvm_params.back()).c_str()));
    }
    llvm::Value *llvm_callee = lower_value(builder, callsite->callable, locals,
                                           block_map, blocks_visited, env);
    // log("llvm_callee = %s", llvm_print(llvm_callee).c_str());
    // log("llvm_callee_type = %s", llvm_print(llvm_callee->getType()).c_str());
    auto llvm_callee_type = llvm::dyn_cast<llvm::FunctionType>(
        llvm_callee->getType()->getPointerElementType());
    assert(llvm_callee_type != nullptr);
    // log("llvm_callee is %s", llvm_print(llvm_callee).c_str());
    // log("--------------------------------------------------------------------");
    // log("callee type params count = %d",
    //    int(llvm_callee_type->params().size()));
    // log("inbound param count = %d", int(llvm_params.size()));

    llvm::Type *llvm_return_type = get_llvm_type(builder, callsite->type);

    /* add the closure to its own param list */
    llvm_params.push_back(builder.CreateBitCast(
        llvm_callee, builder.getInt8Ty()->getPointerTo()));
    llvm::StructType *llvm_closure_struct_type =
        llvm::StructType::create(
            builder.getContext(),
            {llvm_create_function_type(builder, llvm_get_types(llvm_params),
                                       llvm_return_type)
                 ->getPointerTo()});
            llvm_closure_struct_type->setName("ClosureForCallsite");
    llvm::Type *llvm_closure_type = llvm_closure_struct_type->getPointerTo();

    llvm::Value *llvm_closure = builder.CreateBitCast(llvm_callee,
                                                      llvm_closure_type);
    llvm::Value *llvm_zero = llvm::ConstantInt::get(
        llvm::Type::getInt32Ty(builder.getContext()), 0);
    llvm::Value *llvm_gep_args[] = {llvm_zero, llvm_zero};
    llvm::Value *llvm_func_to_call = builder.CreateLoad(
        builder.CreateInBoundsGEP(
            llvm_closure->getType()->getPointerElementType(), llvm_closure,
            llvm_gep_args));
    // log("llvm_func_to_call = %s", llvm_print(llvm_func_to_call).c_str());
    llvm::Value *llvm_callsite = llvm_create_call_inst(
        builder, llvm_func_to_call, llvm_params);
    // log("llvm_callsite = %s", llvm_print(llvm_callsite).c_str());
    locals[callsite->name] = llvm_callsite;
    return llvm_callsite;
  } else if (auto return_ = dyncast<gen::return_t>(value)) {
    llvm::Value *llvm_return_value = lower_value(
        builder, return_->value, locals, block_map, blocks_visited, env);
    return builder.CreateRet(llvm_return_value);
  } else if (auto load = dyncast<gen::load_t>(value)) {
    assert_not_impl();
    return nullptr;
  } else if (auto store = dyncast<gen::store_t>(value)) {
    assert_not_impl();
    return nullptr;
  } else if (auto tuple = dyncast<gen::gen_tuple_t>(value)) {
    if (tuple->parent.lock() != nullptr) {
      /* we are in a function body */
      return lower_tuple_alloc(builder, tuple, locals, block_map,
                               blocks_visited, env);
    } else {
      /* we are at global scope */
      return lower_tuple_global("" /*name*/, builder, llvm_get_module(builder),
                                tuple, env);
    }
  } else if (auto tuple_deref = dyncast<gen::gen_tuple_deref_t>(value)) {
    std::stringstream ss;
    tuple_deref->render(ss);
    debug_above(7, log("tuple_deref = %s", ss.str().c_str()));
    llvm::Value *llvm_value = lower_value(builder, tuple_deref->value, locals,
                                          block_map, blocks_visited, env);

    assert(tuple_deref->index >= 0);
    auto gep_path = std::vector<llvm::Value *>{
        builder.getInt32(0), builder.getInt32(tuple_deref->index)};
    return builder.CreateLoad(builder.CreateInBoundsGEP(llvm_value, gep_path));
  }
  assert_not_impl();
  return nullptr;
}

void lower_block(llvm::IRBuilder<> &builder,
                 gen::block_t::ref block,
                 std::map<std::string, llvm::Value *> &locals,
                 const std::map<gen::block_t::ref,
                                llvm::BasicBlock *,
                                gen::block_t::comparator_t> &block_map,
                 std::map<gen::block_t::ref, bool, gen::block_t::comparator_t>
                     &blocks_visited,
                 lower_env_t &env) {
  if (block == nullptr) {
    /* maybe the value we are lowering doesn't need a block */
    return;
  }

  llvm::BasicBlock *llvm_block = get(block_map, block,
                                     (llvm::BasicBlock *)nullptr);

  if (llvm_block == builder.GetInsertBlock()) {
    /* we're already checking this block right now, so be cool */
    return;
  }

  INDENT(1,
         string_format(
             "lower_block(..., %s, locals={%s}, ...)", block->str().c_str(),
             join_with(locals, ", ",
                       [](const std::pair<std::string, llvm::Value *> &pair) {
                         return pair.first + ": " + llvm_print(pair.second);
                       })
                 .c_str()));

  if (!in(block, block_map)) {
    dbg();
  }

  auto visited_iter = blocks_visited.find(block);
  if (visited_iter == blocks_visited.end()) {
    assert(!blocks_visited[block]);

    /* mark this block as grey */
    blocks_visited[block] = false;

    llvm::IRBuilderBase::InsertPointGuard ipg(builder);
    builder.SetInsertPoint(llvm_block);
    for (auto instruction : block->instructions) {
      locals[instruction->name] = lower_value(builder, instruction, locals,
                                              block_map, blocks_visited, env);
    }

    /* mark this block as white */
    blocks_visited[block] = true;
  } else {
    /* if this assert fires, then a value that dominates its own use somehow */
    assert(visited_iter->second);
  }
}

void lower_function(llvm::IRBuilder<> &builder,
                    llvm::Module *llvm_module,
                    std::string name,
                    types::type_t::ref type,
                    gen::function_t::ref function,
                    llvm::Value *llvm_value,
                    lower_env_t &env) {
  // std::cout << "Lowering " << name << std::endl;
  // function->render(std::cout);

  llvm::IRBuilderBase::InsertPointGuard ipg(builder);
  llvm::Function *llvm_function = llvm::dyn_cast<llvm::Function>(llvm_value);
  assert(llvm_function != nullptr);

  // std::vector<std::shared_ptr<gen::argument_t>> args;
  std::map<std::string, llvm::Value *> locals;
  auto arg_iterator = llvm_function->arg_begin();
  for (auto arg : function->args) {
    if (arg_iterator == llvm_function->arg_end()) {
      log("arguments for %s :: %s and %s don't match", function->str().c_str(),
          function->type->str().c_str(), llvm_print(llvm_function).c_str());
      log("function = %s(%s)", function->name.c_str(),
          join_str(function->args, ", ").c_str());

      assert_not_impl();
    }

    locals[arg->name] = arg_iterator++;
  }

  std::map<gen::block_t::ref, llvm::BasicBlock *, gen::block_t::comparator_t>
      block_map;
  std::map<gen::block_t::ref, bool, gen::block_t::comparator_t> blocks_visited;

  for (auto block : function->blocks) {
    block_map[block] = llvm::BasicBlock::Create(builder.getContext(),
                                                block->name, llvm_function);
  }

  for (auto block : function->blocks) {
    lower_block(builder, block, locals, block_map, blocks_visited, env);
  }
  llvm_verify_function(INTERNAL_LOC(), llvm_function);
}

void lower_populate(llvm::IRBuilder<> &builder,
                    llvm::Module *llvm_module,
                    std::string name,
                    types::type_t::ref type,
                    gen::value_t::ref value,
                    llvm::Value *llvm_value,
                    lower_env_t &env) {
  debug_above(4, log("lower_populate(%s, ..., %s, ...)", name.c_str(),
                     value->str().c_str()));

  assert(value != nullptr);
  auto resolved_value = resolve_proxy(value);

  assert(resolved_value != nullptr);
  value = resolved_value;

  if (auto unit = dyncast<gen::unit_t>(value)) {
    assert(false);
    return;
  } else if (auto literal = dyncast<gen::literal_t>(value)) {
    assert(false);
    return;
  } else if (auto phi_node = dyncast<gen::phi_node_t>(value)) {
    assert(false);
    return;
  } else if (auto cast = dyncast<gen::cast_t>(value)) {
    /* casts don't need further lowering */
    debug_above(6, log("lower_populate called on a cast which already has the "
                       "llvm value %s",
                       llvm_print(llvm_value).c_str()));
    return;
  } else if (auto function = dyncast<gen::function_t>(value)) {
    lower_function(builder, llvm_module, name, type, function, llvm_value, env);
    return;
  } else if (auto builtin = dyncast<gen::builtin_t>(value)) {
    assert(false);
    return;
  } else if (auto argument = dyncast<gen::argument_t>(value)) {
    assert(false);
    return;
  } else if (auto goto_ = dyncast<gen::goto_t>(value)) {
    assert(false);
    return;
  } else if (auto cond_branch = dyncast<gen::cond_branch_t>(value)) {
    assert(false);
    return;
  } else if (auto callsite = dyncast<gen::callsite_t>(value)) {
    assert(false);
    return;
  } else if (auto return_ = dyncast<gen::return_t>(value)) {
    assert(false);
    return;
  } else if (auto load = dyncast<gen::load_t>(value)) {
    assert(false);
    return;
  } else if (auto store = dyncast<gen::store_t>(value)) {
    assert(false);
    return;
  } else if (auto tuple = dyncast<gen::gen_tuple_t>(value)) {
    assert(false);
    return;
  } else if (auto tuple_deref = dyncast<gen::gen_tuple_deref_t>(value)) {
    assert(false);
    return;
  }

  throw user_error(value->get_location(), "unhandled lower for %s",
                   value->str().c_str());
}

int lower(std::string main_function, const gen::gen_env_t &gen_env) {
  llvm::LLVMContext context;
  llvm::Module *module = new llvm::Module("program", context);
  llvm::IRBuilder<> builder(context);

  try {
    lower::lower_env_t lower_env;
    int lowering_index = 0;
    for (auto pair : gen_env) {
      for (auto overload : pair.second) {
        const std::string &name = pair.first;
        types::type_t::ref type = overload.first;
        gen::value_t::ref value = overload.second;
        std::stringstream ss;
        value->render(ss);
        log("%d: " c_id("%s") " :: %s = %s", lowering_index, name.c_str(),
            type->str().c_str(), ss.str().c_str());
        if (resolve_proxy(value) == nullptr) {
          log("we got to the lower stage and yet the gen::proxy_value_t for %s "
              ":: "
              "%s is not resolved to an actual gen::value_t",
              name.c_str(), type->str().c_str());
          dbg();
        }
        ++lowering_index;
      }
    }
    lowering_index = 0;
    for (auto pair : gen_env) {
      for (auto overload : pair.second) {
        const std::string &name = pair.first;
        types::type_t::ref type = overload.first;
        gen::value_t::ref value = overload.second;

        std::stringstream ss;
        value->render(ss);
        debug_above(6, log("emitting #%d " c_id("%s") " :: %s = %s",
                           lowering_index, name.c_str(), type->str().c_str(),
                           ss.str().c_str()));
        ++lowering_index;

        if (maybe_get_llvm_value(lower_env, name, type) != nullptr) {
          /* due to the referential nature of things, some symbols may be
           * declared prior to this outer iteration coming upon them. that's ok,
           * just skip them in that case, because we can assume that they (and
           * theiry child nodes) are already in the lower_env */
          continue;
        }
        lower_decl(name, type, builder, module, overload.second, lower_env);
      }
    }
    for (auto pair : gen_env) {
      for (auto overload : pair.second) {
        const std::string &name = pair.first;
        types::type_t::ref type = overload.first;
        gen::value_t::ref value = overload.second;

        llvm::Value *llvm_value = get_llvm_value(lower_env, name, type);
        lower_populate(builder, module, name, type, value, llvm_value,
                       lower_env);
      }
    }
    std::ofstream ofs;
    ofs.open("zion-output.llir", std::ofstream::out);
    ofs << llvm_print_module(*module) << std::endl;
    ofs.close();
    return EXIT_SUCCESS;
  } catch (user_error &e) {
    print_exception(e);
    return EXIT_FAILURE;
  }
}
} // namespace lower
