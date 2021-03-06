/* firtree-kernel.c */

#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS

#include "firtree-sampler.h"
#include "firtree-vector.h"

#include <llvm-frontend/llvm-compiled-kernel.h>

#include <llvm/Linker.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Instructions.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Target/TargetData.h>

#include "internal/firtree-kernel-intl.hh"
#include "internal/firtree-sampler-intl.hh"
#include "internal/firtree-engine-intl.hh"

using namespace Firtree;
using namespace Firtree::LLVM;

/**
 * SECTION:firtree-kernel
 * @short_description: Compile Firtree kernels from source.
 * @include: firtree/firtree-kernel.h
 *
 * A FirtreeKernel encapsulates a kernel compiled from source code in the
 * Firtree kernel language.
 *
 * A kernel monitors the arguments connected to it and emits an ::argument-changed
 * signal when their value alters. This is most useful for updating GUIs. For
 * re-rendering pipelines, one is better off making use of the ::module-changed and
 * ::contents-changed signals. The former is emitted when the LLVM code underlying
 * the kernel has changed and is of most use to JIT-based render engines. The
 * ::contents-changed signal is emitted when an argument has changed which
 * necessitates a re-render but not a change in the LLVM module. This is different
 * to the ::argument-changed signal in that the Kernel also aggregates any 
 * ::contents-changed signals from samplers connected to it.
 */

/**
 * FirtreeKernelArgumentSpec:
 * @name_quark: The name of the argument.
 * @type: The type of the argument.
 * @is_static: Flag indicating if the argument is static.
 *
 * A structure which holds a record of a kernel argument.
 */

/**
 * FirtreeKernel:
 * @parent: The parent GObject.
 *
 * A FirtreeKernel encapsulates a kernel compiled from source code in the
 * Firtree kernel language.
 */

/**
 * FirtreeKernelPrivate:
 *
 * Internal private data of a FirtreeKernel instance.
 */

/**
 * FirtreeKernelTarget:
 * @FIRTREE_KERNEL_TARGET_RENDER: The kernel is a render kernel.
 * @FIRTREE_KERNEL_TARGET_REDUCE: The kernel is a reduce kernel.
 * @FIRTREE_KERNEL_TARGET_INVALID: The kernel's target has not yet been set.
 *
 * The 'target' of a particular kernel, i.e. if it is to be used for rendering
 * an image of reducing.
 */

enum {
	PROP_0,
	PROP_COMPILE_STATUS,
	LAST_PROP
};

enum {
	ARGUMENT_CHANGED,
	MODULE_CHANGED,
	CONTENTS_CHANGED,
	LAST_SIGNAL
};

static guint _firtree_kernel_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(FirtreeKernel, firtree_kernel, G_TYPE_OBJECT)
#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FIRTREE_TYPE_KERNEL, FirtreeKernelPrivate))


struct _FirtreeKernelPrivate {
	CompiledKernel *compiled_kernel;
	gboolean compile_status;
	KernelFunctionList::const_iterator preferred_function;

	GArray *arg_names;
	GData *arg_spec_list;
	GData *arg_value_list;
};

static GType
_firtree_kernel_type_specifier_to_gtype(KernelTypeSpecifier type_spec)
{
	static GType type_map[TySpecVoid + 1] = {
		G_TYPE_FLOAT,
		G_TYPE_INT,
		G_TYPE_BOOLEAN,
		FIRTREE_TYPE_VEC2,
		FIRTREE_TYPE_VEC3,
		FIRTREE_TYPE_VEC4,
		FIRTREE_TYPE_SAMPLER,
		FIRTREE_TYPE_VEC4,
		G_TYPE_NONE
	};

	if ((type_spec >= 0) && (type_spec <= TySpecVoid)) {
		return type_map[type_spec];
	}

	g_error("Unhandled type specifier: %i\n", type_spec);

	return G_TYPE_NONE;
}

static void
firtree_kernel_get_property(GObject * object, guint property_id,
			    GValue * value, GParamSpec * pspec)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(object);
	switch (property_id) {
	case PROP_COMPILE_STATUS:
		g_value_set_boolean(value, p->compile_status);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
	}
}

static void _firtree_kernel_reset_compile_status(FirtreeKernel * self)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(self);
	p->compile_status = FALSE;
	if (p->arg_names) {
		g_array_free(p->arg_names, TRUE);
		p->arg_names = NULL;
	}
	if (p->arg_spec_list) {
		g_datalist_clear(&(p->arg_spec_list));
	}
	if (p->arg_value_list) {
		g_datalist_clear(&(p->arg_value_list));
	}
}

