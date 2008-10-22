//===========================================================================
/// \file llvm_emit_decl.cc Implementation of Firtree::EmitDeclarations.

#include "llvm_backend.h"
#include "llvm_private.h"

#include <firtree/main.h>

#include "llvm_emit_decl.h"

#include <llvm/Instructions.h>

using namespace llvm;

namespace Firtree {

//===========================================================================
EmitDeclarations::EmitDeclarations(LLVMContext* ctx)
{
  m_Context = ctx;
}

//===========================================================================
EmitDeclarations::~EmitDeclarations()
{
  // Not really necessary but it gives one the warm fuzzy feeling of being
  // a Good Boy.
  m_Context = NULL;
}

//===========================================================================
/// Emit a single firtree declaration.
void EmitDeclarations::emitDeclaration(firtreeExternalDeclaration decl)
{
  firtreeFunctionDefinition func_def;
  firtreeFunctionPrototype func_proto;

  // Decide if this declaration is a prototype or definition and
  // act accordingly.
  if(firtreeExternalDeclaration_definefuntion(decl, &func_def))
  {
    emitFunction(func_def);
  } else if(firtreeExternalDeclaration_declarefunction(decl, &func_proto)) {
    emitPrototype(func_proto);
  } else {
    FIRTREE_LLVM_ICE(m_Context, decl, "Unknown declaration node.");
  }
}

//===========================================================================
/// Emit a list of firtree declarations.
void EmitDeclarations::emitDeclarationList(
    GLS_Lst(firtreeExternalDeclaration) decl_list)
{
  GLS_Lst(firtreeExternalDeclaration) tail = decl_list;
  firtreeExternalDeclaration decl;

  GLS_FORALL(tail, decl_list) {
    decl = GLS_FIRST(firtreeExternalDeclaration, tail);

    // Wrap each external declaration in an exception handler so 
    // we can re-start from here.
    try {
      emitDeclaration(decl);
    } catch (CompileErrorException e) {
      m_Context->Backend->HandleCompilerError(e);
    }
  }
}

//===========================================================================
/// Emit a function prototype.
void EmitDeclarations::emitPrototype(firtreeFunctionPrototype proto_term)
{
  FunctionPrototype prototype;
  constructPrototypeStruct(prototype, proto_term);

  // Check this prototype does not conflict with an existing one.
  if(existsConflictingPrototype(prototype)) {
    FIRTREE_LLVM_ERROR(m_Context, proto_term, "Function '%s' "
        "conflicts with previously declared or defined function.",
        symbolToString(prototype.Name));
  }

  // Insert this prototype into the function table.
  m_FuncTable.insert( 
      std::pair<symbol, FunctionPrototype>(prototype.Name, prototype) );
}

//===========================================================================
/// Emit a function definition.
void EmitDeclarations::emitFunction(firtreeFunctionDefinition func)
{
  firtreeFunctionPrototype function_prototype;
  GLS_Lst(firtreeExpression) function_body;

  if(!firtreeFunctionDefinition_functiondefinition(func,
        &function_prototype, &function_body)) {
    FIRTREE_LLVM_ICE(m_Context, func, "Invalid function definition.");
  }

  // Form prototype struct from parse tree term.
  FunctionPrototype prototype;
  constructPrototypeStruct(prototype, function_prototype);
  prototype.DefinitionTerm = func;

  // Do we have an existing prototype for this function?
  std::multimap<symbol, FunctionPrototype>::iterator existing_proto_it;
  if(existsConflictingPrototype(prototype, &existing_proto_it)) {
    // Check that the existing prototype does not have a definition
    // already.
    if(existing_proto_it->second.DefinitionTerm != NULL) {
      FIRTREE_LLVM_ERROR(m_Context, func, "Multiple defintions of "
          "function '%s'.", symbolToString(prototype.Name));
    }

    // Check the function qualifiers
    if(existing_proto_it->second.Qualifier != prototype.Qualifier) {
      FIRTREE_LLVM_ERROR(m_Context, func, "Conflicting function qualifiers "
          "between definition and declaration of function '%s'.",
          symbolToString(prototype.Name));
    }

    // Check the return types are compatible.
    if(existing_proto_it->second.ReturnType != prototype.ReturnType) {
      FIRTREE_LLVM_ERROR(m_Context, func, "Conflicting return types "
          "between definition and declaration of function '%s'.",
          symbolToString(prototype.Name));
    }

    // Erase the old prototype.
    m_FuncTable.erase(existing_proto_it);
  }
    
  // This function does not yet exist in the function table, since
  // we erase it above if it did so. Insert it.
  m_FuncTable.insert( 
      std::pair<symbol, FunctionPrototype>(prototype.Name, prototype) );

  // Create a LLVM function object corresponding to this definition.
  
  std::vector<const Type*> param_llvm_types;
  std::vector<FunctionParameter>::const_iterator it = 
    prototype.Parameters.begin();
  while(it != prototype.Parameters.end())
  {
    // FIXME: in/out/inout types
    param_llvm_types.push_back(it->Type.ToLLVMType(m_Context));
    it++;
  }

  FunctionType *FT = FunctionType::get(
      prototype.ReturnType.ToLLVMType(m_Context),
      param_llvm_types, false);
  Function* F = NULL;
  if(prototype.Qualifier == FunctionPrototype::FuncQualKernel) {
    F = new Function(FT, 
        Function::ExternalLinkage, symbolToString(prototype.Name),
        m_Context->Module);
  } else {
    F = new Function(FT, 
        Function::InternalLinkage, symbolToString(prototype.Name),
        m_Context->Module);
  }

  prototype.LLVMFunction = F;
  m_Context->Function = F;

  // Create a basic block for this function
  BasicBlock *BB = new BasicBlock("entry", F);
  m_Context->BB = BB;

  // Create a symbol table
  m_Context->Variables = new SymbolTable();

  // Set names for all arguments.
  it = prototype.Parameters.begin();
  Function::arg_iterator AI = F->arg_begin();
  while(it != prototype.Parameters.end())
  {
    if(it->Name != NULL)
    {
      AI->setName(symbolToString(it->Name));

      // FIXME: Add function parameters to symbol table.
    }
    ++it;
    ++AI;
  }

  // Do the following inside a try/catch block
  // so that compile errors don't cause us to leak memory.
  try {
    // FIXME: Emit function.

    // Create a default return inst if there was no terminator.
    if(m_Context->BB->getTerminator() == NULL)
    {
      // Check function return type is void.
      if(prototype.ReturnType.Specifier != FullType::TySpecVoid)
      {
        FIRTREE_LLVM_ERROR(m_Context, func, "Control reaches end of "
            "non-void function.");
      }
      new ReturnInst(NULL, m_Context->BB);
    }
  } catch (CompileErrorException e) {
    m_Context->Backend->HandleCompilerError(e);
  }

  delete m_Context->Variables;
  m_Context->Variables = NULL;
  m_Context->BB = NULL;
  m_Context->Function = NULL;
}

//===========================================================================
/// Check for undefined functions (i.e. functions with
/// prototypes but no definition).
void EmitDeclarations::checkEmittedDeclarations()
{
  std::multimap<symbol, FunctionPrototype>::const_iterator it =
    m_FuncTable.begin();
  while(it != m_FuncTable.end()) {
    if((it->second.DefinitionTerm == NULL) && 
        (it->second.Qualifier != FunctionPrototype::FuncQualIntrinsic)) {
      try {
        FIRTREE_LLVM_ERROR(m_Context, it->second.PrototypeTerm,
            "No definition found for function '%s'.",
            symbolToString(it->second.Name));
      } catch (CompileErrorException e) {
        m_Context->Backend->HandleCompilerError(e);
      }
    }
    it++;
  }
}

//===========================================================================
/// Return true if there already exists a prototype registered
/// in the function table which conflicts with the passed prototype.
/// A prototype conflicts if it matches both the function name of
/// an existing prototype and the type and number of it's parameters.
/// 
/// Optionally, if matched_proto_iter_ptr is non-NULL, write an
/// iterator pointing to the matched prototype into it. If no prototype
/// is matched, matched_proto_iter_ptr has m_FuncTable.end() written
/// into it.
bool EmitDeclarations::existsConflictingPrototype(
    const FunctionPrototype& proto,
    std::multimap<symbol, FunctionPrototype>::iterator*
      matched_proto_iter_ptr)
{
  // Initially, assume no matching prototype.
  if(matched_proto_iter_ptr != NULL)
  {
    *matched_proto_iter_ptr = m_FuncTable.end();
  }

  // First test: does there exist at least one prototype with a matching
  // name.
  if(m_FuncTable.count(proto.Name) == 0)
  {
    // No match, return false.
    return false;
  }

  // If we got this far, there a potential conflict. Iterate over all of 
  // them.
  std::multimap<symbol, FunctionPrototype>::
    iterator it = m_FuncTable.begin();
  for( ; it != m_FuncTable.end(); it ++ )
  {
    if(it->first != proto.Name)
      continue;

    // Sanity check.
    FIRTREE_LLVM_ASSERT(m_Context, NULL, it->second.Name == proto.Name);

    // If the number of parameters is different, no conflict
    if(it->second.Parameters.size() != proto.Parameters.size()) {
      continue;
    }

    // Clear the is_conflict flag if any parameters have differing
    // type.
    bool is_conflict = true;
    std::vector<FunctionParameter>::const_iterator it_a =
      it->second.Parameters.begin();
    std::vector<FunctionParameter>::const_iterator it_b =
      proto.Parameters.begin();
    while((it_a != it->second.Parameters.end()) &&
        (it_b != proto.Parameters.end())) {
      if(it_a->Type != it_b->Type) {
        is_conflict = false;
      }
      it_a++;
      it_b++;
    }

    // Test the is_conflict flag and return signalling conflict if
    // one found.
    if(is_conflict) {
      if(matched_proto_iter_ptr != NULL)
      {
        *matched_proto_iter_ptr = it;
      }
      return true;
    }
  }
  
  // If we got this far, none of the candidate prototypes conflicted.
  return false;
}

//===========================================================================
/// Construct a FunctionPrototype struct from a 
/// firtreeFunctionPrototype parse-tree term.
void EmitDeclarations::constructPrototypeStruct(
    FunctionPrototype& prototype,
    firtreeFunctionPrototype proto_term)
{
  firtreeFunctionQualifier qual;
  firtreeFullySpecifiedType type;
  GLS_Tok name;
  GLS_Lst(firtreeParameterDeclaration) params;

  if(!firtreeFunctionPrototype_functionprototype(proto_term,
        &qual, &type, &name, &params))
  {
    FIRTREE_LLVM_ICE(m_Context, proto_term, "Invalid function prototype.");
  }

  prototype.PrototypeTerm = proto_term;
  prototype.DefinitionTerm = NULL;
  prototype.LLVMFunction = NULL;

  if(firtreeFunctionQualifier_function(qual)) {
    prototype.Qualifier = FunctionPrototype::FuncQualFunction;
  } else if(firtreeFunctionQualifier_kernel(qual)) {
    prototype.Qualifier = FunctionPrototype::FuncQualKernel;
  } else {
    FIRTREE_LLVM_ICE(m_Context, qual, "Invalid function qualifier.");
  }

  prototype.Name = GLS_Tok_symbol(name);
  if(prototype.Name == NULL) {
    FIRTREE_LLVM_ICE(m_Context, type, "Invalid name.");
  }

  prototype.ReturnType = FullType::FromFullySpecifiedType(type);
  if(!FullType::IsValid(prototype.ReturnType)) {
    FIRTREE_LLVM_ICE(m_Context, type, "Invalid type.");
  }

  // For each parameter, append a matching FunctionParameter structure
  // to the prototype's Parameters field.
  GLS_Lst(firtreeParameterDeclaration) params_tail;
  GLS_FORALL(params_tail, params) {
    firtreeParameterDeclaration param_decl = 
      GLS_FIRST(firtreeParameterDeclaration, params_tail);

    firtreeTypeQualifier param_type_qual;
    firtreeParameterQualifier param_qual;
    firtreeTypeSpecifier param_type_spec;
    firtreeParameterIdentifierOpt param_identifier;

    if(firtreeParameterDeclaration_parameterdeclaration(param_decl,
          &param_type_qual, &param_qual, &param_type_spec,
          &param_identifier))
    {
      FunctionParameter new_param;

      new_param.Term = param_decl;

      // Extract the parameter type
      new_param.Type = FullType::FromQualiferAndSpecifier(
          param_type_qual, param_type_spec);
      if(!FullType::IsValid(new_param.Type)) {
        FIRTREE_LLVM_ICE(m_Context, param_decl, "Invalid type.");
      }

      // Get the parameter name
      GLS_Tok parameter_name_token;
      if(firtreeParameterIdentifierOpt_named(param_identifier, 
            &parameter_name_token)) {
        new_param.Name = GLS_Tok_symbol(parameter_name_token);
      } else if(firtreeParameterIdentifierOpt_anonymous(param_identifier)) {
        new_param.Name = NULL;
      } else {
        FIRTREE_LLVM_ICE(m_Context, param_identifier, "Invalid identifier.");
      }

      // Get the parameter direction
      if(firtreeParameterQualifier_in(param_qual)) {
        new_param.Direction = FunctionParameter::FuncParamIn;
      } else if(firtreeParameterQualifier_out(param_qual)) {
        new_param.Direction = FunctionParameter::FuncParamOut;
      } else if(firtreeParameterQualifier_inout(param_qual)) {
        new_param.Direction = FunctionParameter::FuncParamInOut;
      } else {
        FIRTREE_LLVM_ICE(m_Context, param_qual, 
            "Invalid parameter qualifier.");
      }

      prototype.Parameters.push_back(new_param);
    } else {
      FIRTREE_LLVM_ICE(m_Context, param_decl,
          "Invalid parameter declaration.");
    }
  }

  // Kernels must return vec4
  if(prototype.Qualifier == FunctionPrototype::FuncQualKernel) {
    if(prototype.ReturnType.Specifier != FullType::TySpecVec4) {
      FIRTREE_LLVM_ERROR(m_Context, proto_term, "Kernel functions "
          "must have a return type of vec4.");
    }
  }
}

} // namespace Firtree

// vim:sw=2:ts=2:cindent:et
