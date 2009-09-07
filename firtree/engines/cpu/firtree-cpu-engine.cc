/* firtree-cpu-engine.cc */

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

#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS

#include <firtree/firtree-debug.h>
#include "firtree-cpu-engine.h"

#include <llvm/Module.h>
#include <llvm/ModuleProvider.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Linker.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JIT.h>

#include <llvm/Analysis/Verifier.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/LinkAllPasses.h>

#include <llvm/DerivedTypes.h>
#include <llvm/Instructions.h>
#include <llvm/Constants.h>

#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Support/CommandLine.h>

#if FIRTREE_HAVE_CLUTTER
#   include <cogl/cogl.h>
#   include "clutter.hh"
#endif

#include <firtree/internal/firtree-cogl-texture-sampler-intl.hh>
#include <firtree/internal/firtree-engine-intl.hh>
#include <firtree/internal/firtree-sampler-intl.hh>

#include <common/system-info.h>

#include <sstream>

#define bindata _firtee_cpu_engine_render_buffer_mod
#include "llvm-cpu-support.bc.h"
#undef bindata

G_DEFINE_TYPE (FirtreeCpuEngine, firtree_cpu_engine, G_TYPE_OBJECT)

#define GET_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), FIRTREE_TYPE_CPU_ENGINE, FirtreeCpuEnginePrivate))

/* Indexed by FirtreeBufferFormat */
const char* _function_names[] = {
    "render_FIRTREE_FORMAT_ARGB32",
    "render_FIRTREE_FORMAT_ARGB32_PREMULTIPLIED",
    "render_FIRTREE_FORMAT_XRGB32",
    "render_FIRTREE_FORMAT_RGBA32",
    "render_FIRTREE_FORMAT_RGBA32_PREMULTIPLIED",

    "render_FIRTREE_FORMAT_ABGR32",
    "render_FIRTREE_FORMAT_ABGR32_PREMULTIPLIED",
    "render_FIRTREE_FORMAT_XBGR32",
    "render_FIRTREE_FORMAT_BGRA32",
    "render_FIRTREE_FORMAT_BGRA32_PREMULTIPLIED",

    "render_FIRTREE_FORMAT_RGB24",
    "render_FIRTREE_FORMAT_BGR24",

    "render_FIRTREE_FORMAT_RGBX32",
    "render_FIRTREE_FORMAT_BGRX32",

    /* Rendering to the following formats is not supported */

    NULL, /* FIRTREE_FORMAT_L8 */
    NULL, /* FIRTREE_FORMAT_I420_FOURCC */
    NULL, /* FIRTREE_FORMAT_YV12_FOURCC */

    NULL,
};

#define RENDER_FUNC_NAME(id) (_function_names[(id)])

typedef void (* RenderFunc) (unsigned char* buffer,
    unsigned int row_width, unsigned int num_rows,
    unsigned int row_stride, float* extents);

typedef struct _FirtreeCpuEnginePrivate FirtreeCpuEnginePrivate;

struct _FirtreeCpuEnginePrivate {
    FirtreeSampler*             sampler;
    gulong                      sampler_handler_id;
    llvm::ExecutionEngine*      cached_engine;
    GThreadPool*                thread_pool;
    GAsyncQueue*                response_queue;
};

/* invalidate (and release) any cached LLVM modules/functions. This
 * will cause them to be re-generated when ..._get_function() is next
 * called. */
static void
_firtree_cpu_engine_invalidate_llvm_cache(FirtreeCpuEngine* self)
{
    FirtreeCpuEnginePrivate* p = GET_PRIVATE(self);
    if(p && p->cached_engine) {
        delete p->cached_engine;
        p->cached_engine = NULL;
    }
}

struct FirtreeCpuEngineRenderRequest {
    RenderFunc      func;
    unsigned char*  buffer;
    unsigned int    row_width;
    unsigned int    num_rows;
    unsigned int    row_stride;
    float           extents[4];
};

static void
_firtree_cpu_engine_thread_func(gpointer data, gpointer user_data)
{
    FirtreeCpuEngine* self = FIRTREE_CPU_ENGINE(user_data);
    g_assert(self);
    FirtreeCpuEnginePrivate* p = GET_PRIVATE(self); 
    g_assert(p);

    FirtreeCpuEngineRenderRequest* request = (FirtreeCpuEngineRenderRequest*)data;

    /* do stuff */
    g_assert(request->func);
    request->func(request->buffer, request->row_width, request->num_rows, 
            request->row_stride, request->extents);

    /* signal we've finished this request. */
    g_async_queue_push(p->response_queue, request);
}