static void firtree_kernel_dispose(GObject * object)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(object);

	_firtree_kernel_reset_compile_status(FIRTREE_KERNEL(object));

	if (p->compiled_kernel) {
		FIRTREE_SAFE_RELEASE(p->compiled_kernel);
		p->compiled_kernel = NULL;
	}

	G_OBJECT_CLASS(firtree_kernel_parent_class)->dispose(object);
}

static void firtree_kernel_class_init(FirtreeKernelClass * klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *param_spec;

	g_type_class_add_private(klass, sizeof(FirtreeKernelPrivate));

	object_class->get_property = firtree_kernel_get_property;
	object_class->dispose = firtree_kernel_dispose;

	klass->argument_changed = NULL;
	klass->module_changed = NULL;
	klass->contents_changed = NULL;

	param_spec = g_param_spec_boolean("compile-status",
					  "A flag indicating the success of the last compilation.",
					  "Get the compilation status.",
					  FALSE,
					  (GParamFlags) (G_PARAM_READABLE |
							 G_PARAM_STATIC_NAME |
							 G_PARAM_STATIC_NICK |
							 G_PARAM_STATIC_BLURB));
    /**
     * FirtreeKernel:compile-status:
     *
     * The return value from the last call to 
     * firtree_kernel_compile_from_source() or FALSE if this method
     * has not yet been called.
     */
	g_object_class_install_property(object_class,
					PROP_COMPILE_STATUS, param_spec);

    /**
     * FirtreeKernel::argument-changed:
     * @kernel: The kernel whose argument has changed.
     * @arg_name: A string indicating the argument name.
     *
     * The ::argument-changed signal is emitted each time a @kernel 's argument
     * is modified via firtree_kernel_set_argument_value().
     */
	_firtree_kernel_signals[ARGUMENT_CHANGED] =
	    g_signal_new("argument-changed",
			 G_OBJECT_CLASS_TYPE(klass),
			 (GSignalFlags) (G_SIGNAL_RUN_FIRST |
					 G_SIGNAL_NO_RECURSE |
					 G_SIGNAL_DETAILED),
			 G_STRUCT_OFFSET(FirtreeKernelClass, argument_changed),
			 NULL, NULL, g_cclosure_marshal_VOID__STRING,
			 G_TYPE_NONE, 1, G_TYPE_STRING);

    /**
     * FirtreeKernel::module-changed:
     * @kernel: The kernel whose module has changed.
     *
     * The ::module-changed signal is emitted each time the @kernel 's 
     * compiled module is changed via a call to 
     * firtree_kernel_compile_from_source(). In addition, failed compilations
     * cause this signal to be emitted.
     */
	_firtree_kernel_signals[MODULE_CHANGED] =
	    g_signal_new("module-changed",
			 G_OBJECT_CLASS_TYPE(klass),
			 (GSignalFlags) (G_SIGNAL_RUN_FIRST |
					 G_SIGNAL_NO_RECURSE),
			 G_STRUCT_OFFSET(FirtreeKernelClass, module_changed),
			 NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
			 0);
    /**
     * FirtreeKernel::contents-changed:
     * @kernel: The kernel whose argument has changed.
     *
     * The ::contents-changed signal is emitted each time the contents
     * of a kernel changes (i.e. when the contents of a dependent 
     * sampler changes).
     */
	_firtree_kernel_signals[CONTENTS_CHANGED] =
	    g_signal_new("contents-changed",
			 G_OBJECT_CLASS_TYPE(klass),
			 (GSignalFlags) (G_SIGNAL_RUN_FIRST |
					 G_SIGNAL_NO_RECURSE |
					 G_SIGNAL_DETAILED),
			 G_STRUCT_OFFSET(FirtreeKernelClass, contents_changed),
			 NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
			 0);
}

static void firtree_kernel_init(FirtreeKernel * self)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(self);

	/* initialise the compiled kernel to NULL (i.e. we have none) */
	p->compiled_kernel = NULL;
	p->compile_status = FALSE;

	p->arg_names = NULL;
	g_datalist_init(&(p->arg_spec_list));
	g_datalist_init(&(p->arg_value_list));

	_firtree_kernel_reset_compile_status(self);
}

/**
 * firtree_kernel_new:
 *
 * Create a firtree kernel. Call firtree_kernel_compile_from_source() to
 * actually compile a kernel.
 *
 * Returns: A new FirtreeKernel instance.
 */
FirtreeKernel *firtree_kernel_new(void)
{
	return (FirtreeKernel *) g_object_new(FIRTREE_TYPE_KERNEL, NULL);
}

static void _firtree_kernel_arg_spec_destroy_func(gpointer data)
{
	g_slice_free(FirtreeKernelArgumentSpec, data);
}

