// FIRTREE - A generic image processing library
// Copyright (C) 2007, 2008 Rich Wareham <richwareham@gmail.com>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License verstion as published
// by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

//=============================================================================
/// \file image-int.cpp The implementation of the internal FIRTREE image.
//=============================================================================

#include <firtree/main.h>
#include <firtree/math.h>
#include <firtree/image.h>
#include <firtree/opengl.h>
#include <firtree/glsl-runtime.h>
#include <internal/image-int.h>
#include <internal/render-to-texture.h>

#include <assert.h>
#include <float.h>
#include <cmath>

namespace Firtree { namespace Internal {

#define CHECK_GL(ctx,call) do { \
    { ctx->call ; } \
    GLenum _err = ctx->glGetError(); \
    if(_err != GL_NO_ERROR) { \
        FIRTREE_ERROR("OpenGL error executing '%s': %s", \
            #call, gluErrorString(_err)); \
    } \
} while(0)

//=============================================================================
ImageImpl::ImageImpl()
    :   Image()
{
}

//=============================================================================
ImageImpl::~ImageImpl()
{
}

//=============================================================================
bool ImageImpl::GetIsFlipped() const
{
    return false;
}

//=============================================================================
Kernel* ImageImpl::GetKernel() const
{
    return NULL;
}

//=============================================================================
const Image* ImageImpl::GetBaseImage() const
{
    return this;
}

//=============================================================================
TransformedImageImpl::TransformedImageImpl(const Image* inim, AffineTransform* t,
        Rect2D crop)
    :   ImageImpl()
    ,   m_CropRect(crop)
{
    FIRTREE_DEBUG("Created TransformedImageImpl @ %p", this);

    if(t != NULL)
    {
        m_Transform = t->Copy();
    } else {
        m_Transform = AffineTransform::Identity();
    }

    m_BaseImage = dynamic_cast<ImageImpl*>(const_cast<Image*>(inim));
    FIRTREE_SAFE_RETAIN(m_BaseImage);
}

//=============================================================================
TransformedImageImpl::~TransformedImageImpl()
{
    FIRTREE_SAFE_RELEASE(m_Transform);
    FIRTREE_SAFE_RELEASE(m_BaseImage);
}

//=============================================================================
ImageImpl::PreferredRepresentation 
TransformedImageImpl::GetPreferredRepresentation() const
{
    return m_BaseImage->GetPreferredRepresentation();
}

//=============================================================================
bool TransformedImageImpl::GetIsFlipped() const
{
    // TransformedImageImpl implements the flip in 
    // GetTransformFromUnderlyingImage
    return false;
}

//=============================================================================
const Image* TransformedImageImpl::GetBaseImage() const
{
    return m_BaseImage->GetBaseImage();
}

//=============================================================================
Rect2D TransformedImageImpl::GetExtent() const
{
    return Rect2D::Intersect(m_CropRect,
            Rect2D::Transform(m_BaseImage->GetExtent(), m_Transform));
}

//=============================================================================
Size2D TransformedImageImpl::GetUnderlyingPixelSize() const
{
    return m_BaseImage->GetUnderlyingPixelSize();
}

//=============================================================================
AffineTransform* TransformedImageImpl::GetTransformFromUnderlyingImage() const
{
    AffineTransform* t = m_BaseImage->GetTransformFromUnderlyingImage();
    if(m_BaseImage->GetIsFlipped())
    {
        Rect2D baseExtent = m_BaseImage->GetExtent();
        
        // Flipping inifinite images in undefined.
        if(!Rect2D::IsInfinite(baseExtent)) 
        {
            t->ScaleBy(1.0, -1.0);
            t->TranslateBy(0.0, baseExtent.Size.Height - baseExtent.Origin.Y);
        }
    }
    t->AppendTransform(m_Transform);
    return t;
}

//=============================================================================
Kernel* TransformedImageImpl::GetKernel() const
{
    return m_BaseImage->GetKernel();
}

//=============================================================================
unsigned int TransformedImageImpl::GetAsOpenGLTexture(OpenGLContext* ctx)
{
    return m_BaseImage->GetAsOpenGLTexture(ctx);
}

//=============================================================================
Firtree::BitmapImageRep* TransformedImageImpl::CreateBitmapImageRep(
        Firtree::BitmapImageRep::PixelFormat format)
{
    // There are two cases here, a fast path where there is no addition
    // transform or cropping and we can simply return the base image's 
    // representation or one in which we need to use a kernel to render
    // ourselves.

    if(m_Transform->IsIdentity() && Rect2D::IsInfinite(m_CropRect))
    {
        return m_BaseImage->CreateBitmapImageRep(format);
    }

    Firtree::Kernel* renderingKernel = Kernel::CreateFromSource(
            "kernel vec4 passthroughKernel(sampler src) {"
            "  return sample(src, samplerCoord(src));"
            "}");
    renderingKernel->SetValueForKey(this, "src");
    
    Image* im = Image::CreateFromKernel(renderingKernel);
    KernelImageImpl* ki = dynamic_cast<KernelImageImpl*>(im);
    if(ki == NULL)
    {
        FIRTREE_ERROR("Error creating rendering kernel for transformed image.");
    }

    Firtree::BitmapImageRep* kir = ki->CreateBitmapImageRep(format);
    Firtree::BitmapImageRep* retVal = 
        Firtree::BitmapImageRep::CreateFromBitmapImageRep(kir, false);
    FIRTREE_SAFE_RELEASE(kir);

    FIRTREE_SAFE_RELEASE(im);

    FIRTREE_SAFE_RELEASE(renderingKernel);

    return retVal;
}

//=============================================================================
// TEXTURE BACKED IMAGE
//=============================================================================

//=============================================================================
TextureBackedImageImpl::TextureBackedImageImpl()
    :   ImageImpl()
    ,   m_BitmapRep(NULL)
{
    FIRTREE_DEBUG("Created TextureBackedImageImpl @ %p", this);
}

//=============================================================================
TextureBackedImageImpl::~TextureBackedImageImpl()
{
    FIRTREE_SAFE_RELEASE(m_BitmapRep);
}

//=============================================================================
ImageImpl::PreferredRepresentation 
TextureBackedImageImpl::GetPreferredRepresentation() const
{
    return ImageImpl::OpenGLTexture;
}

//=============================================================================
Firtree::BitmapImageRep* TextureBackedImageImpl::CreateBitmapImageRep(
        Firtree::BitmapImageRep::PixelFormat format)
{
    OpenGLContext* c = GetCurrentGLContext();

    GLenum type = (format == Firtree::BitmapImageRep::Float) ?
        GL_FLOAT : GL_UNSIGNED_BYTE;
    size_t element_size = (format == Firtree::BitmapImageRep::Float) ?
        sizeof(float) : 1;

    FIRTREE_DEBUG("Performance hint: copying GPU -> CPU.");

    if(c == NULL)
    {
        FIRTREE_ERROR("Attempt to render image to bitmap outside of an OpenGL "
                "context.");
    }

    unsigned int tex = GetAsOpenGLTexture(c);

    c->Begin();
    GLint w, h;
    CHECK_GL( c, glBindTexture(GL_TEXTURE_2D, tex) );
    CHECK_GL( c, glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w) );
    CHECK_GL( c, glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h) );

