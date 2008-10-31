//===========================================================================
/// \file llvm_emit_negate.cc

#include <firtree/main.h>

#include "llvm_backend.h"
#include "llvm_private.h"
#include "llvm_emit_decl.h"
#include "llvm_expression.h"
#include "llvm_type_cast.h"
#include "llvm_emit_constant.h"

#include <llvm/Instructions.h>

using namespace llvm;

namespace Firtree
{

//===========================================================================
/// \brief Class to emit a negation instruction.
class NegateEmitter : ExpressionEmitter
{
	public:
		NegateEmitter()
				: ExpressionEmitter() {
		}

		virtual ~NegateEmitter() {
		}

		/// Create an instance of this emitter.
		static ExpressionEmitter* Create() {
			return new NegateEmitter();
		}

		/// Emit the passed expression term returning a pointer to
		/// a ExpressionValue containing it's value. It is the
		/// responsibility of the caller to Release() this value.
		virtual ExpressionValue* Emit( LLVMContext* context,
		                               firtreeExpression expression ) {
			firtreeExpression negate_value_expr;
			if ( !firtreeExpression_negate( expression,
			                                &negate_value_expr ) ) {
				FIRTREE_LLVM_ICE( context, expression, "Invalid unary -." );
			}

			// Emit the code to calculate the value to negate.
			ExpressionValue* negate_value =
			    ExpressionEmitterRegistry::GetRegistry()->Emit(
			        context, negate_value_expr );
			ExpressionValue* return_value = NULL;

			try {
				// Depending on the type of the negation value, form
				// an appropriate LLVM value for '-1'.
				llvm::Value* negative_one;
				FullType req_type = negate_value->GetType();
				if ( req_type.IsVector() ) {
					std::vector<Constant*> neg_ones;
					for ( unsigned int i=0; i<req_type.GetArity(); ++i ) {
						neg_ones.push_back( ConstantFP::
						                    get( Type::FloatTy,
						                         APFloat( -1.f ) ) );
					}
					negative_one = ConstantVector::get( neg_ones );
				} else if ( req_type.IsScalar() ) {
					switch ( req_type.Specifier ) {
						case FullType::TySpecFloat:
							negative_one = ConstantFP::
							               get( Type::FloatTy,
							                    APFloat( -1.f ) );
							break;
						case FullType::TySpecInt:
							negative_one = ConstantInt::
							               get( Type::Int32Ty, -1 );
							break;
						case FullType::TySpecBool:
							negative_one = ConstantInt::
							               get( Type::Int1Ty, -1 );
							break;
						default:
							FIRTREE_LLVM_ERROR( context, negate_value_expr,
							                    "Expression is of invalid "
							                    "type for unary '-'." );
							break;
					}
				} else {
					FIRTREE_LLVM_ERROR( context, negate_value_expr,
					                    "Expression is of invalid type for "
					                    "unary '-'." );
				}

				llvm::Value* new_val = BinaryOperator::create(
				                           Instruction::Mul,
				                           negative_one,
				                           negate_value->GetLLVMValue(),
				                           "tmpnegate", context->BB );
				return_value = ConstantExpressionValue::
				               Create( context, new_val );

				FIRTREE_SAFE_RELEASE( negate_value );
			} catch ( CompileErrorException e ) {
				FIRTREE_SAFE_RELEASE( negate_value );
				FIRTREE_SAFE_RELEASE( return_value );
				throw e;
			}

			return return_value;
		}
};

//===========================================================================
// Register the emitter.
RegisterEmitter<NegateEmitter> g_NegateEmitterReg( "negate" );

}

// vim:sw=4:ts=4:cindent:noet