/**
 * firtree_kernel_compile_from_source:
 * @self: A FirtreeKernel instance.
 * @lines: An array of string containing the source lines. If @n_lines is
 * negative, this should be NULL terminated.
 * @n_lines: The number of source lines in @lines or negative if the array
 * @lines is NULL terminated.
 * @kernel_name: NULL or the name of a kernel function within the source.
 *
 * Attempts to compile the passed kernel. If compilation fails, FALSE is
 * returned. TRUE is returned if compilation succeeds. The 
 * firtree_kernel_get_compile_log() method may be used to retrieve any
 * error messages.
 *
 * If @kernel_name is non-NULL, it specifies the name of a kernel function
 * which should be used for this instance. If this kernel function is 
 * not defined in the source code passed, FALSE is returned. If @kernel_name
 * is NULL, the first kernel function defined is used. If no kernels are 
 * defined in the source, compilation is deemed unsuccessful.
 *
 * FIXME: Currently, failed compilation due to missing kernel functions is
 * not adequately reported.
 *
 * Note: The source lines are simply concatenated, no implicit newline
 * characters are inserted.
 *
 * Returns: a flag indicating whether the compilation was successful.
 */
gboolean
firtree_kernel_compile_from_source(FirtreeKernel * self,
				   gchar ** lines, gint n_lines,
				   gchar * kernel_name)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(self);

	/* Create a CompiledKernel object if necessary. */
	if (!p->compiled_kernel) {
		p->compiled_kernel = CompiledKernel::Create();
	}

	_firtree_kernel_reset_compile_status(self);

	p->compile_status = p->compiled_kernel->Compile(lines, n_lines);
	if (!p->compile_status) {
		firtree_kernel_module_changed(self);
		return p->compile_status;
	}

	const KernelFunctionList & functions = p->compiled_kernel->GetKernels();

	/* Do we have any functions defined? */
	if (functions.size() == 0) {
		p->compile_status = FALSE;
		firtree_kernel_module_changed(self);
		return p->compile_status;
	}

	/* By default, use the first kernel function */
	p->preferred_function = functions.begin();

	/* Do we have a preferred function */
	if (NULL != kernel_name) {
		for (;
		     (p->preferred_function != functions.end()) &&
		     (p->preferred_function->Name != kernel_name);
		     ++(p->preferred_function)) {
		}
		p->compile_status = (p->preferred_function != functions.end());
	}

	/* If the compile status is good, let's create a list of our arguments. */
	if (p->compile_status) {
		/* Create an array to store the argument name quarks. */
		g_assert(p->arg_names == NULL);
		p->arg_names = g_array_sized_new(TRUE, FALSE,
						 sizeof(GQuark),
						 p->preferred_function->
						 Parameters.size());

		KernelParameterList::const_iterator i;
		for (i = p->preferred_function->Parameters.begin();
		     i != p->preferred_function->Parameters.end(); ++i) {
			GQuark name_quark =
			    g_quark_from_string(i->Name.c_str());
			g_array_append_val(p->arg_names, name_quark);

			/* Create a FirtreeKernelArgumentSpec struct. */
			FirtreeKernelArgumentSpec *spec =
			    g_slice_new(FirtreeKernelArgumentSpec);

			spec->name_quark = name_quark;
			spec->type =
			    _firtree_kernel_type_specifier_to_gtype(i->Type);
			spec->is_static = i->IsStatic;

			/* add the spec to the list. */
			g_datalist_id_set_data_full(&p->arg_spec_list,
						    name_quark, spec,
						    _firtree_kernel_arg_spec_destroy_func);
		}
	}

	firtree_kernel_module_changed(self);

	return p->compile_status;
}

/**
 * firtree_kernel_get_compile_log:
 * @self: A FirtreeKernel instance.
 * @n_log_lines: If non-NULL, this is updated to contain the number of lines
 * in the log array.
 *
 * Retrieves the compilation log from the last call to 
 * firtree_kernel_compile_from_source(). If there is no such log, NULL
 * is returned.
 * 
 * Returns: an array of log lines terminated by NULL or NULL if there is
 * no log to return.
 */
gchar **firtree_kernel_get_compile_log(FirtreeKernel * self,
				       guint * n_log_lines)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(self);

	/* If we have no compiled kernel, we have no log. */
	if (!p->compiled_kernel) {
		return NULL;
	}

	return const_cast <
	    gchar ** >(p->compiled_kernel->GetCompileLog(n_log_lines));
}

/**
 * firtree_kernel_get_compile_status:
 * @self: A FirtreeKernel instance.
 *
 * Obtain the return value from the last call to 
 * firtree_kernel_compile_from_source() or FALSE if this method
 * has not yet been called.
 *
 * Returns: A gboolean indicating whether compilation was successful.
 */