    Blob* imageBlob = Blob::CreateWithLength(w*h*4*element_size);
    CHECK_GL( c, glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, type, 
                (void*)(imageBlob->GetBytes())) );
    c->End();

    FIRTREE_SAFE_RELEASE(m_BitmapRep);
    m_BitmapRep = BitmapImageRep::Create(imageBlob,
            w, h, w*4*element_size, format, false);
    FIRTREE_SAFE_RELEASE(imageBlob);

    FIRTREE_SAFE_RETAIN(m_BitmapRep);
    return m_BitmapRep;
}

//=============================================================================
// TEXTURE IMAGE
//=============================================================================

//=============================================================================
TextureImageImpl::TextureImageImpl(unsigned int texObj, OpenGLContext* context,
        bool flipped)
    :   TextureBackedImageImpl()
    ,   m_TexObj(texObj)
    ,   m_Context(context)
    ,   m_IsFlipped(flipped)
{
    FIRTREE_DEBUG("Created TextureImageImpl @ %p", this);

    FIRTREE_SAFE_RETAIN(m_Context);
}

//=============================================================================
TextureImageImpl::~TextureImageImpl()
{
    FIRTREE_SAFE_RELEASE(m_Context);
}

//=============================================================================
Rect2D TextureImageImpl::GetExtent() const
{
    GLint w,h;

    m_Context->Begin();
    CHECK_GL( m_Context, glBindTexture(GL_TEXTURE_2D, m_TexObj) );
    CHECK_GL( m_Context, glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w) );
    CHECK_GL( m_Context, glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h) );
    m_Context->End();

    return Rect2D(0,0,w,h);
}

