/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkTkImageViewerWidget.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include <stdlib.h>

#include "vtkTkImageViewerWidget.h"
#include "vtkRenderWindowInteractor.h"

// This widget requires access to structures that are normally 
// not visible to Tcl/Tk applications. For this reason you must
// have access to tkInt.h
// #include "tkInt.h"
#ifdef _WIN32
extern "C"
{
#include "tkWinInt.h" 
}
#endif

#ifdef _MSC_VER
 #pragma warning ( disable : 4273 )
#else
 #ifdef VTK_USE_CARBON
  #include "vtkCarbonRenderWindow.h"
  #include "tkMacOSXInt.h"
 #else
  #ifdef VTK_USE_COCOA
   #include "vtkCocoaRenderWindow.h"
  #else
   #include "vtkXOpenGLRenderWindow.h"
  #endif
 #endif
#endif

#define VTK_ALL_EVENTS_MASK \
    KeyPressMask|KeyReleaseMask|ButtonPressMask|ButtonReleaseMask|      \
    EnterWindowMask|LeaveWindowMask|PointerMotionMask|ExposureMask|     \
    VisibilityChangeMask|FocusChangeMask|PropertyChangeMask|ColormapChangeMask

#define VTK_MAX(a,b)    (((a)>(b))?(a):(b))
    
#if ( _MSC_VER >= 1300 ) // Visual studio .NET
#pragma warning ( disable : 4311 )
#pragma warning ( disable : 4312 )
#  define vtkGetWindowLong GetWindowLongPtr
#  define vtkSetWindowLong SetWindowLongPtr
#else // regular Visual studio 
#  define vtkGetWindowLong GetWindowLong
#  define vtkSetWindowLong SetWindowLong
#endif // 

// These are the options that can be set when the widget is created
// or with the command configure.  The only new one is "-rw" which allows
// the uses to set their own ImageViewer window.
static Tk_ConfigSpec vtkTkImageViewerWidgetConfigSpecs[] = {
    {TK_CONFIG_PIXELS, (char *) "-height", (char *) "height", (char *) "Height",
     (char *) "400", Tk_Offset(struct vtkTkImageViewerWidget, Height), 0, NULL},
  
    {TK_CONFIG_PIXELS, (char *) "-width", (char *) "width", (char *) "Width",
     (char *) "400", Tk_Offset(struct vtkTkImageViewerWidget, Width), 0, NULL},
  
    {TK_CONFIG_STRING, (char *) "-iv", (char *) "iv", (char *) "IV",
     (char *) "", Tk_Offset(struct vtkTkImageViewerWidget, IV), 0, NULL},

    {TK_CONFIG_END, (char *) NULL, (char *) NULL, (char *) NULL,
     (char *) NULL, 0, 0, NULL}
};


// Forward prototypes
extern "C"
{
  void vtkTkImageViewerWidget_EventProc(ClientData clientData, 
                                        XEvent *eventPtr);
}

static int vtkTkImageViewerWidget_MakeImageViewer(struct vtkTkImageViewerWidget *self);
extern int vtkImageViewerCommand(ClientData cd, Tcl_Interp *interp,
                                 int argc, char *argv[]);

    
//----------------------------------------------------------------------------
// It's possible to change with this function or in a script some
// options like width, hieght or the ImageViewer widget.
int vtkTkImageViewerWidget_Configure(Tcl_Interp *interp, 
                                     struct vtkTkImageViewerWidget *self,
                                     int argc, char *argv[], int flags) 
{
  // Let Tk handle generic configure options.
  if (Tk_ConfigureWidget(interp, 
                         self->TkWin, 
                         vtkTkImageViewerWidgetConfigSpecs,
                         argc, 
#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4 && TCL_RELEASE_LEVEL >= TCL_FINAL_RELEASE)
                         const_cast<CONST84 char **>(argv), 
#else
                         argv, 
#endif
                         (char *)self, 
                         flags) == TCL_ERROR) 
    {
    return(TCL_ERROR);
    }

  // Get the new  width and height of the widget
  Tk_GeometryRequest(self->TkWin, self->Width, self->Height);
  
  // Make sure the ImageViewer window has been set.  If not, create one.
  if (vtkTkImageViewerWidget_MakeImageViewer(self) == TCL_ERROR) 
    {
    return TCL_ERROR;
    }
  
  return TCL_OK;
}

