/* 
 * Copyright (C) 2001 Rich Wareham <richwareham@users.sourceforge.net>
 * 
 * libcga is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * libcga is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 */

#include "common.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include <compiler/include/main.h>
#include <compiler/include/platform.h>
#include <compiler/include/opengl.h>

#ifdef FIRTREE_WIN32
#   include <wand/MagickWand.h>
#else
#   include <wand/magick-wand.h>
#endif

/** 
 * This file contains the common routines used by (almost) all of the test
 * programs. 
 **/

/** GLOBALS **/

static int rotate = 0; /* Rotate the camera? */
static int var_epoch = 1; /* Vary epoch count depending on actual
			     framerate? */
static int delay = 1;  /* Introduce a delay to keep constant FPS? */

#define DEFAULT_FPS 60

int mainWindow;

static int glutWin;
static int frame = 0, tickCount, timebase = 0;
static int last_frame_time = 0;
static float angle = 0.0;
int fullscreen = 0;
int paused = 0;
static int antialias = 0;

static int do_dump = 0;

static int called_created_cb = 0;

static int angle_dragging = 0;
static int axis_dragging = 0;
static float start_trans = 0;
static float translate = 0;
static float start_trans2 = 0;
static float translate2 = 0;
static float angle_x = 0;
static float angle_y = -90;
static float startx, starty, startanglex, startangley;

static int rendering = 0;
int width = 0, height = 0;

void display_cb();
void reshape_cb(int w, int h);
void keyboard_cb(unsigned char key, int x, int y);
void special_cb(int key, int x, int y);
void timer_cb(int value);
void mouse_cb(int button, int state, int x, int y);
void motion_cb(int x, int y);

void init_cb(); 

// ============================================================================
bool InitialiseTextureFromFile(unsigned int texObj, const char* pFileName)
{
    static bool calledGenesis = false;
    GLenum err;
    
    if(!calledGenesis)
    {
        MagickWandGenesis();
        calledGenesis = true;
    }

    MagickWand* wand = NewMagickWand();
    MagickBooleanType status = MagickReadImage(wand, pFileName);
    if(status == MagickFalse)
    {
        return false;
    }

    MagickFlipImage(wand);

    unsigned int w = MagickGetImageWidth(wand);
    unsigned int h = MagickGetImageHeight(wand);

    bool hasMatte = true;

    glBindTexture(GL_TEXTURE_2D, texObj);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    if((err == glGetError() != GL_NO_ERROR))
    {
	FIRTREE_DEBUG("GL Error: %s", gluErrorString(err));
        return false;
    }

    unsigned char* image = new unsigned char[w * h * 4];

    PixelIterator* pixit = NewPixelIterator(wand);
    if(pixit == NULL)
    {
        wand = DestroyMagickWand(wand);
        MagickWandTerminus();
        return false;
    }

    unsigned char* curPixel = image;
    for(unsigned int y=0; y<h; y++)
    {
        long unsigned int rowWidth;
        PixelWand** pixels = PixelGetNextIteratorRow(pixit, &rowWidth);
        if(pixels == NULL)
        {
            pixit = DestroyPixelIterator(pixit);
            wand = DestroyMagickWand(wand);
            MagickWandTerminus();
            return false;
        }
        assert(rowWidth == w);

        for(unsigned int x=0; x<rowWidth; x++)
        {
            // Convert the image to pre-multiplied alpha.
            
            float alpha = hasMatte ? PixelGetAlpha(pixels[x]) : 1.f;
            curPixel[0] = (unsigned char)(255.0 * alpha * PixelGetRed(pixels[x]));
            curPixel[1] = (unsigned char)(255.0 * alpha * PixelGetGreen(pixels[x]));
            curPixel[2] = (unsigned char)(255.0 * alpha * PixelGetBlue(pixels[x]));
            curPixel[3] = (unsigned char)(255.0 * alpha);

            curPixel += 4;
        }
    }
    
    pixit = DestroyPixelIterator(pixit);

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, image);

    delete [] image;

    wand = DestroyMagickWand(wand);

    //MagickWandTerminus();

    if((err == glGetError() != GL_NO_ERROR))
    {
	FIRTREE_DEBUG("GL Error: %s", gluErrorString(err));
        return false;
    }

    return true;
}

void idle_cb() {
  int dt;
  
  glutSetWindow(glutWin);
  tickCount = glutGet(GLUT_ELAPSED_TIME);
  dt = tickCount - last_frame_time;
 
  if(tickCount - timebase > 1000) {
    float fps = frame * 1000.0 / (tickCount-timebase);
    timebase = tickCount;
    frame = 0;
    
    printf("FPS = %.5f\n",fps); 
  }
    
  if(!paused) {
    if(var_epoch) {
      angle += ((float)dt) / 20.0;
    } else {
      angle += 2.5;
    }
  }
    
  display_cb();
  
  frame ++;
 
  last_frame_time = tickCount;
}

void timer_cb(int value) {
  glutTimerFunc(1000 / (DEFAULT_FPS), timer_cb, 1);

  idle_cb();
}

void display_cb() {
  if(!called_created_cb)
  {
    context_created();
    called_created_cb = 1;
  }

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glPushMatrix();

  rendering = 1;
  render(angle);
  rendering = 0;
 
  glPopMatrix();

  glutSwapBuffers();
}

void reshape_cb(int w, int h) {
  float ratio;
  int tx, ty, tw, th;

  width = w;
  height = h;
   
  /* Reset the coordinate system before modifying */
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
    
  glViewport(0,0,w,h);
      
  /* Prevent a divide by zero, when window is too short
   * (you cant make a window of zero width). */
  if(th == 0) { th = 1; }

  ratio = (float)(tw) / th;

  /* Set the correct perspective. */
  gluOrtho2D(0,w,0,h);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

void keyboard_cb(unsigned char key, int x, int y) {
  if(key == 'q') 
   exit(0);

  if(key == 'v') {
    var_epoch = !var_epoch;
  }

  if(key == 'f') {
    fullscreen = !fullscreen;

    if(fullscreen) {
      glutFullScreen();
/*      glutGameModeString("800x600");

      glutEnterGameMode();
      glutSetCursor(GLUT_CURSOR_NONE);
      init_cb(); */
    } else {
      glutReshapeWindow(640,480);
      /*
      glutLeaveGameMode();
      glutSetCursor(GLUT_CURSOR_INHERIT);
      */
    }
  }

  if(key == 'p') {
    paused = !paused;
  }

  if(key == 'z') {
    glutReshapeWindow(640,480);
  }
}

void special_cb(int key, int x, int y) {
}

void init_cb() {
  // glutDisplayFunc(display_cb);
  // glutReshapeFunc(reshape_cb);
  glutKeyboardFunc(keyboard_cb);
  glutSpecialFunc(special_cb);
  //glutMouseFunc(mouse_cb);
  //glutMotionFunc(motion_cb);
}

int main(int argc, char** argv) {
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
  glutInitWindowSize(640,480);
  glutWin = glutCreateWindow("firtree demo");

  mainWindow = glutWin;
  
  glutTimerFunc(1000 / (DEFAULT_FPS), timer_cb, 1);
  //GLUI_Master.set_glutIdleFunc(idle_cb);
  glutDisplayFunc(display_cb);
  glutReshapeFunc(reshape_cb);
  
  /* glShadeModel(GL_FLAT); */
  glClearColor(0.0f,0.0f,0.0f,0.5f);
  
  init_cb(); 
  initialise_test(&argc, argv, glutWin);

  glutMainLoop();

  finalise_test();
  
  return 0;
}

// vim:cindent:sw=4:ts=4:et