static void*
_firtree_cpu_lazy_function_creator(const std::string& name) {
    if(name == "exp_f") {
        return (void*)expf;
    }
    if(name == "atan_f") {
        return (void*)atanf;
    }
    if(name == "atan_ff") {
        return (void*)atan2f;
    }
    g_debug("Do not know what function to use for '%s'.", name.c_str());
    return NULL;
}

static void
firtree_cpu_engine_get_property (GObject *object, guint property_id,
        GValue *value, GParamSpec *pspec)
{
    switch (property_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
firtree_cpu_engine_set_property (GObject *object, guint property_id,
        const GValue *value, GParamSpec *pspec)
{
    switch (property_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
firtree_cpu_engine_dispose (GObject *object)
{
    G_OBJECT_CLASS (firtree_cpu_engine_parent_class)->dispose (object);
    FirtreeCpuEnginePrivate* p = GET_PRIVATE(object); 

    if(p && p->thread_pool) {
        g_thread_pool_free(p->thread_pool, FALSE, TRUE);
        p->thread_pool = NULL;
    }

    if(p && p->response_queue) {
        g_async_queue_unref(p->response_queue);
        p->response_queue = NULL;
    }

    FirtreeCpuEngine* cpu_engine = FIRTREE_CPU_ENGINE(object);
    firtree_cpu_engine_set_sampler(cpu_engine, NULL);
    _firtree_cpu_engine_invalidate_llvm_cache(cpu_engine);
}

static void
firtree_cpu_engine_finalize (GObject *object)
{
    G_OBJECT_CLASS (firtree_cpu_engine_parent_class)->finalize (object);
}

static void
firtree_cpu_engine_class_init (FirtreeCpuEngineClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (FirtreeCpuEnginePrivate));

    object_class->get_property = firtree_cpu_engine_get_property;
    object_class->set_property = firtree_cpu_engine_set_property;
    object_class->dispose = firtree_cpu_engine_dispose;
    object_class->finalize = firtree_cpu_engine_finalize;
}

static void
firtree_cpu_engine_init (FirtreeCpuEngine *self)
{
    FirtreeCpuEnginePrivate* p = GET_PRIVATE(self); 
    p->sampler = NULL;
    p->cached_engine = NULL;
    p->thread_pool = g_thread_pool_new(
            _firtree_cpu_engine_thread_func,
            self, system_info_cpu_cores(),
            TRUE, NULL);
    g_assert(p->thread_pool);
    p->response_queue = g_async_queue_new();
}

FirtreeCpuEngine*
firtree_cpu_engine_new (void)
{
    return (FirtreeCpuEngine*)
        g_object_new (FIRTREE_TYPE_CPU_ENGINE, NULL);
}

static void
firtree_cpu_engine_sampler_module_changed_cb(gpointer sampler,
        gpointer data)
{
    FirtreeCpuEngine* self = FIRTREE_CPU_ENGINE(data);
    _firtree_cpu_engine_invalidate_llvm_cache(self);
}

void
firtree_cpu_engine_set_sampler (FirtreeCpuEngine* self,
        FirtreeSampler* sampler)
{
    FirtreeCpuEnginePrivate* p = GET_PRIVATE(self); 

    if(p->sampler) {
        /* disconnect the original handler */
        g_signal_handler_disconnect(p->sampler, p->sampler_handler_id);

        g_object_unref(p->sampler);
        p->sampler = NULL;
    }

    if(sampler) {
        p->sampler = sampler;
        g_object_ref(p->sampler);

        p->sampler_handler_id = g_signal_connect(p->sampler, 
                "module-changed",
                G_CALLBACK(firtree_cpu_engine_sampler_module_changed_cb), self);
    }

    _firtree_cpu_engine_invalidate_llvm_cache(self);
}

FirtreeSampler*
firtree_cpu_engine_get_sampler (FirtreeCpuEngine* self)
{
    FirtreeCpuEnginePrivate* p = GET_PRIVATE(self); 
    return p->sampler;
}

/* lifted from the LLVM SVN. */
static
void createStandardModulePasses(llvm::PassManager *PM,
        unsigned OptimizationLevel,
        bool OptimizeSize,
        bool UnitAtATime,
        bool UnrollLoops,
        bool SimplifyLibCalls,
        bool HaveExceptions,
        llvm::Pass *InliningPass) {
    if (OptimizationLevel == 0) {
        if (InliningPass)
            PM->add(InliningPass);
    } else {
        if (UnitAtATime)
            PM->add(llvm::createRaiseAllocationsPass());    // call %malloc -> malloc inst
        PM->add(llvm::createCFGSimplificationPass());     // Clean up disgusting code
        // Kill useless allocas
        PM->add(llvm::createPromoteMemoryToRegisterPass());
        if (UnitAtATime) {
            PM->add(llvm::createGlobalOptimizerPass());     // Optimize out global vars
            PM->add(llvm::createGlobalDCEPass());           // Remove unused fns and globs
            // IP Constant Propagation
            PM->add(llvm::createIPConstantPropagationPass());
            PM->add(llvm::createDeadArgEliminationPass());  // Dead argument elimination
        }
        PM->add(llvm::createInstructionCombiningPass());  // Clean up after IPCP & DAE
        PM->add(llvm::createCFGSimplificationPass());     // Clean up after IPCP & DAE
        if (UnitAtATime) {
            if (HaveExceptions)
                PM->add(llvm::createPruneEHPass());           // Remove dead EH info
            PM->add(llvm::createFunctionAttrsPass());       // Set readonly/readnone attrs
        }
        if (InliningPass)
            PM->add(InliningPass);
        if (OptimizationLevel > 2)
            PM->add(llvm::createArgumentPromotionPass());   // Scalarize uninlined fn args
        if (SimplifyLibCalls)
            PM->add(llvm::createSimplifyLibCallsPass());    // Library Call Optimizations
        PM->add(llvm::createInstructionCombiningPass());  // Cleanup for scalarrepl.
        PM->add(llvm::createJumpThreadingPass());         // Thread jumps.
        PM->add(llvm::createCFGSimplificationPass());     // Merge & remove BBs
        PM->add(llvm::createScalarReplAggregatesPass());  // Break up aggregate allocas
        PM->add(llvm::createInstructionCombiningPass());  // Combine silly seq's
        PM->add(llvm::createCondPropagationPass());       // Propagate conditionals
        PM->add(llvm::createTailCallEliminationPass());   // Eliminate tail calls
        PM->add(llvm::createCFGSimplificationPass());     // Merge & remove BBs
        PM->add(llvm::createReassociatePass());           // Reassociate expressions
        PM->add(llvm::createLoopRotatePass());            // Rotate Loop
        PM->add(llvm::createLICMPass());                  // Hoist loop invariants
        PM->add(llvm::createLoopUnswitchPass(OptimizeSize));
        PM->add(llvm::createLoopIndexSplitPass());        // Split loop index
        PM->add(llvm::createInstructionCombiningPass());  
        PM->add(llvm::createIndVarSimplifyPass());        // Canonicalize indvars
        PM->add(llvm::createLoopDeletionPass());          // Delete dead loops
        if (UnrollLoops)
            PM->add(llvm::createLoopUnrollPass());          // Unroll small loops
        PM->add(llvm::createInstructionCombiningPass());  // Clean up after the unroller
        PM->add(llvm::createGVNPass());                   // Remove redundancies
        PM->add(llvm::createMemCpyOptPass());             // Remove memcpy / form memset
        PM->add(llvm::createSCCPPass());                  // Constant prop with SCCP

        // Run instcombine after redundancy elimination to exploit opportunities
        // opened up by them.
        PM->add(llvm::createInstructionCombiningPass());
        PM->add(llvm::createCondPropagationPass());       // Propagate conditionals
        PM->add(llvm::createDeadStoreEliminationPass());  // Delete dead stores
        PM->add(llvm::createAggressiveDCEPass());         // Delete dead instructions
        PM->add(llvm::createCFGSimplificationPass());     // Merge & remove BBs

        if (UnitAtATime) {
            PM->add(llvm::createStripDeadPrototypesPass()); // Get rid of dead prototypes
            PM->add(llvm::createDeadTypeEliminationPass()); // Eliminate dead types
        }

        if (OptimizationLevel > 1 && UnitAtATime)
            PM->add(llvm::createConstantMergePass());       // Merge dup global constants
    }
}

/* optimise a llvm module by internalising all but the
 * named function and agressively inlining. */
void optimise_module(llvm::Module* m, std::vector<const char*>& export_list)
{
    if(m == NULL) {
    	return;
    }

    llvm::PassManager PM;

    PM.add(new llvm::TargetData(m));

#if FIRTREE_HAVE_CLUTTER
    PM.add(new CanonicaliseCoglCallsPass());
#endif

    PM.add(llvm::createInternalizePass(export_list));
    PM.add(llvm::createFunctionInliningPass(32768)); 

    PM.add(llvm::createAggressiveDCEPass()); 

    /* copied from the LLVM opt utility */
    createStandardModulePasses(&PM, 3, 
                            /*OptimizeSize=*/ false,
                            /*UnitAtATime=*/ true,
                            /*UnrollLoops=*/ true,
                            /*SimplifyLibCalls=*/ true,
                            /*HaveExceptions=*/ false,
                            llvm::createFunctionInliningPass(32768));

    PM.run(*m);
}

void*
firtree_cpu_engine_get_renderer_func(FirtreeCpuEngine* self, const char* name)
{
    if(!name) {
        g_debug("No renderer function specified.");
        return NULL;
    }

    FirtreeCpuEnginePrivate* p = GET_PRIVATE(self); 
    if(p->cached_engine) {
        llvm::Function* render_function = p->cached_engine->FindFunctionNamed(
                name);
        if(!render_function) {
            g_error("Cached module has no render function.");
        }
        void* render = p->cached_engine->getPointerToFunction(render_function);
        g_assert(render);
        return render;
    }

    llvm::Function* sampler_function = 
        firtree_sampler_get_sample_function(p->sampler);
    if(sampler_function == NULL) {
        g_debug("No sampler function.\n");
        return NULL;
    }

    /* Nasty, nasty hack to set an option to disable MMX *
     * This is really horrible but is required by:       *
     *   http://llvm.org/bugs/show_bug.cgi?id=3287       */
    static bool set_opt = false;
    static const char* opts[] = {
        "progname",
    	"-disable-mmx",
    };
    if(!set_opt) {
        llvm::cl::ParseCommandLineOptions(2, const_cast<char**>(opts));
    	set_opt = true;
    }

    /* create an LLVM module from the bitcode in 
     * _firtee_cpu_engine_render_buffer_mod */
    llvm::MemoryBuffer* mem_buf = 
        llvm::MemoryBuffer::getMemBuffer(
                (const char*)_firtee_cpu_engine_render_buffer_mod,
                (const char*)(_firtee_cpu_engine_render_buffer_mod + 
                    sizeof(_firtee_cpu_engine_render_buffer_mod) - 1));
    llvm::Module* m = llvm::ParseBitcodeFile(mem_buf);
    delete mem_buf;
    
    llvm::Linker* linker = new llvm::Linker("sampler", m);

    llvm::Module* sampler_mod = llvm::CloneModule(sampler_function->getParent());
    std::string err_str;
    bool was_error = linker->LinkInModule(sampler_mod, &err_str);
    if(was_error) {
        g_error("Error linking in sampler: %s\n", err_str.c_str());
    }
    delete sampler_mod;
    sampler_mod = NULL;

    llvm::Module* linked_module = linker->releaseModule();
    delete linker;

    llvm::Function* new_sampler_func = linked_module->getFunction(
            sampler_function->getName());

    llvm::Function* existing_sample_func = linked_module->getFunction(
            "sampler_output");
    if(existing_sample_func) {
        llvm::BasicBlock* bb = llvm::BasicBlock::Create("entry", 
                existing_sample_func);
        std::vector<llvm::Value*> args;
        args.push_back(existing_sample_func->arg_begin());
        llvm::Value* sample_val = llvm::CallInst::Create(
                new_sampler_func,
                args.begin(), args.end(),
                "rv", bb);
        llvm::ReturnInst::Create(sample_val, bb);
    }

    std::vector<const char*> render_functions;
    for(int fi = 0; fi < FIRTREE_FORMAT_LAST; ++fi) {
        if(RENDER_FUNC_NAME(fi)) {
            render_functions.push_back(RENDER_FUNC_NAME(fi));
        }
    }
    optimise_module(linked_module, render_functions);

    llvm::ModuleProvider* mp = new llvm::ExistingModuleProvider(m);

    std::string err;
    llvm::ExecutionEngine* engine = llvm::ExecutionEngine::create(mp,
            false, &err);
    engine->InstallLazyFunctionCreator(_firtree_cpu_lazy_function_creator);
    p->cached_engine = engine;

    if(!engine) {
        g_debug("Error JIT-ing render: %s\n", err.c_str());
        delete mp;
        return NULL;
    }

    llvm::Function* render_function = p->cached_engine->FindFunctionNamed(name);
    if(!render_function) {
        g_debug("JIT engine has no function named %s.", name);
        return NULL;
    }
    g_assert(render_function);
    void* render = engine->getPointerToFunction(render_function);
    g_assert(render);

    return render;
}

GString*
firtree_debug_dump_cpu_engine_function(FirtreeCpuEngine* self)
{
    FirtreeCpuEnginePrivate* p = GET_PRIVATE(self); 
    void* rf = firtree_cpu_engine_get_renderer_func(self,
            RENDER_FUNC_NAME(FIRTREE_FORMAT_ABGR32_PREMULTIPLIED));
    if(!p->cached_engine || !rf) {
        return NULL;
    }

    llvm::Module* m = p->cached_engine->FindFunctionNamed(
            RENDER_FUNC_NAME(FIRTREE_FORMAT_ABGR32_PREMULTIPLIED))->getParent();
    
    std::ostringstream out;

    m->print(out, NULL);

    /* This is non-optimal, invlving a copy as it does but
     * production code shouldn't be using this function anyway. */
    return g_string_new(out.str().c_str());
}

static void
firtree_cpu_engine_perform_render(FirtreeCpuEngine* self,
        RenderFunc func, unsigned char* buffer, unsigned int row_width, 
        unsigned int num_rows, unsigned int row_stride, 
        float* extents) 
{
    FirtreeCpuEnginePrivate* p = GET_PRIVATE(self); 

    if(!p->sampler)
        return;

    if(!firtree_sampler_lock(p->sampler)) {
        g_debug("Failed to lock sampler.");
        return;
    }

    /* Create an array of render requests. */
    GPtrArray* request_array = g_ptr_array_new();

    float dy = extents[3] / (float)num_rows;
    int slice_rows = 8;
    guint row = 0;
    float y = extents[1];
    float slice_height = dy * (float)slice_rows;

    while(row < num_rows) {
        FirtreeCpuEngineRenderRequest* request =
            g_slice_new(FirtreeCpuEngineRenderRequest);

        request->func = func;
        request->buffer = buffer + (row_stride * row);
        request->row_width = row_width;
        if(row + slice_rows <= num_rows) {
            request->num_rows = slice_rows;
        } else {
            request->num_rows = num_rows - row;
        }
        request->row_stride = row_stride;

        request->extents[0] = extents[0];
        request->extents[1] = y;
        request->extents[2] = extents[2];
        request->extents[3] = dy * (float)(request->num_rows);

        g_ptr_array_add(request_array, request);

        g_thread_pool_push(p->thread_pool, request, NULL);

        row += slice_rows;
        y += slice_height;
    }

    /* wait for responses */
    while(request_array->len > 0)
    {
        gpointer finished_req = g_async_queue_pop(p->response_queue);

        /* look for request and remove it */
        if(!g_ptr_array_remove_fast(request_array, finished_req)) 
        {
            g_error("Unexpected response from worker thread.");
        }

        g_slice_free(FirtreeCpuEngineRenderRequest, finished_req);
    }

    g_ptr_array_free(request_array, TRUE);

    firtree_sampler_unlock(p->sampler);
}

#if FIRTREE_HAVE_GDK_PIXBUF

gboolean
firtree_cpu_engine_render_into_pixbuf (FirtreeCpuEngine* self,
        FirtreeVec4* extents, GdkPixbuf* pixbuf)
{
    FirtreeCpuEnginePrivate* p = GET_PRIVATE(self); 

    if(!extents) { return FALSE; }
    if(!pixbuf) { return FALSE; }

    guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
    guint width = gdk_pixbuf_get_width(pixbuf);
    guint height = gdk_pixbuf_get_height(pixbuf);
    guint stride = gdk_pixbuf_get_rowstride(pixbuf);
    guint channels = gdk_pixbuf_get_n_channels(pixbuf);

    if((channels != 4) && (channels != 3)) {
        g_debug("Only 3 and 4 channel GdkPixbufs supported.");
        return FALSE;
    }

    if(p->sampler == NULL) {
        return FALSE;
    }

    RenderFunc render = NULL;

    if(gdk_pixbuf_get_has_alpha(pixbuf)) {
        /* GdkPixbufs use non-premultiplied alpha. */
        render = (RenderFunc)firtree_cpu_engine_get_renderer_func(self, 
                RENDER_FUNC_NAME(FIRTREE_FORMAT_RGBA32));
    } else {
        /* Use the render function optimised for ignored alpha. */
        render = (RenderFunc)firtree_cpu_engine_get_renderer_func(self, 
                RENDER_FUNC_NAME(FIRTREE_FORMAT_RGB24));
    }

    if(!render) {
        return FALSE;
    }

    firtree_cpu_engine_perform_render(self, render,
            pixels, width, height, stride, (float*)extents);

    return TRUE;
}

#endif

#if FIRTREE_HAVE_CAIRO
gboolean 
firtree_cpu_engine_render_into_cairo_surface (FirtreeCpuEngine* self,
        FirtreeVec4* extents, cairo_surface_t* surface)
{
    if(!extents) { return FALSE; }
    if(!surface) { return FALSE; }

    guchar* data = cairo_image_surface_get_data(surface);
    if(!data) {
        g_debug("Surface is not an image surface with data.");
        return FALSE;
    }

    cairo_format_t format = cairo_image_surface_get_format(surface);
    guint width = cairo_image_surface_get_width(surface);
    guint height = cairo_image_surface_get_height(surface);
    guint stride = cairo_image_surface_get_stride(surface);

    RenderFunc render = NULL;

    switch(format) {
        case CAIRO_FORMAT_ARGB32:
            render = (RenderFunc)firtree_cpu_engine_get_renderer_func(self, 
                    RENDER_FUNC_NAME(FIRTREE_FORMAT_BGRA32_PREMULTIPLIED));
            break;
        case CAIRO_FORMAT_RGB24:
            render = (RenderFunc)firtree_cpu_engine_get_renderer_func(self, 
                    RENDER_FUNC_NAME(FIRTREE_FORMAT_BGRX32));
            break;
        default:
            g_debug("Invalid Cairo format.");
            return FALSE;
            break;
    }

    if(!render) {
        return FALSE;
    }

    firtree_cpu_engine_perform_render(self, render,
            data, width, height, stride, (float*)extents);

    return TRUE;
}
#endif

gboolean 
firtree_cpu_engine_render_into_buffer (FirtreeCpuEngine* self,
        FirtreeVec4* extents,
        gpointer buffer, guint width, guint height,
        guint stride, FirtreeBufferFormat format)
{
    g_assert(self);

    if(!buffer) { return FALSE; }

    RenderFunc render = NULL;

    switch(format) {
        case FIRTREE_FORMAT_ARGB32:
        case FIRTREE_FORMAT_ARGB32_PREMULTIPLIED:
        case FIRTREE_FORMAT_XRGB32:
        case FIRTREE_FORMAT_RGBA32:
        case FIRTREE_FORMAT_RGBA32_PREMULTIPLIED:
        case FIRTREE_FORMAT_ABGR32:
        case FIRTREE_FORMAT_ABGR32_PREMULTIPLIED:
        case FIRTREE_FORMAT_XBGR32:
        case FIRTREE_FORMAT_BGRA32:
        case FIRTREE_FORMAT_BGRA32_PREMULTIPLIED:
        case FIRTREE_FORMAT_RGB24:
        case FIRTREE_FORMAT_BGR24:
        case FIRTREE_FORMAT_RGBX32:
        case FIRTREE_FORMAT_BGRX32:
            render = (RenderFunc)firtree_cpu_engine_get_renderer_func(self, 
                    RENDER_FUNC_NAME(format));
            break;
        default:
            g_warning("Attempt to render to buffer in unsupported format.");
            return FALSE;
            break;
    }

    if(!render) {
        return FALSE;
    }

    firtree_cpu_engine_perform_render(self, render,
            (unsigned char*)buffer, width, height, stride, (float*)extents);

    return TRUE;
}

/* vim:sw=4:ts=4:et:cindent
 */