//----------------------------------------------------------------------------
// This function is called when the ImageViewer widget name is 
// evaluated in a Tcl script.  It will compare string parameters
// to choose the appropriate method to invoke.
extern "C"
{
  int vtkTkImageViewerWidget_Widget(ClientData clientData, 
                                    Tcl_Interp *interp,
                                    int argc, 
#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4 && TCL_RELEASE_LEVEL >= TCL_FINAL_RELEASE)
                                    CONST84
#endif
                                    char *argv[]) 
  {
    struct vtkTkImageViewerWidget *self = 
      (struct vtkTkImageViewerWidget *)clientData;
    int result = TCL_OK;
    
    // Check to see if the command has enough arguments.
    if (argc < 2) 
      {
      Tcl_AppendResult(interp, "wrong # args: should be \"",
                       argv[0], " ?options?\"", NULL);
      return TCL_ERROR;
      }
    
    // Make sure the widget is not deleted during this function
    Tk_Preserve((ClientData)self);
    
    
    // Handle render call to the widget
    if (strncmp(argv[1], "render", VTK_MAX(1, strlen(argv[1]))) == 0 || 
        strncmp(argv[1], "Render", VTK_MAX(1, strlen(argv[1]))) == 0) 
      {
      // make sure we have a window
      if (self->ImageViewer == NULL)
        {
        vtkTkImageViewerWidget_MakeImageViewer(self);
        }
      self->ImageViewer->Render();
      }
    // Handle configure method
    else if (!strncmp(argv[1], "configure", VTK_MAX(1, strlen(argv[1])))) 
      {
      if (argc == 2) 
        {
        /* Return list of all configuration parameters */
        result = Tk_ConfigureInfo(interp, self->TkWin, 
                                  vtkTkImageViewerWidgetConfigSpecs,
                                  (char *)self, (char *)NULL, 0);
        }
      else if (argc == 3) 
        {
        /* Return a specific configuration parameter */
        result = Tk_ConfigureInfo(interp, self->TkWin, 
                                  vtkTkImageViewerWidgetConfigSpecs,
                                  (char *)self, argv[2], 0);
        }
      else 
        {
        /* Execute a configuration change */
        result = vtkTkImageViewerWidget_Configure(interp, 
                                                  self, 
                                                  argc-2, 
#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4 && TCL_RELEASE_LEVEL >= TCL_FINAL_RELEASE)
                                                  const_cast<char **>(argv+2), 
#else
                                                  argv+2, 
#endif
                                                  TK_CONFIG_ARGV_ONLY);
        }
      }
    else if (!strcmp(argv[1], "GetImageViewer"))
      { // Get ImageViewerWindow is my own method
      // Create a ImageViewerWidget if one has not been set yet.
      result = vtkTkImageViewerWidget_MakeImageViewer(self);
      if (result != TCL_ERROR)
        {
        // Return the name (Make Tcl copy the string)
        Tcl_SetResult(interp, self->IV, TCL_VOLATILE);
        }
      }
    else 
      {
      // Unknown method name.
      Tcl_AppendResult(interp, "vtkTkImageViewerWidget: Unknown option: ", argv[1], 
                       "\n", "Try: configure or GetImageViewer\n", NULL);
      result = TCL_ERROR;
      }
    
    // Unlock the object so it can be deleted.
    Tk_Release((ClientData)self);
    return result;
  }
}