//=============================================================================
Size2D TextureImageImpl::GetUnderlyingPixelSize() const
{
    return GetExtent().Size;
}

//=============================================================================
bool TextureImageImpl::GetIsFlipped() const
{
    return m_IsFlipped;
}


//=============================================================================
AffineTransform* TextureImageImpl::GetTransformFromUnderlyingImage() const
{
    if(!m_IsFlipped)
    {
        return AffineTransform::Identity();
    } else {
        float height = GetExtent().Size.Height;
        AffineTransform* flip_transform = AffineTransform::Scaling(1.0, -1.0);
        flip_transform->AppendTransform(
                AffineTransform::Translation(0.0, height));
        return flip_transform;
    }

    FIRTREE_ERROR("Unreachable code has been reached!");
    return NULL;
}

//=============================================================================
unsigned int TextureImageImpl::GetAsOpenGLTexture(OpenGLContext* ctx)
{
    return m_TexObj;
}

//=============================================================================
// BITMAP BACKED IMAGE
//=============================================================================

//=============================================================================
BitmapBackedImageImpl::BitmapBackedImageImpl()
    :   ImageImpl()
    ,   m_GLTexture(0)
    ,   m_GLContext(NULL)
    ,   m_CacheValid(false)
{
    FIRTREE_DEBUG("Created BitmapBackedImageImpl @ %p", this);
}

//=============================================================================
BitmapBackedImageImpl::~BitmapBackedImageImpl()
{
    if(m_GLTexture != 0)
    {
        m_GLContext->DeleteTexture(m_GLTexture);
        m_GLTexture = 0;
    }
    FIRTREE_SAFE_RELEASE(m_GLContext);
}

//=============================================================================
ImageImpl::PreferredRepresentation 
BitmapBackedImageImpl::GetPreferredRepresentation() const
{
    return ImageImpl::BitmapImageRep;
}

//=============================================================================
void BitmapBackedImageImpl::InvalidateCache()
{
    m_CacheValid = false;
}