gboolean firtree_kernel_get_compile_status(FirtreeKernel * self)
{
	gboolean ret_val = FALSE;
	g_object_get(self, "compile-status", &ret_val, NULL);
	return ret_val;
}

/**
 * firtree_kernel_list_arguments:
 * @self: A FirtreeKernel instance.
 * @n_arguments: NULL or a pointer to write the number of arguments.
 *
 * Obtain a pointer to a 0-terminated array of argument name quarks.
 *
 * Should there not be a compiled kernel, this method returns NULL.
 *
 * Returns: NULL or an array of GQuark-s.
 */
GQuark *firtree_kernel_list_arguments(FirtreeKernel * self, guint * n_arguments)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(self);
	if (p->compiled_kernel && p->compile_status) {
		if (n_arguments) {
			*n_arguments = p->arg_names->len;
		}
		return (GQuark *) p->arg_names->data;
	}
	return NULL;
}

/**
 * firtree_kernel_has_argument_named:
 * @self: A FirtreeKernel instance.
 * @arg_name: A string containing an argument name.
 *
 * Checks to see if ther kernel posesses an argument named @arg_name.
 * If no such argument is present, or the kernel is not compiled,
 * FALSE is returned. Otherwise, TRUE is returned.
 *
 * Returns: A boolean indicating the presence of an argument named 
 * @arg_name.
 */
gboolean
firtree_kernel_has_argument_named(FirtreeKernel * self, gchar * arg_name)
{
	/* We do it this way to avoid creating a GQuark for an
	 * argument name that may well not exist meaning we needlessly make
	 * a copy of the argument. This way is slower but more memory 
	 * efficient. */

	GQuark *args = firtree_kernel_list_arguments(self, NULL);
	while (*args) {
		if (0 == g_strcmp0(arg_name, g_quark_to_string(*args))) {
			return TRUE;
		}
		++args;
	}
	return FALSE;
}

/**
 * firtree_kernel_get_argument_spec:
 * @self: A FirtreeKernel instance.
 * @arg_name: A quark corresponding to an argument name.
 *
 * Return a pointer to a FirtreeKernelArgumentSpec containing details
 * on the argument with name @arg_name. If no such argument exists,
 * return NULL.
 *
 * Returns: A pointer to a FirtreeKernelArgumentSpec structure describing
 * the argument.
 */
FirtreeKernelArgumentSpec *firtree_kernel_get_argument_spec(FirtreeKernel *
							    self,
							    GQuark arg_name)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(self);
	return (FirtreeKernelArgumentSpec *)
	    g_datalist_id_get_data(&p->arg_spec_list, arg_name);
}

/**
 * firtree_kernel_get_argument_value:
 * @self: A FirtreeKernel instance.
 * @arg_name: A quark corresponding to an argument name.
 *
 * Retrieves the value of the argument referred to by @arg_name. If 
 * the kernel is not compiled, there is no argument named @arg_name, or
 * the argument is unset, NULL is returned.
 *
 * Returns: The value of the specified argument.
 */
GValue *firtree_kernel_get_argument_value(FirtreeKernel * self, GQuark arg_name)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(self);
	return (GValue *) g_datalist_id_get_data(&p->arg_value_list, arg_name);
}

/* Free a GValue structure allocated via g_slice_{alloc,new}, etc. */
/* Used in firtree_kernel_set_argument_value(). */
static void _firtree_kernel_value_destroy_func(gpointer value)
{
	g_value_unset((GValue *) value);
	g_slice_free(GValue, value);
}

static void
_firtree_kernel_sampler_module_changed_cb(FirtreeSampler * sampler,
					  FirtreeKernel * self)
{
	if (FIRTREE_IS_KERNEL(self)) {
		firtree_kernel_module_changed(self);
	}
}

static void
_firtree_kernel_sampler_contents_changed_cb(FirtreeSampler * sampler,
					    FirtreeKernel * self)
{
	if (FIRTREE_IS_KERNEL(self)) {
		firtree_kernel_contents_changed(self);
	}
}

/**
 * firtree_kernel_set_argument_value:
 * @self: A FirtreeKernel instance.
 * @arg_name: A quark corresponding to an argument name.
 * @value: A GValue containing the new value of the argument.
 *
 * Sets the value of @arg_name to @value. It is an error for the type of
 * @value to be a mis-match with that of @arg_name. Use 
 * firtree_kernel_get_argument_spec() to query what type is expected.
 *
 * If NULL is passed in @value, the argument is unset.
 *
 * Returns: A flag indicating if the argument was set.
 */