//----------------------------------------------------------------------------
// vtkTkImageViewerWidget_Cmd
// Called when vtkTkImageViewerWidget is executed 
// - creation of a vtkTkImageViewerWidget widget.
//     * Creates a new window
//     * Creates an 'vtkTkImageViewerWidget' data structure
//     * Creates an event handler for this window
//     * Creates a command that handles this object
//     * Configures this vtkTkImageViewerWidget for the given arguments
extern "C"
{
  int vtkTkImageViewerWidget_Cmd(ClientData clientData, 
                                 Tcl_Interp *interp, 
                                 int argc, 
#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4 && TCL_RELEASE_LEVEL >= TCL_FINAL_RELEASE)
                                 CONST84
#endif
                                 char **argv)
  {
#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4 && TCL_RELEASE_LEVEL >= TCL_FINAL_RELEASE)
    CONST84
#endif
    char *name;
    Tk_Window main = (Tk_Window)clientData;
    Tk_Window tkwin;
    struct vtkTkImageViewerWidget *self;
    
    // Make sure we have an instance name.
    if (argc <= 1) 
      {
      Tcl_ResetResult(interp);
      Tcl_AppendResult(interp, 
                       "wrong # args: should be \"pathName read filename\"", 
                       NULL);
      return(TCL_ERROR);
      }
    
    // Create the window.
    name = argv[1];
    // Possibly X dependent
    tkwin = Tk_CreateWindowFromPath(interp, main, name, (char *) NULL);
    if (tkwin == NULL) 
      {
      return TCL_ERROR;
      }
    
    // Tcl needs this for setting options and matching event bindings.
    Tk_SetClass(tkwin, (char *) "vtkTkImageViewerWidget");
    
    // Create vtkTkImageViewerWidget data structure 
    self = (struct vtkTkImageViewerWidget *)
      ckalloc(sizeof(struct vtkTkImageViewerWidget));
    
    self->TkWin = tkwin;
    self->Interp = interp;
    self->Width = 0;
    self->Height = 0;
    self->ImageViewer = NULL;
    self->IV = NULL;
    
    // ...
    // Create command event handler
    Tcl_CreateCommand(interp, Tk_PathName(tkwin), vtkTkImageViewerWidget_Widget, 
                      (ClientData)self, (void (*)(ClientData)) NULL);
    Tk_CreateEventHandler(tkwin, ExposureMask | StructureNotifyMask,
                          vtkTkImageViewerWidget_EventProc, (ClientData)self);
    
    // Configure vtkTkImageViewerWidget widget
    if (vtkTkImageViewerWidget_Configure(interp, 
                                         self, 
                                         argc-2, 
#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4 && TCL_RELEASE_LEVEL >= TCL_FINAL_RELEASE)
                                         const_cast<char **>(argv+2), 
#else
                                         argv+2, 
#endif
                                         0) 
        == TCL_ERROR) 
      {
      Tk_DestroyWindow(tkwin);
      Tcl_DeleteCommand(interp, (char *) "vtkTkImageViewerWidget");
      // Don't free it, if we do a crash occurs later...
      //free(self);  
      return TCL_ERROR;
      }
    
    Tcl_AppendResult(interp, Tk_PathName(tkwin), NULL);
    return TCL_OK;
  }
}


//----------------------------------------------------------------------------
char *vtkTkImageViewerWidget_IV(const struct vtkTkImageViewerWidget *self)
{
  return self->IV;
}


//----------------------------------------------------------------------------
int vtkTkImageViewerWidget_Width( const struct vtkTkImageViewerWidget *self)
{
   return self->Width;
}


//----------------------------------------------------------------------------
int vtkTkImageViewerWidget_Height( const struct vtkTkImageViewerWidget *self)
{
   return self->Height;
}

extern "C"
{
  void vtkTkImageViewerWidget_Destroy(char *memPtr)
  {
    struct vtkTkImageViewerWidget *self = (struct vtkTkImageViewerWidget *)memPtr;
    
    if (self->ImageViewer)
      {
      int netRefCount = 0;
      netRefCount =  self->ImageViewer->GetReferenceCount();
      if (self->ImageViewer->GetRenderWindow()->GetInteractor() && 
          self->ImageViewer->GetRenderWindow()->GetInteractor()->GetRenderWindow() == self->ImageViewer->GetRenderWindow() &&
          self->ImageViewer->GetRenderWindow()->GetInteractor()->GetReferenceCount() == 1)
        {
        netRefCount = netRefCount - 1;
        }
      if (netRefCount > 1)
        {
        vtkGenericWarningMacro("A TkImageViewerWidget is being destroyed before it associated vtkImageViewer is destroyed. This is very bad and usually due to the order in which objects are being destroyed. Always destroy the vtkImageViewer before destroying the user interface components.");
        return;
        }
      // Squash the ImageViewer's WindowID
      self->ImageViewer->SetWindowId ( (void*)NULL );
      self->ImageViewer->UnRegister(NULL);
      self->ImageViewer = NULL;
      ckfree (self->IV);
      }
    ckfree((char *) memPtr);
  }
}