//=============================================================================
unsigned int BitmapBackedImageImpl::GetAsOpenGLTexture(OpenGLContext* ctx)
{
    Firtree::BitmapImageRep* bir = CreateBitmapImageRep(
            Firtree::BitmapImageRep::Any);

    if(bir->IsDynamic) {
        InvalidateCache();
    }

    ctx->Begin();

    if(m_GLTexture == 0)
    {
        FIRTREE_SAFE_RETAIN(ctx);
        FIRTREE_SAFE_RELEASE(m_GLContext);
        m_GLContext = ctx;

        m_GLTexture = m_GLContext->GenTexture();

        InvalidateCache();
    }

    m_TexSize = Size2DU32(bir->Width, bir->Height);
    if((m_TexSize.Width != bir->Width) || (m_TexSize.Height != bir->Height))
    {
        InvalidateCache();
    }

    if(!m_CacheValid)
    {
        FIRTREE_DEBUG("Image %p: Performance hint: copying CPU -> GPU.", 
                this);

        CHECK_GL( ctx, glBindTexture(GL_TEXTURE_2D, m_GLTexture) );

        if(bir->Format == Firtree::BitmapImageRep::Float)
        { 
            assert(bir->Stride == bir->Width*4*4);
            CHECK_GL( ctx, glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 
                        bir->Width, bir->Height, 0,
                        GL_RGBA, GL_FLOAT,
                        bir->ImageBlob->GetBytes()) );
        } else if(bir->Format == Firtree::BitmapImageRep::Byte) {
            assert(bir->Stride == bir->Width*4);
            CHECK_GL( ctx, glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 
                        bir->Width, bir->Height, 0,
                        GL_RGBA, GL_UNSIGNED_BYTE,
                        bir->ImageBlob->GetBytes()) );
        } else {
            FIRTREE_ERROR("Unknown bitmap image rep format: %i", bir->Format);
        }

        m_CacheValid = true;
    }

    ctx->End();

    FIRTREE_SAFE_RELEASE(bir);

    return m_GLTexture;
}

//=============================================================================
// KERNEL IMAGE
//=============================================================================

//=============================================================================
KernelImageImpl::KernelImageImpl(Firtree::Kernel* k, ExtentProvider* extentProvider)
    :   TextureBackedImageImpl()
    ,   m_Kernel(k)
    ,   m_ExtentProvider(extentProvider)
    ,   m_TextureRenderer(NULL)
{
    FIRTREE_DEBUG("Created KernelImageImpl @ %p", this);
    FIRTREE_SAFE_RETAIN(m_Kernel);
    FIRTREE_SAFE_RETAIN(m_ExtentProvider);
}

//=============================================================================
KernelImageImpl::~KernelImageImpl()
{
    FIRTREE_SAFE_RELEASE(m_TextureRenderer);

    FIRTREE_SAFE_RELEASE(m_Kernel);
    FIRTREE_SAFE_RELEASE(m_ExtentProvider);
}

//=============================================================================
ImageImpl::PreferredRepresentation KernelImageImpl::GetPreferredRepresentation() const
{
    return ImageImpl::Kernel;
}

//=============================================================================
Rect2D KernelImageImpl::GetExtent() const
{
    if(m_ExtentProvider == NULL)
    {
        FIRTREE_WARNING("Kernel backed image lacks an extent provider.");
        return Rect2D::MakeInfinite();
    }

    return m_ExtentProvider->ComputeExtentForKernel(m_Kernel);
}

//=============================================================================
Size2D KernelImageImpl::GetUnderlyingPixelSize() const
{
    // Use the extent provider.
    return GetExtent().Size;
}

//=============================================================================
AffineTransform* KernelImageImpl::GetTransformFromUnderlyingImage() const
{
    return AffineTransform::Identity();
}

//=============================================================================
Firtree::Kernel* KernelImageImpl::GetKernel() const
{
    assert(m_Kernel != NULL);
    return m_Kernel;
}

