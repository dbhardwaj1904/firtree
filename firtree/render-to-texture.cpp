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
/// \file render-to-texture.cpp Implementation of the render to texture support.
//=============================================================================

#include <firtree/main.h>
#include <firtree/math.h>
#include <firtree/image.h>
#include <firtree/opengl.h>
#include <firtree/internal/render-to-texture.h>

#include <assert.h>

namespace Firtree { namespace Internal {

//=============================================================================
#define CHECK_GL(ctx,call) do { \
    { ctx->call ; } \
    GLenum _err = ctx->glGetError(); \
    if(_err != GL_NO_ERROR) { \
        FIRTREE_ERROR("OpenGL error executing '%s': %s", \
            #call, gluErrorString(_err)); \
    } \
} while(0)

//=============================================================================
#define CHECK_GL_RV(rv, ctx,call) do { \
    { rv = ctx->call ; } \
    GLenum _err = ctx->glGetError(); \
    if(_err != GL_NO_ERROR) { \
        FIRTREE_ERROR("OpenGL error executing '%s': %s", \
            #call, gluErrorString(_err)); \
    } \
} while(0)

//=============================================================================
static void EnsureContextIsCurrent(OpenGLContext* context) 
{
    static bool initialised = false;
    if(!initialised)
    {
        initialised = true;
    }
}

//=============================================================================
RenderTextureContext::RenderTextureContext(uint32_t width, uint32_t height,
        Firtree::OpenGLContext* parentContext)
    :   OpenGLContext()
    ,   m_ParentContext(parentContext)
    ,   m_Size(width, height)
    ,   m_OpenGLTextureName(0)
    ,   m_OpenGLFrameBufferName(0)
    ,   m_PreviousOpenGLFrameBufferName(0)
{   
    FIRTREE_SAFE_RETAIN(m_ParentContext);
    if(m_ParentContext == NULL)
    {
        FIRTREE_ERROR("NULL value passed as current context.");
    }

    m_ParentContext->Begin();
    EnsureContextIsCurrent(m_ParentContext);

    m_OpenGLTextureName = m_ParentContext->GenTexture();

    CHECK_GL( m_ParentContext, glBindTexture(GL_TEXTURE_2D, m_OpenGLTextureName) );

    // HACK: This should really be GL_RGBA32F_ARB but on older cards,
    // only the 16-bit version is supported.
    CHECK_GL( m_ParentContext, glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F_ARB, width,
                height, 0, GL_RGBA, GL_FLOAT, NULL) );

    CHECK_GL( m_ParentContext, glGenerateMipmapEXT(GL_TEXTURE_2D) );

    GLint prevFb = 0;
    CHECK_GL( m_ParentContext, glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &prevFb) );

    CHECK_GL( m_ParentContext, glGenFramebuffersEXT(1, 
                reinterpret_cast<GLuint*>(&m_OpenGLFrameBufferName)) );
    CHECK_GL( m_ParentContext, glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, m_OpenGLFrameBufferName) );
    CHECK_GL( m_ParentContext, glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, 
                GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, 
                m_OpenGLTextureName, 0) );
    GLenum framebufferStatus;
    CHECK_GL_RV( framebufferStatus, m_ParentContext, glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) );

    if(framebufferStatus != GL_FRAMEBUFFER_COMPLETE_EXT)
    {
        FIRTREE_ERROR("Could not create frame buffer for render to "
                "texture. Status is 0x%x.", framebufferStatus);
    }

    CHECK_GL( m_ParentContext, glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, prevFb) );
    m_ParentContext->End();
}

//=============================================================================
RenderTextureContext::~RenderTextureContext()
{
    m_ParentContext->Begin();
    if(m_OpenGLFrameBufferName != 0)
    {
        EnsureContextIsCurrent(m_ParentContext);
        CHECK_GL( m_ParentContext, glDeleteFramebuffersEXT(1,
                   reinterpret_cast<GLuint*>(&m_OpenGLFrameBufferName)) );
        m_OpenGLFrameBufferName = 0;
    }

    if(m_OpenGLTextureName != 0u)
    {
        EnsureContextIsCurrent(m_ParentContext);
        m_ParentContext->DeleteTexture(m_OpenGLTextureName);
        m_OpenGLTextureName = 0;
    }
    m_ParentContext->End();

    FIRTREE_SAFE_RELEASE(m_ParentContext);
}

//=============================================================================
RenderTextureContext* RenderTextureContext::Create(uint32_t width,
        uint32_t height, Firtree::OpenGLContext* parentContext)
{
    return new RenderTextureContext(width, height, parentContext);
}

//=============================================================================
void RenderTextureContext::Begin()
{
    OpenGLContext::Begin();

    if(GetBeginDepth() == 1)
    {
        m_ParentContext->Begin();

        CHECK_GL( m_ParentContext, glGetFloatv(GL_VIEWPORT, m_PreviousViewport) );

        GLint currentFb = 0;
        CHECK_GL( m_ParentContext, glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &currentFb) );
        m_PreviousOpenGLFrameBufferName = currentFb;

        if(currentFb != m_OpenGLFrameBufferName)
        {
            CHECK_GL( m_ParentContext, glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 
                        m_OpenGLFrameBufferName) );
            CHECK_GL( m_ParentContext, glViewport(0,0,m_Size.Width,m_Size.Height) );
            FIRTREE_DEBUG("Changing viewport to 0,0+%i+%i.",
                    m_Size.Width,m_Size.Height);
        }

        GLenum framebufferStatus;
        CHECK_GL_RV( framebufferStatus, m_ParentContext, 
                glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) );
        if(framebufferStatus != GL_FRAMEBUFFER_COMPLETE_EXT)
        {
            FIRTREE_WARNING("Frame buffer status is not complete for rendering. "
                    "Status is 0x%x.", framebufferStatus);
            // assert(false);
        }
    }
}

//=============================================================================
void RenderTextureContext::End()
{
    if(GetBeginDepth() == 1)
    {
        CHECK_GL( m_ParentContext, glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 
                    m_PreviousOpenGLFrameBufferName) );

        CHECK_GL( m_ParentContext, glViewport(
                m_PreviousViewport[0], m_PreviousViewport[1],
                m_PreviousViewport[2], m_PreviousViewport[3]) );
        FIRTREE_DEBUG("Restoring viewport to %f,%f+%f+%f.",
                m_PreviousViewport[0], m_PreviousViewport[1],
                m_PreviousViewport[2], m_PreviousViewport[3]);

        m_ParentContext->End();
    }

    OpenGLContext::End();
}

} } // namespace Firtree::Internal

//=============================================================================
// vim:sw=4:ts=4:cindent:et
//