//----------------------------------------------------------------------------
// This gets called to handle vtkTkImageViewerWidget wind configuration events
// Possibly X dependent
extern "C"
{
  void vtkTkImageViewerWidget_EventProc(ClientData clientData, 
                                        XEvent *eventPtr) 
  {
    struct vtkTkImageViewerWidget *self = 
      (struct vtkTkImageViewerWidget *)clientData;
    
    switch (eventPtr->type) 
      {
      case Expose:
        if ((eventPtr->xexpose.count == 0)
            /* && !self->UpdatePending*/) 
          {
          // bid this in tcl now
          //self->ImageViewer->Render();
          }
        break;
      case ConfigureNotify:
        if ( 1 /*Tk_IsMapped(self->TkWin)*/ ) 
          {
          self->Width = Tk_Width(self->TkWin);
          self->Height = Tk_Height(self->TkWin);
          //Tk_GeometryRequest(self->TkWin,self->Width,self->Height);
          if (self->ImageViewer)
            {
#ifdef VTK_USE_CARBON
            TkWindow *winPtr = (TkWindow *)self->TkWin;
            self->ImageViewer->SetPosition(winPtr->privatePtr->xOff,
                                           winPtr->privatePtr->yOff);
#else
            self->ImageViewer->SetPosition(Tk_X(self->TkWin),Tk_Y(self->TkWin));
#endif
            self->ImageViewer->SetSize(self->Width, self->Height);
            }
          
          //vtkTkImageViewerWidget_PostRedisplay(self);
          }
        break;
      case MapNotify:
        break;
      case DestroyNotify:
#ifdef _WIN32
        if (self->ImageViewer->GetRenderWindow()->GetGenericWindowId())
          {
          SetWindowLong((HWND)self->ImageViewer->GetRenderWindow()->GetGenericWindowId(),
                        GWL_USERDATA,(LONG)((TkWindow *)self->TkWin)->window);
          SetWindowLong((HWND)self->ImageViewer->GetRenderWindow()->GetGenericWindowId(),
                        GWL_WNDPROC,(LONG)TkWinChildProc);
          }
#endif
        Tcl_EventuallyFree( (ClientData) self, vtkTkImageViewerWidget_Destroy );
        break;
      default:
        // nothing
        ;
      }
  }
}



//----------------------------------------------------------------------------
// vtkTkImageViewerWidget_Init
// Called upon system startup to create vtkTkImageViewerWidget command.
extern "C" {VTK_TK_EXPORT int Vtktkimageviewerwidget_Init(Tcl_Interp *interp);}
int Vtktkimageviewerwidget_Init(Tcl_Interp *interp)
{
  if (Tcl_PkgProvide(interp, (char *) "Vtktkimageviewerwidget", (char *) "1.2") != TCL_OK) 
    {
    return TCL_ERROR;
    }
  
  Tcl_CreateCommand(interp, (char *) "vtkTkImageViewerWidget", 
                    vtkTkImageViewerWidget_Cmd, 
                    Tk_MainWindow(interp), NULL);
  
  return TCL_OK;
}


// Here is the windows specific code for creating the window
// The Xwindows version follows after this
#ifdef _WIN32

