/* firtree-sampler.cc */

/* Firtree - A generic image processing library
 * Copyright (C) 2009 Rich Wareham <richwareham@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.    See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA    02110-1301, USA
 */

#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS

#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Instructions.h>
#include <llvm/Constants.h>
#include <llvm/Linker.h>

#include <common/uuid.h>

#include "internal/firtree-sampler-intl.hh"

/**
 * SECTION:firtree-sampler
 * @short_description: Encapsulate the various image sources Firtree can use.
 * @include: firtree/firtree-sampler.h
 *
 * A FirtreeSampler is the object assigned to sampler arguments on Firtree
 * kernels to specify image sources and a pixel pipeline.
 *
 * There are a number of sub-classes of FirtreeSampler who know how to sample from
 * image buffers, textures, etc.
 *
 * A sampler has associated a set of extents which define the rectangle outside of
 * which the sampler is guaranteed to return a transparent pixel. The extents are
 * defined in *sampler co-ordinates*.
 *
 * The sampler co-ordinate is the result of applying the sampler transform to a 
 * world co-ordinate.
 *
 * A sampler should emit a signal when the extents change and when the transform 
 * changes.
 *
 * In addition to the signals described above, a sampler will emit a ::module-changed
 * signal when the internal LLVM function which describes it has changed. It will
 * emit a ::contents-changed signal when the contents of the sampler have changed and
 * should be re-rendered by interested parties.
 */

/**
 * FirtreeSampler:
 * @parent: The GObject parent of FirtreeSampler.
 *
 * A FirtreeSampler is the object assigned to sampler arguments on Firtree
 * kernels to specify image sources and a pixel pipeline.
 */

/**
 * FirtreeSamplerPrivate:
 *
 * Private data for a FirtreeSampler instance.
 */

/**
 * FirtreeSamplerIntlVTable:
 *
 * Internal structure holding virtual function pointers for the 
 * internal API.
 */

enum {
	CONTENTS_CHANGED,
	MODULE_CHANGED,
	EXTENTS_CHANGED,
	TRANSFORM_CHANGED,
	LAST_SIGNAL
};

static guint _firtree_sampler_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(FirtreeSampler, firtree_sampler, G_TYPE_OBJECT)
#define GET_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), FIRTREE_TYPE_SAMPLER, FirtreeSamplerPrivate))

struct _FirtreeSamplerPrivate {
	llvm::Function * transform_function;
};

gboolean
firtree_sampler_get_param_default(FirtreeSampler * self, guint param,
				  gpointer dest, guint dest_size);

llvm::Function *
firtree_sampler_get_sample_function_default(FirtreeSampler * self);

FirtreeVec4 firtree_sampler_get_extent_default(FirtreeSampler * self);

gboolean firtree_sampler_lock_default(FirtreeSampler * self);

void firtree_sampler_unlock_default(FirtreeSampler * self);

static void _firtree_sampler_invalidate_llvm_cache(FirtreeSampler * self)
{
	FirtreeSamplerPrivate *p = GET_PRIVATE(self);
	if (p && p->transform_function) {
		delete p->transform_function->getParent();
		p->transform_function = NULL;
	}
}

static void firtree_sampler_dispose(GObject * object)
{
	G_OBJECT_CLASS(firtree_sampler_parent_class)->dispose(object);

	_firtree_sampler_invalidate_llvm_cache((FirtreeSampler *) object);
}

static FirtreeSamplerIntlVTable _firtree_sampler_class_vtable;

