//===========================================================================
/// \file llvm-frontend.h LLVM output backend for the firtree kernel language.
///
/// This file defines the interface to the LLVM output backend. The backend
/// also takes care of checking the well-formedness of the passed abstract
/// depth grammar.

#ifndef __LLVM_FRONTEND_H
#define __LLVM_FRONTEND_H

#include "ptm_gen.h" // General Parsing Routines
#include "ptm_pp.h"  // Pretty Printer
#include "gls.h"     // General Language Services
#include "symbols.h" // Datatype: Symbols

#include <common/common.h>

// STL templates
#include <vector>
#include <map>

// On OSX, the styx headers define 'FreeMem' which is also defined by the
// Mac memory handlers. Since we don't use it directly, we can hack around.
#define FreeMem StyxFreeMem

#undef FreeMem

#include "llvm/Module.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Constants.h"

#include "styx-parser/firtree_int.h"

#include "llvm-compiled-kernel.h"

namespace Firtree
{

class CompileErrorException;

/// Opaque type used for passing the current LLVM context around
/// between code-generators.
struct LLVMContext;

//=======================================================================
/// \brief The possible type qualifiers.
enum KernelTypeQualifier {
	TyQualNone,           ///< The 'default' qualifier.
	TyQualConstant,       ///< The value is const (i.e. non-assignable).
	TyQualStatic,         ///< The value is static (i.e. it's 
	///< value should be available at link.
	TyQualInvalid = -1,   ///< An 'invalid' qualifier.
};

//===========================================================================
/// \brief A structure defining a fully specified type.
struct FullType {

	KernelTypeQualifier    Qualifier;
	KernelTypeSpecifier   Specifier;

	/// The constructor defines the default values.
	inline FullType()
			: Qualifier( TyQualInvalid ), Specifier( TySpecInvalid ) { }
	inline FullType( KernelTypeQualifier q, KernelTypeSpecifier s )
			: Qualifier( q ), Specifier( s ) { }

	/// Return a flag indicating the validity of the passed type.
	inline static bool IsValid( const FullType& t ) {
		return ( t.Qualifier != TyQualInvalid ) &&
		       ( t.Specifier != TySpecInvalid );
	}

	/// Static initialiser from a perse tree qualifier and specifier. If
	/// the qualifier is NULL, it is assumed to be TyQualNone.
	static FullType FromQualiferAndSpecifier( firtreeTypeQualifier qual,
	        firtreeTypeSpecifier spec );

	/// Static initialiser from a perse tree fully specified type.
	static FullType FromFullySpecifiedType( firtreeFullySpecifiedType t );

	/// Convert this type to the matching LLVM type. Pass a LLVM context
	/// which can be used for error reporting.
	const llvm::Type* ToLLVMType( LLVMContext* ctx ) const;

	/// Return true if this type is static.
	inline bool IsStatic() const {
		return Qualifier == TyQualStatic;
	}

	/// Return true if this type is const.
	inline bool IsConst() const {
		return Qualifier == TyQualConstant;
	}

	/// Return true if this type can be promoted to type t by simply removing
	/// a const qualifier.
	inline bool IsConstCastableTo( const FullType& t ) {
		if ( *this == t ) {
			return true;
		}

		if ( Qualifier == Firtree::TyQualConstant ) {
			if ( t.Specifier == Specifier ) {
				return true;
			}
		}

		return false;
	}

	/// Return true if this type is a scalar type (int, float, bool).
	inline bool IsScalar() const {
		return ( Specifier == TySpecFloat ) || ( Specifier == TySpecInt ) ||
		       ( Specifier == TySpecBool );
	}

	/// Return true if this type is a vector type (vec{2,3,4} or color).
	inline bool IsVector() const {
		return ( Specifier == TySpecVec2 ) || ( Specifier == TySpecVec3 ) ||
		       ( Specifier == TySpecVec4 ) || ( Specifier == TySpecColor );
	}

	/// Return true if this type is a compound type (sampler).
	inline bool IsCompound() const {
		return ( Specifier == TySpecSampler );
	}