LRESULT APIENTRY vtkTkImageViewerWidgetProc(HWND hWnd, UINT message, 
                                            WPARAM wParam, LPARAM lParam)
{
  LRESULT rval;
  struct vtkTkImageViewerWidget *self = 
    (struct vtkTkImageViewerWidget *)vtkGetWindowLong(hWnd,GWL_USERDATA);
  
  if (!self)
    {
    return 0;
    }

  // forward message to Tk handler
  vtkSetWindowLong(hWnd,GWL_USERDATA,(LONG)((TkWindow *)self->TkWin)->window);
  if (((TkWindow *)self->TkWin)->parentPtr)
    {
    vtkSetWindowLong(hWnd,GWL_WNDPROC,(LONG)TkWinChildProc);
    rval = TkWinChildProc(hWnd,message,wParam,lParam);
    }
  else
    {
//
// TkWinTopLevelProc has been deprecated in Tcl/Tk8.0.  Not sure how 
// well this will actually work in 8.0.
//
#if (TK_MAJOR_VERSION < 8)
    vtkSetWindowLong(hWnd,GWL_WNDPROC,(LONG)TkWinTopLevelProc);
    rval = TkWinTopLevelProc(hWnd,message,wParam,lParam);
#else
    if (message == WM_WINDOWPOSCHANGED) 
      {
      XEvent event;
            WINDOWPOS *pos = (WINDOWPOS *) lParam;
            TkWindow *winPtr = (TkWindow *) Tk_HWNDToWindow(pos->hwnd);
    
            if (winPtr == NULL) {
              return 0;
              }

            /*
             * Update the shape of the contained window.
             */
            if (!(pos->flags & SWP_NOSIZE)) {
              winPtr->changes.width = pos->cx;
              winPtr->changes.height = pos->cy;
              }
            if (!(pos->flags & SWP_NOMOVE)) {
              winPtr->changes.x = pos->x;
              winPtr->changes.y = pos->y;
              }


      /*
       *  Generate a ConfigureNotify event.
       */
      event.type = ConfigureNotify;
      event.xconfigure.serial = winPtr->display->request;
      event.xconfigure.send_event = False;
      event.xconfigure.display = winPtr->display;
      event.xconfigure.event = winPtr->window;
      event.xconfigure.window = winPtr->window;
      event.xconfigure.border_width = winPtr->changes.border_width;
      event.xconfigure.override_redirect = winPtr->atts.override_redirect;
      event.xconfigure.x = winPtr->changes.x;
      event.xconfigure.y = winPtr->changes.y;
      event.xconfigure.width = winPtr->changes.width;
      event.xconfigure.height = winPtr->changes.height;
      event.xconfigure.above = None;
      Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

            Tcl_ServiceAll();
            return 0;
      }
    vtkSetWindowLong(hWnd,GWL_WNDPROC,(LONG)TkWinChildProc);
    rval = TkWinChildProc(hWnd,message,wParam,lParam);
#endif
    }

    if (message != WM_PAINT)
      {
      if (self->ImageViewer)
        {
        vtkSetWindowLong(hWnd,GWL_USERDATA,
                         (LONG)self->ImageViewer->GetRenderWindow());
        vtkSetWindowLong(hWnd,GWL_WNDPROC,(LONG)self->OldProc);
        CallWindowProc(self->OldProc,hWnd,message,wParam,lParam);
        }
      }

    // now reset to the original config
    vtkSetWindowLong(hWnd,GWL_USERDATA,(LONG)self);
    vtkSetWindowLong(hWnd,GWL_WNDPROC,(LONG)vtkTkImageViewerWidgetProc);
    return rval;
}