//=============================================================================
unsigned int KernelImageImpl::GetAsOpenGLTexture(OpenGLContext* ctx)
{
    // FIXME
    return 0;

#if 0
    Rect2D extent = GetExtent();
    if(Rect2D::IsInfinite(extent))
    {
        FIRTREE_ERROR("Cannot render infinite extent image to texture.");
        return 0;
    }

    FIRTREE_DEBUG("Rendering kernel-based image to texture with extent: %f,%f+%f+%f.",
            extent.Origin.X, extent.Origin.Y,
            extent.Size.Width, extent.Size.Height);

    Size2DU32 imageSize(ceil(extent.Size.Width), ceil(extent.Size.Height));

    if(m_TextureRenderer != NULL)
    {
        Size2DU32 cacheSize = m_TextureRenderer->GetSize();
        if((cacheSize.Width != imageSize.Width) || 
                (cacheSize.Height != imageSize.Height))
        {
            FIRTREE_SAFE_RELEASE(m_TextureRenderer);
            m_TextureRenderer = NULL;
            FIRTREE_SAFE_RELEASE(m_GLRenderer);
            m_GLRenderer = NULL;
        } else if(m_TextureRenderer->GetParentContext() != ctx) {
            FIRTREE_SAFE_RELEASE(m_TextureRenderer);
            m_TextureRenderer = NULL;
            FIRTREE_SAFE_RELEASE(m_GLRenderer);
            m_GLRenderer = NULL;
        }
    }

    ctx->Begin();

    if(m_TextureRenderer == NULL)
    {
        FIRTREE_SAFE_RELEASE(m_GLRenderer);
        m_TextureRenderer = RenderTextureContext::Create(imageSize.Width,
                imageSize.Height, ctx);
    }

    m_GLRenderer = GLRenderer::Create(m_TextureRenderer);
    m_TextureRenderer->Begin();
    m_GLRenderer->Clear(0,0,0,0);
    m_GLRenderer->RenderAtPoint(this, Point2D(0,0), extent);
    m_TextureRenderer->End();

    ctx->End();

    FIRTREE_SAFE_RELEASE(m_GLRenderer);

    return m_TextureRenderer->GetOpenGLTexture();
#endif
}

//=============================================================================
// BITMAP IMAGE
//=============================================================================

//=============================================================================
BitmapImageImpl::BitmapImageImpl(const Firtree::BitmapImageRep* imageRep, bool copy)
    :   BitmapBackedImageImpl()
{
    FIRTREE_DEBUG("Created BitmapImageImpl @ %p", this);
    if(imageRep->ImageBlob == NULL) { return; }
    if(imageRep->Stride < imageRep->Width) { return; }
    if(imageRep->Stride*imageRep->Height > imageRep->ImageBlob->GetLength()) {
        return; 
    }

    m_BitmapRep = BitmapImageRep::CreateFromBitmapImageRep(imageRep, copy);
}

//=============================================================================
BitmapImageImpl::~BitmapImageImpl()
{
    FIRTREE_SAFE_RELEASE(m_BitmapRep);
}

//=============================================================================
Rect2D BitmapImageImpl::GetExtent() const
{
    // Get the size of the underlying pixel representation.
    Size2D pixelSize = GetUnderlyingPixelSize();

    // Is this image 'infinite' in extent?
    if(Size2D::IsInfinite(pixelSize))
    {
        // Return an 'infinite' extent
        return Rect2D::MakeInfinite();
    }

    // Form a rectangle which covers the underlying pixel
    // representation
    Rect2D pixelRect = Rect2D(Point2D(0,0), pixelSize);

    AffineTransform* transform = GetTransformFromUnderlyingImage();

    Rect2D extentRect = Rect2D::Transform(pixelRect, transform);

    FIRTREE_SAFE_RELEASE(transform);

    return extentRect;
}

//=============================================================================
Size2D BitmapImageImpl::GetUnderlyingPixelSize() const
{
    return Size2D(m_BitmapRep->Width, m_BitmapRep->Height);
}

//=============================================================================
bool BitmapImageImpl::GetIsFlipped() const
{
    return m_BitmapRep->Flipped;
}

//=============================================================================
AffineTransform* BitmapImageImpl::GetTransformFromUnderlyingImage() const
{
    return AffineTransform::Identity();
}

