/* firtree-kernel-sampler.cc */

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

#include "internal/firtree-engine-intl.hh"
#include "internal/firtree-kernel-intl.hh"
#include "internal/firtree-sampler-intl.hh"

#include "firtree-kernel-sampler.h"

G_DEFINE_TYPE (FirtreeKernelSampler, firtree_kernel_sampler, FIRTREE_TYPE_SAMPLER)

#define GET_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), FIRTREE_TYPE_KERNEL_SAMPLER, FirtreeKernelSamplerPrivate))

typedef struct _FirtreeKernelSamplerPrivate FirtreeKernelSamplerPrivate;

struct _FirtreeKernelSamplerPrivate {
    FirtreeKernel*  kernel;
    gulong          kernel_mod_ch_handler_id;
    gulong          kernel_arg_ch_handler_id;
    gulong          kernel_cont_ch_handler_id;
    llvm::Function* cached_function;
};

gboolean
firtree_kernel_sampler_get_param(FirtreeSampler* self, guint param, 
        gpointer dest, guint dest_size);

llvm::Function*
firtree_kernel_sampler_get_sample_function(FirtreeSampler* self);

/* invalidate (and release) any cached LLVM modules/functions. This
 * will cause them to be re-generated when ..._get_function() is next
 * called. */
static void
_firtree_kernel_sampler_invalidate_llvm_cache(FirtreeKernelSampler* self)
{
    FirtreeKernelSamplerPrivate* p = GET_PRIVATE(self);
    if(p && p->cached_function) {
        delete p->cached_function->getParent();
        p->cached_function = NULL;
    }
    firtree_sampler_module_changed(FIRTREE_SAMPLER(self));
}