//-----------------------------------------------------------------------------
// Creates a ImageViewer window and forces Tk to use the window.
static int vtkTkImageViewerWidget_MakeImageViewer(struct vtkTkImageViewerWidget *self) 
{
  Display *dpy;
  TkWindow *winPtr = (TkWindow *) self->TkWin;
  Tcl_HashEntry *hPtr;
  int new_flag;
  vtkImageViewer *ImageViewer = NULL;
  TkWinDrawable *twdPtr;
  HWND parentWin;
  vtkRenderWindow *ImageWindow;

  if (self->ImageViewer)
    {
    return TCL_OK;
    }

  dpy = Tk_Display(self->TkWin);
  
  if (winPtr->window != None) 
    {
    // XDestroyWindow(dpy, winPtr->window);
    }

  if (self->IV[0] == '\0')
    {
    // Make the ImageViewer window.
    self->ImageViewer = vtkImageViewer::New();
    ImageViewer = (vtkImageViewer *)(self->ImageViewer);
#ifndef VTK_PYTHON_BUILD
    vtkTclGetObjectFromPointer(self->Interp, self->ImageViewer,
                               vtkImageViewerCommand);
#endif
    ckfree (self->IV);
    self->IV = strdup(self->Interp->result);
    self->Interp->result[0] = '\0';
    }
  else
    {
    // is IV an address ? big ole python hack here
    if (self->IV[0] == 'A' && self->IV[1] == 'd' && 
        self->IV[2] == 'd' && self->IV[3] == 'r')
      {
      void *tmp;
      sscanf(self->IV+5,"%p",&tmp);
      ImageViewer = (vtkImageViewer *)tmp;
      }
    else
      {
#ifndef VTK_PYTHON_BUILD
      ImageViewer = (vtkImageViewer *)
        vtkTclGetPointerFromObject(self->IV, "vtkImageViewer", self->Interp,
                                   new_flag);
#endif
      }
    if (ImageViewer != self->ImageViewer)
      {
      if (self->ImageViewer != NULL)
        {
        self->ImageViewer->UnRegister(NULL);
        }
      self->ImageViewer = (vtkImageViewer *)(ImageViewer);
      if (self->ImageViewer != NULL)
        {
        self->ImageViewer->Register(NULL);
        }
      }
    }
  
  // Set the size
  self->ImageViewer->SetSize(self->Width, self->Height);
  
  // Set the parent correctly
  // Possibly X dependent
  if ((winPtr->parentPtr != NULL) && !(winPtr->flags & TK_TOP_LEVEL)) 
    {
    if (winPtr->parentPtr->window == None) 
      {
      Tk_MakeWindowExist((Tk_Window) winPtr->parentPtr);
      }

    parentWin = ((TkWinDrawable *)winPtr->parentPtr->window)->window.handle;
    ImageViewer->SetParentId(parentWin);
    }
  
  // Use the same display
  self->ImageViewer->SetDisplayId(dpy);
  
  /* Make sure Tk knows to switch to the new colormap when the cursor
   * is over this window when running in color index mode.
   */
  //Tk_SetWindowVisual(self->TkWin, ImageViewer->GetDesiredVisual(), 
  //ImageViewer->GetDesiredDepth(), 
  //ImageViewer->GetDesiredColormap());
  
  self->ImageViewer->Render();  
  ImageWindow = self->ImageViewer->GetRenderWindow();

#if(TK_MAJOR_VERSION >=  8)
  twdPtr = (TkWinDrawable*)Tk_AttachHWND(self->TkWin, (HWND)ImageWindow->GetGenericWindowId());
#else
  twdPtr = (TkWinDrawable*) ckalloc(sizeof(TkWinDrawable));
  twdPtr->type = TWD_WINDOW;
  twdPtr->window.winPtr = winPtr;
  twdPtr->window.handle = (HWND)ImageWindow->GetGenericWindowId();
#endif
  
  self->OldProc = (WNDPROC)vtkGetWindowLong(twdPtr->window.handle,GWL_WNDPROC);
  vtkSetWindowLong(twdPtr->window.handle,GWL_USERDATA,(LONG)self);
  vtkSetWindowLong(twdPtr->window.handle,GWL_WNDPROC,
                   (LONG)vtkTkImageViewerWidgetProc);

  winPtr->window = (Window)twdPtr;
  
  hPtr = Tcl_CreateHashEntry(&winPtr->dispPtr->winTable,
                             (char *) winPtr->window, &new_flag);
  Tcl_SetHashValue(hPtr, winPtr);
  
  winPtr->dirtyAtts = 0;
  winPtr->dirtyChanges = 0;
#ifdef TK_USE_INPUT_METHODS
  winPtr->inputContext = NULL;
#endif // TK_USE_INPUT_METHODS 

  if (!(winPtr->flags & TK_TOP_LEVEL)) 
    {
    /*
     * If this window has a different colormap than its parent, add
     * the window to the WM_COLORMAP_WINDOWS property for its top-level.
     */
    if ((winPtr->parentPtr != NULL) &&
              (winPtr->atts.colormap != winPtr->parentPtr->atts.colormap)) 
      {
      TkWmAddToColormapWindows(winPtr);
      }
    } 

  /*
   * Issue a ConfigureNotify event if there were deferred configuration
   * changes (but skip it if the window is being deleted;  the
   * ConfigureNotify event could cause problems if we're being called
   * from Tk_DestroyWindow under some conditions).
   */
  if ((winPtr->flags & TK_NEED_CONFIG_NOTIFY)
      && !(winPtr->flags & TK_ALREADY_DEAD))
    {
    XEvent event;
    
    winPtr->flags &= ~TK_NEED_CONFIG_NOTIFY;
    
    event.type = ConfigureNotify;
    event.xconfigure.serial = LastKnownRequestProcessed(winPtr->display);
    event.xconfigure.send_event = False;
    event.xconfigure.display = winPtr->display;
    event.xconfigure.event = winPtr->window;
    event.xconfigure.window = winPtr->window;
    event.xconfigure.x = winPtr->changes.x;
    event.xconfigure.y = winPtr->changes.y;
    event.xconfigure.width = winPtr->changes.width;
    event.xconfigure.height = winPtr->changes.height;
    event.xconfigure.border_width = winPtr->changes.border_width;
    if (winPtr->changes.stack_mode == Above) 
      {
      event.xconfigure.above = winPtr->changes.sibling;
      }
    else 
      {
      event.xconfigure.above = None;
      }
    event.xconfigure.override_redirect = winPtr->atts.override_redirect;
    Tk_HandleEvent(&event);
    }

  return TCL_OK;
}