static void firtree_sampler_class_init(FirtreeSamplerClass * klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	g_type_class_add_private(klass, sizeof(FirtreeSamplerPrivate));

	object_class->dispose = firtree_sampler_dispose;

	klass->contents_changed = NULL;
	klass->module_changed = NULL;
	klass->extents_changed = NULL;
	klass->transform_changed = NULL;

	klass->get_extent = firtree_sampler_get_extent_default;
	klass->lock = firtree_sampler_lock_default;
	klass->unlock = firtree_sampler_unlock_default;

	klass->intl_vtable = &_firtree_sampler_class_vtable;
	klass->intl_vtable->get_param = firtree_sampler_get_param_default;
	klass->intl_vtable->get_sample_function =
	    firtree_sampler_get_sample_function_default;

    /**
     * FirtreeSampler::contents-changed:
     * @sampler: The sampler whose contents has changed.
     *
     * Emitted by a sampler when the contents this sampler represents
     * has changed.
     */
	_firtree_sampler_signals[CONTENTS_CHANGED] =
	    g_signal_new("contents-changed",
			 G_OBJECT_CLASS_TYPE(klass),
			 (GSignalFlags) (G_SIGNAL_RUN_FIRST |
					 G_SIGNAL_NO_RECURSE),
			 G_STRUCT_OFFSET(FirtreeSamplerClass, contents_changed),
			 NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
			 0);

    /**
     * FirtreeSampler::module-changed:
     * @sampler: The sampler whose module has changed.
     *
     * Used by the internal API. Emitted when the LLVM IR of this sampler
     * has changed.
     */
	_firtree_sampler_signals[MODULE_CHANGED] =
	    g_signal_new("module-changed",
			 G_OBJECT_CLASS_TYPE(klass),
			 (GSignalFlags) (G_SIGNAL_RUN_FIRST |
					 G_SIGNAL_NO_RECURSE),
			 G_STRUCT_OFFSET(FirtreeSamplerClass, module_changed),
			 NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
			 0);

    /**
     * FirtreeSampler::extents-changed:
     * @sampler: The sampler whose extents have changed.
     *
     * Emitted by a sampler when its extents have changed. 
     */
	_firtree_sampler_signals[EXTENTS_CHANGED] =
	    g_signal_new("extents-changed",
			 G_OBJECT_CLASS_TYPE(klass),
			 (GSignalFlags) (G_SIGNAL_RUN_FIRST |
					 G_SIGNAL_NO_RECURSE),
			 G_STRUCT_OFFSET(FirtreeSamplerClass, extents_changed),
			 NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
			 0);

    /**
     * FirtreeSampler::transform-changed:
     * @sampler: The sampler whose transform has changed.
     *
     * Emitted by a sampler when the sampler's transform has changed.
     */
	_firtree_sampler_signals[TRANSFORM_CHANGED] =
	    g_signal_new("transform-changed",
			 G_OBJECT_CLASS_TYPE(klass),
			 (GSignalFlags) (G_SIGNAL_RUN_FIRST |
					 G_SIGNAL_NO_RECURSE),
			 G_STRUCT_OFFSET(FirtreeSamplerClass,
					 transform_changed), NULL, NULL,
			 g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static void firtree_sampler_init(FirtreeSampler * self)
{
	FirtreeSamplerPrivate *p = GET_PRIVATE(self);

	p->transform_function = NULL;
}

/**
 * firtree_sampler_new:
 *
 * Construct a NULL sampler.
 *
 * Returns: A newly instantiated FirtreeSampler.
 */
FirtreeSampler *firtree_sampler_new(void)
{
	return (FirtreeSampler *) g_object_new(FIRTREE_TYPE_SAMPLER, NULL);
}

FirtreeVec4 firtree_sampler_get_extent_default(FirtreeSampler * self)
{
	/* an 'infinite' extent. */
	FirtreeVec4 extent =
	    { -0.5 * G_MAXFLOAT, -0.5 * G_MAXFLOAT, G_MAXFLOAT, G_MAXFLOAT };
	return extent;
}

/**
 * firtree_sampler_get_extent:
 * @self: An instantiated FirtreeSampler object.
 *
 * Find the extent of the sampler as a (minx, miny, width, height)
 * 4-vector.
 *
 * Returns: A 4-vector with the sampler extent.
 */
FirtreeVec4 firtree_sampler_get_extent(FirtreeSampler * self)
{
	return FIRTREE_SAMPLER_GET_CLASS(self)->get_extent(self);
}

/**
 * firtree_sampler_get_transform:
 * @self: An instantiated FirtreeSampler object.
 *
 * Retrieve the transform which should be use to map points from the
 * output space of the sampler to the sampler space. This ultimately
 * is what ends up being implemented by samplerTransform().
 *
 * Callers should release the returned transform via g_object_unref()
 * when finished with it.
 *
 * Returns: A referenced FirtreeAffineTransform object.
 */
FirtreeAffineTransform *firtree_sampler_get_transform(FirtreeSampler * self)
{
	return firtree_affine_transform_new();
}

/**
 * firtree_sampler_contents_changed:
 * @self: A FirtreeSampler object.
 *
 * Emit the ::contents-changed signal.
 */
void firtree_sampler_contents_changed(FirtreeSampler * self)
{
	g_return_if_fail(FIRTREE_IS_SAMPLER(self));
	g_signal_emit(self, _firtree_sampler_signals[CONTENTS_CHANGED], 0);
}

/**
 * firtree_sampler_module_changed:
 * @self: A FirtreeSampler object.
 *
 * Emit the ::module-changed signal.
 */
void firtree_sampler_module_changed(FirtreeSampler * self)
{
	g_return_if_fail(FIRTREE_IS_SAMPLER(self));
	g_signal_emit(self, _firtree_sampler_signals[MODULE_CHANGED], 0);
}

/**
 * firtree_sampler_extents_changed:
 * @self: A FirtreeSampler object.
 *
 * Emit the ::extents-changed signal.
 */
void firtree_sampler_extents_changed(FirtreeSampler * self)
{
	g_return_if_fail(FIRTREE_IS_SAMPLER(self));
	g_signal_emit(self, _firtree_sampler_signals[EXTENTS_CHANGED], 0);
}

/**
 * firtree_sampler_transform_changed:
 * @self: A FirtreeSampler object.
 *
 * Emit the ::transform-changed signal.
 */
void firtree_sampler_transform_changed(FirtreeSampler * self)
{
	g_return_if_fail(FIRTREE_IS_SAMPLER(self));
	g_signal_emit(self, _firtree_sampler_signals[TRANSFORM_CHANGED], 0);
}

gboolean
firtree_sampler_get_param_default(FirtreeSampler * self, guint param,
				  gpointer dest, guint dest_size)
{
	return FALSE;
}

gboolean
firtree_sampler_get_param(FirtreeSampler * self, guint param,
			  gpointer dest, guint dest_size)
{
	return FIRTREE_SAMPLER_GET_CLASS(self)->intl_vtable->get_param(self,
								       param,
								       dest,
								       dest_size);
}

gboolean firtree_sampler_lock_default(FirtreeSampler * self)
{
	return TRUE;
}

gboolean firtree_sampler_lock(FirtreeSampler * self)
{
	return FIRTREE_SAMPLER_GET_CLASS(self)->lock(self);
}

void firtree_sampler_unlock_default(FirtreeSampler * self)
{
	/* nop */
}

void firtree_sampler_unlock(FirtreeSampler * self)
{
	FIRTREE_SAMPLER_GET_CLASS(self)->unlock(self);
}

llvm::Function * firtree_sampler_get_sample_function(FirtreeSampler * self)
{
	return FIRTREE_SAMPLER_GET_CLASS(self)->intl_vtable->
	    get_sample_function(self);
}

llvm::Function *
firtree_sampler_get_sample_function_default(FirtreeSampler * self)
{
	return NULL;
}

#if 0
llvm::Function * firtree_sampler_get_transform_function(FirtreeSampler * self)
{
	FirtreeSamplerPrivate *p = GET_PRIVATE(self);
	if (p->transform_function) {
		return p->transform_function;
	}

	llvm::Module * m = new llvm::Module("sampler");

	/* work out the function name. */
	std::string func_name("sampler_transform_");
	gchar uuid[37];
	generate_random_uuid(uuid, '_');
	func_name += uuid;

	std::vector < const llvm::Type * >params;
	params.push_back(llvm::VectorType::get(llvm::Type::FloatTy, 2));
	llvm::FunctionType * ft = llvm::FunctionType::get(llvm::VectorType::get(llvm::Type::FloatTy, 4),	/* ret. type */
							  params, false);

	llvm::Function * f =
	    llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
				   func_name.c_str(), m);

	llvm::BasicBlock * bb = llvm::BasicBlock::Create("entry", f);

	llvm::ReturnInst::Create(f->arg_begin(), bb);

	p->transform_function = f;
	return p->transform_function;
}
#endif

/* vim:sw=8:ts=8:tw=78:noet:cindent
 */