gboolean
firtree_kernel_set_argument_value(FirtreeKernel * self,
				  GQuark arg_name, GValue * value)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(self);
	FirtreeKernelArgumentSpec *spec =
	    firtree_kernel_get_argument_spec(self, arg_name);

	/* If we have no spec, we assume it is because this argument doesn't
	 * exist. */
	if (spec == NULL) {
		return FALSE;
	}

	/* If value is NULL, unset the value and return. */
	if (NULL == value) {
		g_datalist_id_set_data(&p->arg_value_list, arg_name, NULL);
		firtree_kernel_argument_changed(self, arg_name);
		return TRUE;
	}

	/* Check the type */
	if (spec->type != G_VALUE_TYPE(value)) {
		return FALSE;
	}

	/* Special case: for sampler arguments, we care about when their
	 * module changes since we link them in. Register handlers for
	 * this. */
	if (spec->type == FIRTREE_TYPE_SAMPLER) {
		/* register the new handler if necessary. */
		if (value) {
			FirtreeSampler *new_sampler =
			    FIRTREE_SAMPLER(g_value_get_object(value));
			g_signal_connect(new_sampler, "module-changed",
					 G_CALLBACK
					 (_firtree_kernel_sampler_module_changed_cb),
					 self);
			g_signal_connect(new_sampler, "contents-changed",
					 G_CALLBACK
					 (_firtree_kernel_sampler_contents_changed_cb),
					 self);
		}
	}

	/* Create a new GValue to store a copy of the passed value. */
	GValue *new_val = (GValue *) g_slice_alloc0(sizeof(GValue));
	g_value_init(new_val, spec->type);
	g_value_copy(value, new_val);

	/* Insert the GValue into the arg value list. */
	g_datalist_id_set_data_full(&p->arg_value_list, arg_name,
				    new_val,
				    _firtree_kernel_value_destroy_func);

	firtree_kernel_argument_changed(self, arg_name);

	return TRUE;
}

/**
 * firtree_kernel_argument_changed:
 * @self: A FirtreeKernel instance.
 * @arg_name: A quark corresponding to an argument name.
 *
 * Emit an ::argument-changed signal indicating that the value of
 * @arg_name has changed. The detail parameter is set to @arg_name.
 */
void firtree_kernel_argument_changed(FirtreeKernel * self, GQuark arg_name)
{
	g_return_if_fail(FIRTREE_IS_KERNEL(self));
	g_signal_emit(self, _firtree_kernel_signals[ARGUMENT_CHANGED], arg_name,
		      g_quark_to_string(arg_name));
}

/**
 * firtree_kernel_is_valid:
 * @self: A FirtreeKernel instance.
 *
 * Returns TRUE iff the kernel is considered 'valid'. A valid kernel is
 * one which is successfully compiled and has all of its arguments set.
 *
 * Returns: A gboolean indicating whether the kernel is valid.
 */
gboolean firtree_kernel_is_valid(FirtreeKernel * self)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(self);
	if (!p->compile_status) {
		return FALSE;
	}

	/* iterate through arguments */
	GQuark *args = firtree_kernel_list_arguments(self, NULL);
	if (!args) {
		return FALSE;
	}
	while (*args) {
		if (NULL == firtree_kernel_get_argument_value(self, *args)) {
			return FALSE;
		}
		++args;
	}

	return TRUE;
}

/**
 * firtree_kernel_module_changed:
 * @self: A FirtreeKernel instance.
 *
 * Emit an ::modules-changed signal indicating that the underlying compiled
 * module of @self has changed.
 */
void firtree_kernel_module_changed(FirtreeKernel * self)
{
	g_return_if_fail(FIRTREE_IS_KERNEL(self));
	g_signal_emit(self, _firtree_kernel_signals[MODULE_CHANGED], 0);
}

/**
 * firtree_kernel_contents_changed:
 * @self: A FirtreeKernel instance.
 *
 * @arg_name has changed. The detail parameter is set to @arg_name.
 */
void firtree_kernel_contents_changed(FirtreeKernel * self)
{
	g_return_if_fail(FIRTREE_IS_KERNEL(self));
	g_signal_emit(self, _firtree_kernel_signals[CONTENTS_CHANGED], 0);
}

/**
 * firtree_kernel_get_return_type:
 * @self: A FirtreeKernel instance.
 *
 * Return the return value type of the kernel. Most kernels should have vec4 or
 * void return types indicating if they are an image processing or a map/reduce
 * kernel. If there is no kernel associated with @self, this return G_TYPE_NONE.
 *
 * Returns: A GType.
 */
GType firtree_kernel_get_return_type(FirtreeKernel * self)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(self);

	/* If we have no compiled kernel, we have no return type. */
	if (!p->compiled_kernel || !p->compile_status) {
		return G_TYPE_NONE;
	}

	return _firtree_kernel_type_specifier_to_gtype(p->preferred_function->
						       ReturnType);
}