// now the APPLE version - only available using the Carbon APIs
#else
#ifdef VTK_USE_CARBON
//----------------------------------------------------------------------------
// Creates a ImageViewer window and forces Tk to use the window.
static int
vtkTkImageViewerWidget_MakeImageViewer(struct vtkTkImageViewerWidget *self) 
{
  Display *dpy;
  TkWindow *winPtr = (TkWindow *)self->TkWin;
  vtkImageViewer *ImageViewer;
  vtkCarbonRenderWindow *ImageWindow;
  WindowPtr parentWin;
  
  if (self->ImageViewer)
    {
    return TCL_OK;
    }

  dpy = Tk_Display(self->TkWin);
  
  if (self->IV[0] == '\0')
    {
    // Make the ImageViewer window.
    self->ImageViewer = vtkImageViewer::New();
    ImageViewer = self->ImageViewer;
#ifndef VTK_PYTHON_BUILD
    vtkTclGetObjectFromPointer(self->Interp, self->ImageViewer,
                               vtkImageViewerCommand);
#endif
    ckfree (self->IV);
    self->IV = strdup(self->Interp->result);
    self->Interp->result[0] = '\0';
    }
  else
    {
    // is IV an address ? big ole python hack here
    if (self->IV[0] == 'A' && self->IV[1] == 'd' && 
        self->IV[2] == 'd' && self->IV[3] == 'r')
      {
      void *tmp;
      sscanf(self->IV+5,"%p",&tmp);
      ImageViewer = (vtkImageViewer *)tmp;
      }
    else
      {
#ifndef VTK_PYTHON_BUILD
      int new_flag;
      ImageViewer = (vtkImageViewer *)
        vtkTclGetPointerFromObject(self->IV, "vtkImageViewer", self->Interp,
                                   new_flag);
#endif
      }
    if (ImageViewer != self->ImageViewer)
      {
      if (self->ImageViewer != NULL)
        {
        self->ImageViewer->UnRegister(NULL);
        }
      self->ImageViewer = (vtkImageViewer *)(ImageViewer);
      if (self->ImageViewer != NULL)
        {
        self->ImageViewer->Register(NULL);
        }
      }
    }
  
        
  // get the window
  ImageWindow = static_cast<vtkCarbonRenderWindow *>(ImageViewer->GetRenderWindow());
  // If the imageviewer has already created it's window, throw up our hands and quit...
  if ( ImageWindow->GetWindowId() != (Window)NULL )
    {
    return TCL_ERROR;
    }
        
  // Use the same display
  ImageWindow->SetDisplayId(dpy);

  // Set the parent correctly and get the actual OSX window on the screen
  // Window must be up so that the aglContext can be attached to it
  if ((winPtr->parentPtr != NULL) && !(winPtr->flags & TK_TOP_LEVEL))
    {
      if (winPtr->parentPtr->window == None)
        {
        // Look at each parent TK window in order until we run out
        // of windows or find the top level. Then the OSX window that will be
        // the parent is created so that we have a window to pass to the 
        // vtkRenderWindow so it can attach its openGL context.
        // Ideally the Tk_MakeWindowExist call would do the deed. (I think)
        TkWindow *curWin = winPtr->parentPtr;
        while ((NULL != curWin->parentPtr) && !(curWin->flags & TK_TOP_LEVEL))
          {
          curWin = curWin->parentPtr;
          }
        Tk_MakeWindowExist((Tk_Window) winPtr->parentPtr);
        if (NULL != curWin)
          {
          TkMacOSXMakeRealWindowExist(curWin);
          }
        else
          {
          vtkGenericWarningMacro("Could not find the TK_TOP_LEVEL. This is bad.");
          }
        }

      parentWin = GetWindowFromPort(TkMacOSXGetDrawablePort(
                                    Tk_WindowId(winPtr->parentPtr)));
      // Carbon does not have 'sub-windows', so the ParentId is used more
      // as a flag to indicate that the renderwindow is being used as a sub-
      // view of its 'parent' window.
      ImageWindow->SetParentId(parentWin);
      ImageWindow->SetWindowId(parentWin);
    }


  // Set the size
  self->ImageViewer->SetSize(self->Width, self->Height);


  self->ImageViewer->Render();          
  return TCL_OK;
}