static void
firtree_kernel_sampler_get_property (GObject *object, guint property_id,
        GValue *value, GParamSpec *pspec)
{
    switch (property_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
firtree_kernel_sampler_set_property (GObject *object, guint property_id,
        const GValue *value, GParamSpec *pspec)
{
    switch (property_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
firtree_kernel_sampler_dispose (GObject *object)
{
    G_OBJECT_CLASS (firtree_kernel_sampler_parent_class)->dispose (object);

    /* drop any references to any kernel we might have. */
    firtree_kernel_sampler_set_kernel((FirtreeKernelSampler*)object, NULL);

    /* dispose of any LLVM modules we might have. */
    _firtree_kernel_sampler_invalidate_llvm_cache((FirtreeKernelSampler*)object);
}

static void
firtree_kernel_sampler_finalize (GObject *object)
{
    G_OBJECT_CLASS (firtree_kernel_sampler_parent_class)->finalize (object);
}

static FirtreeSamplerIntlVTable _firtree_kernel_sampler_class_vtable;

static void
firtree_kernel_sampler_class_init (FirtreeKernelSamplerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (FirtreeKernelSamplerPrivate));

    object_class->get_property = firtree_kernel_sampler_get_property;
    object_class->set_property = firtree_kernel_sampler_set_property;
    object_class->dispose = firtree_kernel_sampler_dispose;
    object_class->finalize = firtree_kernel_sampler_finalize;

    /* override the sampler virtual functions with our own */
    FirtreeSamplerClass* sampler_class = FIRTREE_SAMPLER_CLASS(klass);

    sampler_class->intl_vtable = &_firtree_kernel_sampler_class_vtable;
    sampler_class->intl_vtable->get_param = firtree_kernel_sampler_get_param;
    sampler_class->intl_vtable->get_sample_function = 
        firtree_kernel_sampler_get_sample_function;
}

static void
firtree_kernel_sampler_init (FirtreeKernelSampler *self)
{
    FirtreeKernelSamplerPrivate* p = GET_PRIVATE(self);
    p->kernel = NULL;
    p->cached_function = NULL;
}

FirtreeKernelSampler*
firtree_kernel_sampler_new (void)
{
    return (FirtreeKernelSampler*)
        g_object_new (FIRTREE_TYPE_KERNEL_SAMPLER, NULL);
}

static void
_firteee_kernel_sampler_module_changed_cb(FirtreeKernel* kernel,
        FirtreeKernelSampler* self)
{
    _firtree_kernel_sampler_invalidate_llvm_cache(self);
}

static void
_firteee_kernel_sampler_argument_changed_cb(FirtreeKernel* kernel,
        const gchar* arg_name, FirtreeKernelSampler* self)
{
    if(firtree_kernel_get_argument_spec(kernel, 
                g_quark_from_string(arg_name))->is_static) 
    {
        /* If the argument is static, we need to force a new function. */
        _firtree_kernel_sampler_invalidate_llvm_cache(self);
    }
    firtree_sampler_contents_changed(FIRTREE_SAMPLER(self));
}

static void
_firteee_kernel_sampler_contents_changed_cb(FirtreeKernel* kernel,
        FirtreeKernelSampler* self)
{
    firtree_sampler_contents_changed(FIRTREE_SAMPLER(self));
}

void
firtree_kernel_sampler_set_kernel (FirtreeKernelSampler* self,
        FirtreeKernel* kernel)
{
    FirtreeKernelSamplerPrivate* p = GET_PRIVATE(self);
    /* unref any kernel we already have. */

    if(p->kernel) {
        g_signal_handler_disconnect(p->kernel, p->kernel_mod_ch_handler_id);
        g_signal_handler_disconnect(p->kernel, p->kernel_arg_ch_handler_id);
        g_signal_handler_disconnect(p->kernel, p->kernel_cont_ch_handler_id);
        g_object_unref(p->kernel);
        p->kernel = NULL;
    }

    if(kernel) {
        g_assert(FIRTREE_IS_KERNEL(kernel));

        /* Can only sample from render kernels. */
        if(firtree_kernel_get_target(kernel) != FIRTREE_KERNEL_TARGET_RENDER) {
            g_warning("Ignoring attempt to set KernelSampler's kernel to non-render kernel.");
            return;
        }

        g_object_ref(kernel);
        p->kernel = kernel;
        
        /* Connect kernel changed signals. */
        p->kernel_mod_ch_handler_id = g_signal_connect(kernel, "module-changed", 
                G_CALLBACK(_firteee_kernel_sampler_module_changed_cb),
                self);
        p->kernel_arg_ch_handler_id = g_signal_connect(kernel, "argument-changed", 
                G_CALLBACK(_firteee_kernel_sampler_argument_changed_cb),
                self);
        p->kernel_cont_ch_handler_id = g_signal_connect(kernel, "contents-changed", 
                G_CALLBACK(_firteee_kernel_sampler_contents_changed_cb),
                self);
    }

    _firtree_kernel_sampler_invalidate_llvm_cache(self);
}

FirtreeKernel*
firtree_kernel_sampler_get_kernel (FirtreeKernelSampler* self)
{
    FirtreeKernelSamplerPrivate* p = GET_PRIVATE(self);
    return p->kernel;
}

gboolean
firtree_kernel_sampler_get_param(FirtreeSampler* self, guint param, 
        gpointer dest, guint dest_size)
{
    return FALSE;
}

llvm::Function*
firtree_kernel_sampler_get_sample_function(FirtreeSampler* self)
{
    FirtreeKernelSamplerPrivate* p = GET_PRIVATE(self);
    if(p->cached_function) {
        return p->cached_function;
    }

    if(!p->kernel) {
        g_debug("No kernel associated with sampler.\n");
        return NULL;
    }

    if(!firtree_kernel_is_valid(p->kernel)) {
        g_debug("firtree_kernel_is_valid() returns false for associated kernel.\n");
        return NULL;
    }

    return firtree_kernel_create_overall_function(p->kernel);
}

/* vim:sw=4:ts=4:et:cindent
 */