//=============================================================================
Firtree::BitmapImageRep* BitmapImageImpl::CreateBitmapImageRep(
        Firtree::BitmapImageRep::PixelFormat format)
{
    if((format != BitmapImageRep::Any) && (format != m_BitmapRep->Format))
    {
        Blob* imblob = NULL;
        if(format == BitmapImageRep::Float) {
            imblob = Blob::CreateWithLength(
                    m_BitmapRep->Width * m_BitmapRep->Height * 
                        sizeof(float) * 4);
        } else if(format == BitmapImageRep::Byte) {
            imblob = Blob::CreateWithLength(
                    m_BitmapRep->Width * m_BitmapRep->Height * 4);
        } else {
            FIRTREE_WARNING("Implicit bitmap format conversion not "
                    "yet implemented.");
        }

        FormatConversion::SourcePixelFormat informat;
        switch(m_BitmapRep->Format) {
            case BitmapImageRep::Byte:
                informat = FormatConversion::RGBA8;
                break;
            default:
                FIRTREE_WARNING("Unknown format: %i", 
                        m_BitmapRep->Format);
        }
        FormatConversion::ExpandComponents(m_BitmapRep->ImageBlob,
                informat, imblob, format);
        Firtree::BitmapImageRep* newrep = BitmapImageRep::Create(imblob,
                m_BitmapRep->Width, m_BitmapRep->Height,
                m_BitmapRep->Width * sizeof(float) * 4,
                format);
        FIRTREE_SAFE_RELEASE(imblob);

        return newrep;
    }

    return BitmapImageRep::CreateFromBitmapImageRep(m_BitmapRep, false);
}

//=============================================================================
// IMAGE PROVIDER IMAGE
//=============================================================================

//=============================================================================
ImageProviderImageImpl::ImageProviderImageImpl(ImageProvider* improv)
    :   BitmapBackedImageImpl()
    ,   m_ImageProvider(improv)
    ,   m_FlipFlagCache(false)
{
    FIRTREE_DEBUG("Created ImageProviderImageImpl @ %p", this);
    FIRTREE_SAFE_RETAIN(m_ImageProvider);
}

//=============================================================================
ImageProviderImageImpl::~ImageProviderImageImpl()
{
    FIRTREE_SAFE_RELEASE(m_ImageProvider);
}

//=============================================================================
bool ImageProviderImageImpl::GetIsFlipped() const
{
    return m_FlipFlagCache;
}

//=============================================================================
Rect2D ImageProviderImageImpl::GetExtent() const
{
    // Get the size of the underlying pixel representation.
    Size2D pixelSize = GetUnderlyingPixelSize();

    // Is this image 'infinite' in extent?
    if(Size2D::IsInfinite(pixelSize))
    {
        // Return an 'infinite' extent
        return Rect2D::MakeInfinite();
    }

    // Form a rectangle which covers the underlying pixel
    // representation
    Rect2D pixelRect = Rect2D(Point2D(0,0), pixelSize);

    AffineTransform* transform = GetTransformFromUnderlyingImage();

    Rect2D extentRect = Rect2D::Transform(pixelRect, transform);

    FIRTREE_SAFE_RELEASE(transform);

    return extentRect;
}

//=============================================================================
Size2D ImageProviderImageImpl::GetUnderlyingPixelSize() const
{
    return m_ImageProvider->GetImageSize();
}

//=============================================================================
AffineTransform* ImageProviderImageImpl::GetTransformFromUnderlyingImage() const
{
    return AffineTransform::Identity();
}

//=============================================================================
Firtree::BitmapImageRep* ImageProviderImageImpl::CreateBitmapImageRep(
        Firtree::BitmapImageRep::PixelFormat format)
{
    // FIXME: Pass format down.
    if((format != Firtree::BitmapImageRep::Float) && (format != Firtree::BitmapImageRep::Any))
    {
        FIRTREE_ERROR("ImageProvider API needs updating to handle non-floating "
                "point formats.");
    }

    // TODO: Should probably make this method return a const BitmapImageRep.
    Firtree::BitmapImageRep* rv = 
        const_cast<Firtree::BitmapImageRep*>(m_ImageProvider->CreateImageRep());
    m_FlipFlagCache = rv->Flipped;
    return rv;
}

} } // namespace Firtree::Internal

//=============================================================================
// vim:sw=4:ts=4:cindent:et
//
