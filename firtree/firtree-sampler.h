/* firtree-sampler.h */

/* Firtree - A generic image processing library
 * Copyright (C) 2009 Rich Wareham <richwareham@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License verstion as published
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

#ifndef _FIRTREE_SAMPLER
#define _FIRTREE_SAMPLER

#include <glib-object.h>

/**
 * SECTION:firtree-sampler
 * @short_description: Encapsulate the various image sources Firtree can use.
 * @include: firtree/firtree-sampler.h
 *
 * A FirtreeSampler is the object assigned to sampler arguments on Firtree
 * kernels to specify image sources and a pixel pipeline.
 */

G_BEGIN_DECLS

#define FIRTREE_TYPE_SAMPLER firtree_sampler_get_type()

#define FIRTREE_SAMPLER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), FIRTREE_TYPE_SAMPLER, FirtreeSampler))

#define FIRTREE_SAMPLER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), FIRTREE_TYPE_SAMPLER, FirtreeSamplerClass))

#define FIRTREE_IS_SAMPLER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FIRTREE_TYPE_SAMPLER))

#define FIRTREE_IS_SAMPLER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), FIRTREE_TYPE_SAMPLER))

#define FIRTREE_SAMPLER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), FIRTREE_TYPE_SAMPLER, FirtreeSamplerClass))

/**
 * FirtreeSampler
 * @parent: The GObject parent of FirtreeSampler.
 *
 * A FirtreeSampler is the object assigned to sampler arguments on Firtree
 * kernels to specify image sources and a pixel pipeline.
 */
typedef struct {
    GObject parent;
} FirtreeSampler;

typedef struct {
    GObjectClass parent_class;
} FirtreeSamplerClass;

GType firtree_sampler_get_type (void);

/**
 * firtree_sampler_new:
 *
 * Construct a NULL sampler.
 *
 * Returns: A newly instantiated FirtreeSampler.
 */
FirtreeSampler*
firtree_sampler_new (void);

G_END_DECLS

#endif /* _FIRTREE_SAMPLER */

/* vim:sw=4:ts=4:et:cindent
 */
