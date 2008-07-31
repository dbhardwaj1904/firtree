#!/usr/bin/env python

# Import the Python OpenGL stuff
from OpenGL.GLUT import *
from OpenGL.GLU import *
from OpenGL.GL import *

#==============================================================================
# Use sys to find the script path and hence construct a path in which to 
# search for the Firtree bindings.

import math
import sys
import os
import gc

# Find the script path.
scriptDir = os.path.abspath(os.path.dirname(sys.argv[0]))

# Walk down three subdirs...
rootDir = os.path.dirname(os.path.dirname(os.path.dirname(scriptDir)))

# Add path to the python bindings
importDir = os.path.join(rootDir, 'bindings', 'python')

# Create path to the example images
imageDir = os.path.join(rootDir, 'artwork')

# Append this path to our search path & import the firtree bindings.
sys.path.insert(0, importDir)

import Firtree

class FirtreeScene:

    def init (self):
        # Set background colour & disable depth testing
        glClearColor(0, 0, 0, 1)
        glDisable(GL_DEPTH_TEST)

        # Setup an image mixing kernel
        mixKernel = Firtree.Kernel.CreateFromSource('''
        kernel vec4 mixKernel(sampler a, sampler b, float mix)
        {
            vec4 aColour = sample(a, samplerCoord(a));
            vec4 bColour = sample(b, samplerCoord(b));
            return mix(aColour, bColour, mix);
        }
        ''')
        mixImage = Firtree.Image.CreateFromKernel(mixKernel)

        # Load the firtree image.
        firtreeImage = Firtree.Image.CreateFromFile(
            os.path.join(imageDir, 'firtree-192x192.png'))
        self._firtree = firtreeImage

        # Form a translated and scaled firtree image.
        firtreeTransform = Firtree.AffineTransform.Translation(-96, -96)
        firtreeTransform.ScaleBy(1.25,1.25)
        firtreeTransImage = Firtree.Image.CreateFromImageWithTransform(
            firtreeImage, firtreeTransform)

        # Wire the original image into
        # the mix kernel.
        mixKernel.SetValueForKey(firtreeTransImage, 'b')

        self.mixKernel = mixKernel    
        self.image = mixImage

        # Create a rendering context.
        self.context = Firtree.GLRenderer.Create()

    def display (self, width, height):
        glClear(GL_COLOR_BUFFER_BIT)

        t = float(glutGet(GLUT_ELAPSED_TIME)) / 1000.0
        self.mixKernel.SetValueForKey(
            0.5 * (1.0 + math.sin(3.0 * t)), 'mix')

        firtreeTransform = Firtree.AffineTransform.Translation(-96, -96)
        firtreeTransform.ScaleBy(1.0 + 0.5 * (1.0 + math.sin(5.0 * t)))
        firtreeTransform.RotateByDegrees(30.0 * t)
        tempImage = Firtree.Image.CreateFromImageWithTransform(self._firtree, firtreeTransform)

        self.mixKernel.SetValueForKey(tempImage, 'a')

        # Render the composited image into the framebuffer.
        self.context.RenderWithOrigin(self.image,
            Firtree.Point2D(width/2,height/2))

    def clear_up (self):
        print('Clearing up...')

        self.mixKernel = None
        self.image = None
        self.context = None
        self._firtree = None
        
        # Sanity check to make sure that there are no objects left
        # dangling.
        gc.collect()
        globalObCount = Firtree.ReferenceCounted.GetGlobalObjectCount()
        print('Number of objects still allocated (should be zero): %i' 
            % globalObCount)

    def reshape (self, width, height):
        # Resize the OpenGL viewport
        glViewport(0, 0, width, height)
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        glOrtho(0.0, width, 0.0, height, -1.0, 1.0)
        glMatrixMode(GL_MODELVIEW)
        glLoadIdentity()
        
    def keypress(self, key):
        if(key == 'q'):
            print('Quitting...')
            quit()

def reshape(w, h):
    global winWidth, winHeight

    winWidth = w
    winHeight = h
    glscene.reshape(winWidth, winHeight)

def quit():
    glscene.clear_up()
    sys.exit()

def display():
    global shouldInit, shouldQuit

    if(shouldInit):
        glscene.init()
        shouldInit = False

    glscene.display(winWidth, winHeight)
    glutSwapBuffers()

def keypress(k, x, y):
    glscene.keypress(k)

def timer(val):
    glutSetWindow(window)
    glutPostRedisplay()
    glutTimerFunc(frameDelay, timer, val)

if __name__ == '__main__':
    glscene = FirtreeScene()

    shouldInit = True
    winWidth = winHeight = 0
    frameDelay = 1000/60

    glutInit(sys.argv)
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB)
    glutInitWindowSize(800, 600)
    window = glutCreateWindow("Firtree Demo")

    glutDisplayFunc(display)
    glutReshapeFunc(reshape)
    glutKeyboardFunc(keypress)
    
    glutTimerFunc(frameDelay, timer, 0)

    glutMainLoop()

# vim:sw=4:ts=4:et:autoindent