	/// Return the artiy of this type. Scalar types have arity 1,
	/// vector types have artiy 2-4 and compound types have arity 0.
	inline uint32_t GetArity() const {
		if ( IsScalar() ) {
			return 1;
		}

		if ( IsVector() ) {
			switch ( Specifier ) {
				case TySpecVec2:
					return 2;
					break;
				case TySpecVec3:
					return 3;
					break;
				case TySpecVec4:
				case TySpecColor:
					return 4;
					break;
				default:
					// Do nothing, handled by return below.
					break;
			}
		}

		return 0;
	}

	inline bool operator == ( const FullType& b ) const {
		return ( Qualifier == b.Qualifier ) && ( Specifier == b.Specifier );
	}

	inline bool operator != ( const FullType& b ) const {
		return ( Qualifier != b.Qualifier ) || ( Specifier != b.Specifier );
	}
};

//===========================================================================
/// \brief A structure recording the declaration of a variable.

struct VariableDeclaration {
	/// The LLVM value which points to the variable in memory.
	llvm::Value*      value;

	/// The Styx symbol associated with the variable's name.
	symbol            name;

	/// The variable's type.
	FullType          type;

	/// Is the variable initialised. i.e., has it been assigned to at least
	/// once.
	bool              initialised;
};

//===========================================================================
/// \brief A scoped symbol table
///
/// This class defines the interface to a scoped symbol table implementation.
/// Each symbol is associated with a VariableDeclaration. A backend uses this
/// table to record associations between a symbol's name and the declaration
/// of the variable associated with it.
///
/// The table supports nested 'scopes' which allows symbol definitions to
/// be removed, or 'popped', when the scope changes. 'Popping' the scope
/// causes all symbols added to the table since the matching 'push' to
/// be removed. Scope pushes/pops can be nested.
class SymbolTable
{

	public:
		SymbolTable();
		virtual ~SymbolTable();

		// Push the current scope onto the stack.
		void PushScope();

		// Pop the current scope from the stack. All symbols added since the
		// last (nested) call to PushScope are removed.
		void PopScope();

		// Find the variable declaration corresponding to the passed symbol.
		// If there is no matching declaration, return NULL.
		const VariableDeclaration* LookupSymbol( symbol s ) const;

		// Add the passed VariableDeclaration to the symbol table.
		void AddDeclaration( const VariableDeclaration& decl );

	private:
		// A vector of scopes, each scope is a map between a symbol
		// and a pointer to the associated value.
		std::vector< std::map<symbol, VariableDeclaration> > m_scopeStack;
};

//===========================================================================
/// \brief The LLVM backend class.
///
/// This class defines the interface to the LLVM output backend. A compiler
/// constructs an instance of this class with a Styx abstract depth grammer
/// and can query the LLVM module genereated therefrom.
class LLVMFrontend
{

	public:
		/// Construct the backend by passing it the top-level translation
		/// unit node.
		LLVMFrontend( firtree top_level_term,
				LLVM::KernelFunctionList* kernel_list = NULL );

		virtual ~LLVMFrontend();

		/// Retrieve the LLVM module which was constructed.
		llvm::Module* GetCompiledModule() const;

		/// Retrieve the compilation success flag: true on success, false
		/// otherwise.
		bool GetCompilationSucceeded() const;

		/// Retrieve the compilation log.
		const std::vector<std::string>& GetLog() const {
			return m_Log;
		}

		//===================================================================
		/// These methods are intended to be called by code generation
		/// objects.
		///@{

		/// Throw a compiler exception.
		void ThrowCompileErrorException(
		    const char* file, int line, const char* func,
		    bool is_ice, PT_Term term, const char* format, ... );

		/// Handle a compiler error exception by recording it in the error
		/// log.
		void HandleCompilerError(
		    const CompileErrorException& error_exception );

		/// Record a warning in the log.
		void RecordWarning( PT_Term term, const char* format, ... );

		/// Record an error or warning in the log.
		void RecordMessage( PT_Term term, bool is_error,
		                    const char* format, ... );

		///@}

	private:
		LLVMContext*       m_LLVMContext;     ///< The current LLVM context.

		std::vector<std::string>  m_Log;      ///< The compilation log.

		bool               m_Status;          ///< The compilation status.
};

} // namespace Firtree

#endif // __LLVM_FRONTEND_H 

// vim:sw=4:ts=4:cindent:noet