/**
 * firtree_kernel_get_target:
 * @self:  A FirtreeKernel instance.
 *
 * A kernel can be targetted to particular tasks. In Firtree currently there are
 * two types of kernels, rendering and reducing kernels. Rendering kernels output
 * exactly one vec4 for every destination co-ordinate. Reducing kernels may
 * output one or none vec4s for each destination co-ordinate.
 *
 * Returns: A member of the FirtreeKernelTarget enum indicating the kernel's target.
 */
FirtreeKernelTarget firtree_kernel_get_target(FirtreeKernel * self)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(self);

	if (!p->compiled_kernel || !p->compile_status) {
		return FIRTREE_KERNEL_TARGET_INVALID;
	}

	if (p->preferred_function->Target == LLVM::KernelFunction::Render) {
		return FIRTREE_KERNEL_TARGET_RENDER;
	} else if (p->preferred_function->Target ==
		   LLVM::KernelFunction::Reduce) {
		return FIRTREE_KERNEL_TARGET_REDUCE;
	}

	return FIRTREE_KERNEL_TARGET_INVALID;
}

llvm::Function * firtree_kernel_get_function(FirtreeKernel * self)
{
	FirtreeKernelPrivate *p = GET_PRIVATE(self);

	/* If we have no compiled kernel, we have no module. */
	if (!p->compiled_kernel || !p->compile_status) {
		return NULL;
	}

	return p->preferred_function->Function;
}

static void
firtree_kernel_implement_transform_function(FirtreeKernel * self,
					    llvm::Function * transform_func)
{
	llvm::BasicBlock * bb =
	    llvm::BasicBlock::Create(FIRTREE_LLVM_CONTEXT "entry", transform_func);

	/* extract the input arguments. */
	llvm::Function::const_arg_iterator argit = transform_func->arg_begin();
	const llvm::Value * input_id = argit;
	++argit;
	const llvm::Value * input_vector = argit;

	/* construct a basic block that 'implements' an identity transform
	 * (the default). */
	llvm::BasicBlock * id_bb =
	    llvm::BasicBlock::Create(FIRTREE_LLVM_CONTEXT "identity", transform_func);
	llvm::ReturnInst::Create(FIRTREE_LLVM_CONTEXT const_cast < llvm::Value * >(input_vector),
				 id_bb);

	/* Now make a switch statement for each sampler argument. */

	guint n_arguments = 0;
	GQuark *arg_list = firtree_kernel_list_arguments(self, &n_arguments);

	llvm::SwitchInst * sampler_switch =
	    llvm::SwitchInst::Create(const_cast < llvm::Value * >(input_id),
				     id_bb, 0, bb);
	for (guint i = 0; i < n_arguments; ++i) {
		GQuark arg_quark = arg_list[i];
		FirtreeKernelArgumentSpec *spec =
		    firtree_kernel_get_argument_spec(self, arg_quark);
		if (spec->type == FIRTREE_TYPE_SAMPLER) {
			FirtreeSampler *sampler = (FirtreeSampler *)
			    g_value_get_object(firtree_kernel_get_argument_value
					       (self, arg_quark));
			g_assert(sampler);

			FirtreeAffineTransform *transform =
			    firtree_sampler_get_transform(sampler);

			if (firtree_affine_transform_is_identity(transform)) {
				sampler_switch->
				    addCase(llvm::ConstantInt::
					    get(FIRTREE_LLVM_INT32_TY, arg_quark,
						false), id_bb);
			} else {
				g_error
				    ("Non-identity transforms not yet implemented.");
			}

			g_object_unref(transform);
		}
	}

}