// now the Xwindows version
#else

//----------------------------------------------------------------------------
// Creates a ImageViewer window and forces Tk to use the window.
static int
vtkTkImageViewerWidget_MakeImageViewer(struct vtkTkImageViewerWidget *self) 
{
  Display *dpy;
  vtkImageViewer *ImageViewer = 0;
  vtkXOpenGLRenderWindow *ImageWindow;
  
  if (self->ImageViewer)
    {
    return TCL_OK;
    }

  dpy = Tk_Display(self->TkWin);
  
  if (Tk_WindowId(self->TkWin) != None) 
    {
    XDestroyWindow(dpy, Tk_WindowId(self->TkWin) );
    }

  if (self->IV[0] == '\0')
    {
    // Make the ImageViewer window.
    self->ImageViewer = vtkImageViewer::New();
    ImageViewer = self->ImageViewer;
#ifndef VTK_PYTHON_BUILD
    vtkTclGetObjectFromPointer(self->Interp, self->ImageViewer,
                               vtkImageViewerCommand);
#endif
    self->IV = strdup(self->Interp->result);
    self->Interp->result[0] = '\0';
    }
  else
    {
    // is IV an address ? big ole python hack here
    if (self->IV[0] == 'A' && self->IV[1] == 'd' && 
        self->IV[2] == 'd' && self->IV[3] == 'r')
      {
      void *tmp;
      sscanf(self->IV+5,"%p",&tmp);
      ImageViewer = (vtkImageViewer *)tmp;
      }
    else
      {
#ifndef VTK_PYTHON_BUILD
      int new_flag;
      ImageViewer = (vtkImageViewer *)
        vtkTclGetPointerFromObject(self->IV, "vtkImageViewer", self->Interp,
                                   new_flag);
#endif
      }
    if (ImageViewer != self->ImageViewer)
      {
      if (self->ImageViewer != NULL)
        {
        self->ImageViewer->UnRegister(NULL);
        }
      self->ImageViewer = (vtkImageViewer *)(ImageViewer);
      if (self->ImageViewer != NULL)
        {
        self->ImageViewer->Register(NULL);
        }
      }
    }
  
        
  // get the window
  ImageWindow = static_cast<vtkXOpenGLRenderWindow *>(ImageViewer->GetRenderWindow());
  // If the imageviewer has already created it's window, throw up our hands and quit...
  if ( ImageWindow->GetWindowId() != (Window)NULL )
    {
    return TCL_ERROR;
    }
        
  // Use the same display
  ImageWindow->SetDisplayId(dpy);
  // The visual MUST BE SET BEFORE the window is created.
  Tk_SetWindowVisual(self->TkWin, ImageWindow->GetDesiredVisual(), 
                     ImageWindow->GetDesiredDepth(), 
                     ImageWindow->GetDesiredColormap());

  // Make this window exist, then use that information to make the vtkImageViewer in sync
  Tk_MakeWindowExist ( self->TkWin );
  ImageViewer->SetWindowId ( (void*)Tk_WindowId ( self->TkWin ) );

  // Set the size
  self->ImageViewer->SetSize(self->Width, self->Height);

  // Set the parent correctly
  // Possibly X dependent
  if ((Tk_Parent(self->TkWin) == NULL) || (Tk_IsTopLevel(self->TkWin))) 
    {
    ImageWindow->SetParentId(XRootWindow(Tk_Display(self->TkWin), Tk_ScreenNumber(self->TkWin)));
    }
  else 
    {
    ImageWindow->SetParentId(Tk_WindowId(Tk_Parent(self->TkWin) ));
    }

  self->ImageViewer->Render();          
  return TCL_OK;
}
#endif
#endif