static void
firtree_kernel_implement_sample_function(FirtreeKernel * self,
					 llvm::Function * sample_func,
					 GData ** sampler_func_map)
{
	llvm::BasicBlock * bb = llvm::BasicBlock::Create(FIRTREE_LLVM_CONTEXT "entry", sample_func);

	llvm::Function::arg_iterator args = sample_func->arg_begin();

	llvm::Value * sampler_id = args;
	++args;

	std::vector < llvm::Value * >remaining_args;
	for (; args != sample_func->arg_end(); ++args) {
		remaining_args.push_back(args);
	}

	/* default return value. */
	llvm::BasicBlock * default_bb = llvm::BasicBlock::Create(FIRTREE_LLVM_CONTEXT "default_id",
								 sample_func);
	llvm::ReturnInst::Create(FIRTREE_LLVM_CONTEXT llvm::ConstantAggregateZero::
				 get(llvm::VectorType::
				     get(FIRTREE_LLVM_FLOAT_TY, 4)), default_bb);

	guint n_arguments = 0;
	GQuark *arg_list = firtree_kernel_list_arguments(self, &n_arguments);

	/* For each possible input id, add a case to a switch which calls
	 * the appropriate sampler. */
	llvm::SwitchInst * sampler_switch =
	    llvm::SwitchInst::Create(sampler_id, default_bb, 0, bb);
	for (guint i = 0; i < n_arguments; ++i) {
		GQuark arg_quark = arg_list[i];
		FirtreeKernelArgumentSpec *spec =
		    firtree_kernel_get_argument_spec(self, arg_quark);
		if (spec->type == FIRTREE_TYPE_SAMPLER) {
			FirtreeSampler *sampler = (FirtreeSampler *)
			    g_value_get_object(firtree_kernel_get_argument_value
					       (self, arg_quark));
			g_assert(sampler);

			llvm::Function * sampler_f = (llvm::Function *)
			    g_datalist_id_get_data(sampler_func_map, arg_quark);
			g_assert(sampler_f);

			llvm::BasicBlock * sample_bb =
			    llvm::BasicBlock::Create(FIRTREE_LLVM_CONTEXT "id", sample_func);

			llvm::Value * ret_val =
			    llvm::CallInst::Create(sampler_f,
						   remaining_args.begin(),
						   remaining_args.end(), "rv",
						   sample_bb);

			llvm::ReturnInst::Create(FIRTREE_LLVM_CONTEXT ret_val, sample_bb);

			sampler_switch->
			    addCase(llvm::ConstantInt::
				    get(FIRTREE_LLVM_INT32_TY, arg_quark, false),
				    sample_bb);
		}
	}
}

static void _firtree_kernel_aggressive_inline(llvm::Function * f)
{
	llvm::Module * m = f->getParent();

	llvm::PassManager PM;
	PM.add(new llvm::TargetData(m));

	/* Firstly internalise all the functions apart from our sampler
	 * function. */
	std::vector < const char *>export_list;
#if FIRTREE_LLVM_AT_LEAST_2_6
	export_list.push_back(f->getName().str().c_str());
#else
	export_list.push_back(f->getName().c_str());
#endif
	PM.add(llvm::createInternalizePass(export_list));

	/* Now inline functions. */
	PM.add(llvm::createFunctionInliningPass(32768));

	/* Agressively remove dead code. */
	PM.add(llvm::createAggressiveDCEPass());

	/* Now do some compile optimisations. */
	PM.add(llvm::createStripDeadPrototypesPass());

	PM.add(llvm::createIPConstantPropagationPass());
	PM.add(llvm::createInstructionCombiningPass());
	PM.add(llvm::createCFGSimplificationPass());

	PM.add(llvm::createCondPropagationPass());
	PM.add(llvm::createReassociatePass());

	PM.run(*m);
}

llvm::Function * firtree_kernel_create_overall_function(FirtreeKernel * self)
{
	if (!firtree_kernel_is_valid(self)) {
		g_debug
		    ("firtree_kernel_is_valid() returns false for associated kernel.\n");
		return NULL;
	}

	/* if we get here, we need to create our function. */

	/* get our associate function. */
	llvm::Function * kernel_func = firtree_kernel_get_function(self);
	if (!kernel_func) {
		g_debug("No kernel function.\n");
		return NULL;
	}

	/* Link the kernel function into a new module. */
#if FIRTREE_LLVM_AT_LEAST_2_6
	llvm::Linker * linker = new llvm::Linker("linked_kernel", "module",
			llvm::getGlobalContext());
#else
	llvm::Linker * linker = new llvm::Linker("linked_kernel", "module");
#endif
	std::string err_str;
	llvm::Module * new_mod = llvm::CloneModule(kernel_func->getParent());
	if (linker->LinkInModule(new_mod, &err_str)) {
		g_error("Error linking function: %s\n", err_str.c_str());
	}
	delete new_mod;

	llvm::Value * new_kernel_func =
	    linker->getModule()->getFunction(kernel_func->getName());
	g_assert(new_kernel_func);

	guint n_arguments = 0;
	GQuark *arg_list = firtree_kernel_list_arguments(self, &n_arguments);

	/* A list which maps sampler argument quarks to their associated function
	 * in our new module. */
	GData *sampler_function_list = NULL;
	g_datalist_init(&sampler_function_list);

	/* For each sampler parameter, get the associated sampler function
	 * and link that in too. */
	for (guint arg_i = 0; arg_i < n_arguments; ++arg_i) {
		GQuark arg_quark = arg_list[arg_i];
		FirtreeKernelArgumentSpec *arg_spec =
		    firtree_kernel_get_argument_spec(self, arg_quark);
		g_assert(arg_spec);

		if (arg_spec->type == FIRTREE_TYPE_SAMPLER) {
			GValue *val =
			    firtree_kernel_get_argument_value(self, arg_quark);
			FirtreeSampler *sampler = NULL;
			llvm::Function * sampler_f = NULL;

			if (val) {
				sampler =
				    (FirtreeSampler *) g_value_get_object(val);
			}

			if (sampler) {
				sampler_f =
				    firtree_sampler_get_sample_function
				    (sampler);
			}

			if (sampler_f) {
				llvm::Module * new_mod =
				    llvm::CloneModule(sampler_f->getParent());
				if (linker->LinkInModule(new_mod, &err_str)) {
					g_error("Error linking function: %s\n",
						err_str.c_str());
				}
				delete new_mod;

				llvm::Function * new_f =
				    linker->getModule()->getFunction(sampler_f->
								     getName());
				g_assert(new_f);

				/* record the new function in the sampler function list. */
				g_datalist_id_set_data(&sampler_function_list,
						       arg_quark, new_f);
			} else {
				/* bail from function, connected sampler is invalid. */
				delete linker;
				g_datalist_clear(&sampler_function_list);
				return NULL;
			}
		}
	}

	/* We've finished all our linking, release the linker. */
	llvm::Module * m = linker->releaseModule();
	delete linker;

	/* If we have any calls to sample(), replace them with calls to the
	 * apropriate sampler function. We do this by implementing the @sample_sv2
	 * function. */
	llvm::Function * sample_sv2_f = m->getFunction("sample_sv2");
	if (sample_sv2_f) {
		firtree_kernel_implement_sample_function(self, sample_sv2_f,
							 &sampler_function_list);
	}

	/* Similarly, we implement the samplerTransform_sv2 function. */
	llvm::Function * trans_f = m->getFunction("samplerTransform_sv2");
	if (trans_f) {
		firtree_kernel_implement_transform_function(self, trans_f);
	}

	g_datalist_clear(&sampler_function_list);

	llvm::Function * f = NULL;

	/* create the function */
	switch (firtree_kernel_get_target(self)) {
	case FIRTREE_KERNEL_TARGET_RENDER:
		f = firtree_engine_create_sample_function_prototype(m);
		break;
	case FIRTREE_KERNEL_TARGET_REDUCE:
		f = firtree_engine_create_reduce_function_prototype(m);
		break;
	default:
		g_error("Unknown target");
	}

	g_assert(f);

	llvm::BasicBlock * bb = llvm::BasicBlock::Create(FIRTREE_LLVM_CONTEXT "entry", f);

	std::vector < llvm::Value * >arguments;

	llvm::Function::arg_iterator ai = f->arg_begin();
	while (ai != f->arg_end()) {
		arguments.push_back(ai);
		++ai;
	}

	for (guint arg_i = 0; arg_i < n_arguments; ++arg_i) {
		GQuark arg_quark = arg_list[arg_i];
		FirtreeKernelArgumentSpec *arg_spec =
		    firtree_kernel_get_argument_spec(self, arg_quark);
		g_assert(arg_spec);

		if (!arg_spec->is_static) {
			g_error
			    ("FIXME: Non-static argument support not yet written.");
		} else {
			GValue *kernel_arg =
			    firtree_kernel_get_argument_value(self,
							      arg_quark);
			g_assert(kernel_arg);

			/* Can't use a switch here because the FIRTREE_TYPE_SAMPLER macro
			 * doesn't expand to a constant. */
			if (arg_spec->type == FIRTREE_TYPE_SAMPLER) {
				llvm::Value * arg_quark_val =
				    llvm::ConstantInt::get(FIRTREE_LLVM_INT32_TY,
							   arg_quark, false);
				arguments.push_back(arg_quark_val);
			} else {
				arguments.
				    push_back
				    (firtree_engine_get_constant_for_kernel_argument
				     (kernel_arg));
			}
		}
	}

	llvm::Value * function_call =
	    llvm::CallInst::Create(new_kernel_func, arguments.begin(),
				   arguments.end(), "", bb);

	switch (firtree_kernel_get_target(self)) {
	case FIRTREE_KERNEL_TARGET_RENDER:
		llvm::ReturnInst::Create(FIRTREE_LLVM_CONTEXT function_call, bb);
		break;
	case FIRTREE_KERNEL_TARGET_REDUCE:
		llvm::ReturnInst::Create(FIRTREE_LLVM_CONTEXT NULL, bb);
		break;
	default:
		g_error("Unknown target");
	}

	/* run an agressive inlining pass over the function */
	_firtree_kernel_aggressive_inline(f);
	g_assert(m->getFunction("sample_sv2") == NULL);

	return f;
}

/* vim:sw=8:ts=8:tw=78:noet:cindent
 */
