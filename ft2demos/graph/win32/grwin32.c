/*******************************************************************
 *
 *  grwin32.c  graphics driver for Win32 platform
 *
 *  This is the driver for displaying inside a window under Win32,
 *  used by the graphics utility of the FreeType test suite.
 *
 *  Written by Antoine Leca.
 *  Copyright (C) 1999-2020 by
 *  Antoine Leca, David Turner, Robert Wilhelm, and Werner Lemberg.
 *
 *  Borrowing liberally from the other FreeType drivers.
 *
 *  This file is part of the FreeType project, and may only be used
 *  modified and distributed under the terms of the FreeType project
 *  license, LICENSE.TXT. By continuing to use, modify or distribute
 *  this file you indicate that you have read the license and
 *  understand and accept it fully.
 *
 ******************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* define to activate OLPC swizzle */
#define xxSWIZZLE

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <grobjs.h>
#include <grdevice.h>
#ifdef SWIZZLE
#include <grswizzle.h>
#endif

/* logging facility */
#include <stdarg.h>

#define  DEBUGxxx

#ifdef DEBUG
#define LOG(x)  LogMessage##x
#else
#define LOG(x)  /* rien */
#endif

#ifdef DEBUG
  static void  LogMessage( const char*  fmt, ... )
  {
    va_list  ap;

    va_start( ap, fmt );
    vfprintf( stderr, fmt, ap );
    va_end( ap );
  }
#endif
/*-------------------*/

/*  Custom messages. */
#define WM_RESIZE  WM_USER+517


  typedef struct  Translator_
  {
    ULONG   winkey;
    grKey   grkey;

  } Translator;

  static
  Translator  key_translators[] =
  {
    { VK_BACK,      grKeyBackSpace },
    { VK_TAB,       grKeyTab       },
    { VK_RETURN,    grKeyReturn    },
    { VK_ESCAPE,    grKeyEsc       },
    { VK_HOME,      grKeyHome      },
    { VK_LEFT,      grKeyLeft      },
    { VK_UP,        grKeyUp        },
    { VK_RIGHT,     grKeyRight     },
    { VK_DOWN,      grKeyDown      },
    { VK_PRIOR,     grKeyPageUp    },
    { VK_NEXT,      grKeyPageDown  },
    { VK_END,       grKeyEnd       },
    { VK_F1,        grKeyF1        },
    { VK_F2,        grKeyF2        },
    { VK_F3,        grKeyF3        },
    { VK_F4,        grKeyF4        },
    { VK_F5,        grKeyF5        },
    { VK_F6,        grKeyF6        },
    { VK_F7,        grKeyF7        },
    { VK_F8,        grKeyF8        },
    { VK_F9,        grKeyF9        },
    { VK_F10,       grKeyF10       },
    { VK_F11,       grKeyF11       },
    { VK_F12,       grKeyF12       }
  };

  static ATOM  ourAtom;

  typedef struct grWin32SurfaceRec_
  {
    grSurface     root;
    HWND          window;
    HICON         sIcon;
    HICON         bIcon;
    BITMAPINFOHEADER  bmiHeader;
    RGBQUAD           bmiColors[256];
    grBitmap      bgrBitmap;  /* windows wants data in BGR format !! */
#ifdef SWIZZLE
    grBitmap      swizzle_bitmap;
#endif
  } grWin32Surface;


/* destroys the surface*/
static void
gr_win32_surface_done( grWin32Surface*  surface )
{
  /* The graphical window has perhaps already destroyed itself */
  if ( surface->window )
  {
    DestroyWindow ( surface->window );
    PostMessage( surface->window, WM_QUIT, 0, 0 );
  }

  DestroyIcon( surface->sIcon );
  DestroyIcon( surface->bIcon );
#ifdef SWIZZLE
  grDoneBitmap( &surface->swizzle_bitmap );
#endif
  grDoneBitmap( &surface->bgrBitmap );
  grDoneBitmap( &surface->root.bitmap );
}


static void
gr_win32_surface_refresh_rectangle(
         grWin32Surface*  surface,
         int              x,
         int              y,
         int              w,
         int              h )
{
  int        delta;
  RECT       rect;
  HANDLE     window = surface->window;
  grBitmap*  bitmap = &surface->root.bitmap;

  LOG(( "gr_win32_surface_refresh_rectangle: ( %p, %d, %d, %d, %d )\n",
        (long)surface, x, y, w, h ));

  /* clip update rectangle */

  if ( x < 0 )
  {
    w += x;
    x  = 0;
  }

  delta = x + w - bitmap->width;
  if ( delta > 0 )
    w -= delta;

  if ( y < 0 )
  {
    h += y;
    y  = 0;
  }

  delta = y + h - bitmap->rows;
  if ( delta > 0 )
    h -= delta;

  if ( w <= 0 || h <= 0 )
    return;

  rect.left   = x;
  rect.top    = y;
  rect.right  = x + w;
  rect.bottom = y + h;

#ifdef SWIZZLE
  {
    grBitmap*  swizzle = &surface->swizzle_bitmap;

    gr_swizzle_rect_rgb24( bitmap->buffer, bitmap->pitch,
                           swizzle->buffer, swizzle->pitch,
                           bitmap->width,
                           bitmap->rows,
                           0, 0, bitmap->width, bitmap->rows );

    bitmap = swizzle;
  }
#endif

  /* copy to BGR buffer */
  {
    unsigned char*  read_line   = (unsigned char*)bitmap->buffer;
    int             read_pitch  = bitmap->pitch;
    unsigned char*  write_line  = (unsigned char*)surface->bgrBitmap.buffer;
    int             write_pitch = surface->bgrBitmap.pitch;

    if ( read_pitch < 0 )
      read_line -= ( bitmap->rows - 1 ) * read_pitch;

    if ( write_pitch < 0 )
      write_line -= ( bitmap->rows - 1 ) * write_pitch;

    read_line  += y * read_pitch;
    write_line += y * write_pitch;

    if ( bitmap->mode == gr_pixel_mode_gray )
    {
      read_line  += x;
      write_line += x;
      for ( ; h > 0; h-- )
      {
        memcpy( write_line, read_line, w );

        read_line  += read_pitch;
        write_line += write_pitch;
      }
    }
    else
    {
      read_line  += 3 * x;
      write_line += 3 * x;
      for ( ; h > 0; h-- )
      {
        unsigned char*  read       = read_line;
        unsigned char*  read_limit = read + 3 * w;
        unsigned char*  write      = write_line;

        for ( ; read < read_limit; read += 3, write += 3 )
        {
          write[0] = read[2];
          write[1] = read[1];
          write[2] = read[0];
        }

        read_line  += read_pitch;
        write_line += write_pitch;
      }
    }
  }

  InvalidateRect( window, &rect, FALSE );
}


static void
gr_win32_surface_set_title( grWin32Surface*  surface,
                            const char*      title )
{
  SetWindowText( surface->window, title );
}


static int
gr_win32_surface_set_icon( grWin32Surface*  surface,
                           grBitmap*        icon )
{
  int       s[] = { GetSystemMetrics( SM_CYSMICON ),
                    GetSystemMetrics( SM_CYICON ) };
  WPARAM    wParam;
  HDC       hDC;
  VOID*     bts;
  ICONINFO  ici = { TRUE };
  HICON     hIcon;

  BITMAPV4HEADER  hdr = { sizeof( BITMAPV4HEADER ),
                          0, 0, 1, 32, BI_BITFIELDS, 0, 0, 0, 0, 0,
                          0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000,
                          LCS_sRGB };


  if ( !icon )
    return s[1];
  else if ( icon->mode != gr_pixel_mode_rgb32 )
    return 0;
  else if ( icon->rows == s[0] )
    wParam = ICON_SMALL;
  else if ( icon->rows == s[1] )
    wParam = ICON_BIG;
  else
    return 0;

  ici.hbmMask  = CreateBitmap( icon->width, icon->rows, 1, 1, NULL);

  hdr.bV4Width  =  icon->width;
  hdr.bV4Height = -icon->rows;

  hDC = GetDC( NULL );
  ici.hbmColor = CreateDIBSection( hDC, (LPBITMAPINFO)&hdr,
                                   DIB_RGB_COLORS, &bts, NULL, 0 );
  ReleaseDC( NULL, hDC );

  memcpy( bts, icon->buffer, icon->rows * icon->width * 4 );

  hIcon = CreateIconIndirect( &ici );

  PostMessage( surface->window, WM_SETICON, wParam, (LPARAM)hIcon );

  switch( wParam )
  {
  case ICON_SMALL:
    surface->sIcon = hIcon;
    return 0;
  case ICON_BIG:
    surface->bIcon = hIcon;
    return s[0];
  }
}


/*
 * set graphics mode
 * and create the window class and the message handling.
 */


static grWin32Surface*
gr_win32_surface_resize( grWin32Surface*  surface,
                         int              width,
                         int              height )
{
  grBitmap*       bitmap = &surface->root.bitmap;

  /* resize root bitmap */
  if ( grNewBitmap( bitmap->mode,
                    bitmap->grays,
                    width,
                    height,
                    bitmap ) )
    return 0;
  bitmap->pitch = -bitmap->pitch;

  /* resize BGR shadow bitmap */
  if ( grNewBitmap( bitmap->mode,
                    bitmap->grays,
                    width,
                    height,
                    &surface->bgrBitmap ) )
    return 0;
  surface->bgrBitmap.pitch = -surface->bgrBitmap.pitch;

#ifdef SWIZZLE
  if ( bitmap->mode == gr_pixel_mode_rgb24 )
  {
    if ( grNewBitmap( bitmap->mode,
                      bitmap->grays,
                      width,
                      height,
                      &surface->swizzle_bitmap ) )
      return 0;
    surface->swizzle_bitmap.pitch = -surface->swizzle_bitmap.pitch;
  }
#endif

  /* update the header to appropriate values */
  surface->bmiHeader.biWidth  = width;
  surface->bmiHeader.biHeight = height;

  return surface;
}

static void
gr_win32_surface_listen_event( grWin32Surface*  surface,
                               int              event_mask,
                               grEvent*         grevent )
{
  MSG     msg;
  HANDLE  window = surface->window;

  event_mask=event_mask;  /* unused parameter */

  while (GetMessage( &msg, NULL, 0, 0 ) > 0)
  {
    switch ( msg.message )
    {
    case WM_RESIZE:
      {
        int  width  = LOWORD(msg.lParam);
        int  height = HIWORD(msg.lParam);


        if ( ( width  != surface->root.bitmap.width  ||
               height != surface->root.bitmap.rows   )         &&
             gr_win32_surface_resize( surface, width, height ) )
        {
          grevent->type  = gr_event_resize;
          grevent->x     = width;
          grevent->y     = height;
          return;
        }
      }
      break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      {
        Translator*  trans = key_translators;
        Translator*  limit = trans + sizeof( key_translators ) /
                                     sizeof( key_translators[0] );
        for ( ; trans < limit; trans++ )
          if ( msg.wParam == trans->winkey )
          {
            grevent->type = gr_event_key;
            grevent->key  = trans->grkey;
            LOG(( "KeyPress: VK = 0x%02x\n", msg.wParam ));
            return;
          }
      }
      break;

    case WM_CHAR:
      {
        grevent->type = gr_event_key;
        grevent->key  = msg.wParam;
        LOG(( isprint( msg.wParam ) ? "KeyPress: Char = '%c'\n"
                                    : "KeyPress: Char = <%02x>\n",
              msg.wParam ));
        return;
      }
      break;
    }

    TranslateMessage( &msg );
    DispatchMessage( &msg );
  }
}

static grWin32Surface*
gr_win32_surface_init( grWin32Surface*  surface,
                       grBitmap*        bitmap )
{
  static RGBQUAD  black = {    0,    0,    0, 0 };
  static RGBQUAD  white = { 0xFF, 0xFF, 0xFF, 0 };

  LOG(( "Win32: init_surface( %p, %p )\n", surface, bitmap ));

  LOG(( "       -- input bitmap =\n" ));
  LOG(( "       --   mode   = %d\n", bitmap->mode ));
  LOG(( "       --   grays  = %d\n", bitmap->grays ));
  LOG(( "       --   width  = %d\n", bitmap->width ));
  LOG(( "       --   height = %d\n", bitmap->rows ));

  /* create the bitmap - under Win32, we support all modes as the GDI */
  /* handles all conversions automatically..                          */
  if ( grNewBitmap( bitmap->mode,
                    bitmap->grays,
                    bitmap->width,
                    bitmap->rows,
                    bitmap ) )
    return 0;
  bitmap->pitch = -bitmap->pitch;

  /* allocate the BGR shadow bitmap */
  if ( grNewBitmap( bitmap->mode,
                    bitmap->grays,
                    bitmap->width,
                    bitmap->rows,
                    &surface->bgrBitmap ) )
    return 0;
  surface->bgrBitmap.pitch = -surface->bgrBitmap.pitch;

#ifdef SWIZZLE
  if ( bitmap->mode == gr_pixel_mode_rgb24 )
  {
    if ( grNewBitmap( bitmap->mode,
                      bitmap->grays,
                      bitmap->width,
                      bitmap->rows,
                      &surface->swizzle_bitmap ) )
      return 0;
    surface->swizzle_bitmap.pitch = -surface->swizzle_bitmap.pitch;
  }
#endif

  LOG(( "       -- output bitmap =\n" ));
  LOG(( "       --   mode   = %d\n", bitmap->mode ));
  LOG(( "       --   grays  = %d\n", bitmap->grays ));
  LOG(( "       --   width  = %d\n", bitmap->width ));
  LOG(( "       --   height = %d\n", bitmap->rows ));

  surface->bmiHeader.biSize   = sizeof( BITMAPINFOHEADER );
  surface->bmiHeader.biWidth  = bitmap->width;
  surface->bmiHeader.biHeight = bitmap->rows;
  surface->bmiHeader.biPlanes = 1;

  switch ( bitmap->mode )
  {
  case gr_pixel_mode_mono:
    surface->bmiHeader.biBitCount = 1;
    surface->bmiColors[0] = white;
    surface->bmiColors[1] = black;
    break;

  case gr_pixel_mode_rgb24:
    surface->bmiHeader.biBitCount    = 24;
    surface->bmiHeader.biCompression = BI_RGB;
    break;

  case gr_pixel_mode_gray:
    surface->bmiHeader.biBitCount = 8;
    surface->bmiHeader.biClrUsed  = bitmap->grays;
    {
      int   count = bitmap->grays;
      int   x;
      RGBQUAD*  color = surface->bmiColors;

      for ( x = 0; x < count; x++, color++ )
      {
        color->rgbRed   =
        color->rgbGreen =
        color->rgbBlue  = (unsigned char)(x*255/(count-1));
        color->rgbReserved = 0;
      }
    }
    break;

  default:
    return 0;         /* Unknown mode */
  }

  {
    DWORD  style = WS_OVERLAPPEDWINDOW;
    RECT   WndRect;

    WndRect.left   = 0;
    WndRect.top    = 0;
    WndRect.right  = bitmap->width;
    WndRect.bottom = bitmap->rows;

    AdjustWindowRect( &WndRect, style, FALSE );

    surface->window = CreateWindow(
            /* LPCSTR lpszClassName;    */ "FreeTypeTestGraphicDriver",
            /* LPCSTR lpszWindowName;   */ "FreeType Test Graphic Driver",
            /* DWORD dwStyle;           */  style,
            /* int x;                   */  CW_USEDEFAULT,
            /* int y;                   */  CW_USEDEFAULT,
            /* int nWidth;              */  WndRect.right - WndRect.left,
            /* int nHeight;             */  WndRect.bottom - WndRect.top,
            /* HWND hwndParent;         */  HWND_DESKTOP,
            /* HMENU hmenu;             */  0,
            /* HINSTANCE hinst;         */  GetModuleHandle( NULL ),
            /* void FAR* lpvParam;      */  surface );
  }

  if ( surface->window == 0 )
    return  0;

  ShowWindow( surface->window, SW_SHOWNORMAL );

  surface->root.bitmap       = *bitmap;
  surface->root.done         = (grDoneSurfaceFunc) gr_win32_surface_done;
  surface->root.refresh_rect = (grRefreshRectFunc) gr_win32_surface_refresh_rectangle;
  surface->root.set_title    = (grSetTitleFunc)    gr_win32_surface_set_title;
  surface->root.set_icon     = (grSetIconFunc)     gr_win32_surface_set_icon;
  surface->root.listen_event = (grListenEventFunc) gr_win32_surface_listen_event;

  return surface;
}


/* ---- Windows-specific stuff ------------------------------------------- */


  /* Message processing for our Windows class */
LRESULT CALLBACK Message_Process( HWND handle, UINT mess,
                                  WPARAM wParam, LPARAM lParam )
  {
    grWin32Surface*  surface = NULL;

    if ( mess == WM_CREATE )
    {
      /* WM_CREATE is the first message sent to this function, and the */
      /* surface handle is available from the 'lParam' parameter. We   */
      /* save its value in a window property..                         */
      /*                                                               */
      surface = ((LPCREATESTRUCT)lParam)->lpCreateParams;

      SetProp( handle, (LPCSTR)(LONG)ourAtom, surface );
    }
    else
    {
      /* for other calls, we retrieve the surface handle from the window */
      /* property.. ugly, isn't it ??                                    */
      /*                                                                 */
      surface = (grWin32Surface*) GetProp( handle, (LPCSTR)(LONG)ourAtom );
    }

    switch( mess )
    {
    case WM_CLOSE:
      /* warn the main thread to quit if it didn't know */
      PostMessage( handle, WM_CHAR, (WPARAM)grKeyEsc, 0 );
      return 0;

    case WM_SIZE:
      if ( wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED )
        PostMessage( handle, WM_RESIZE, wParam, lParam );
      break;

    case WM_EXITSIZEMOVE:
      {
        RECT  WndRect;

        GetClientRect( handle, &WndRect );
        PostMessage( handle, WM_RESIZE, SIZE_RESTORED,
                     MAKELPARAM( WndRect.right, WndRect.bottom ) );
      }
      break;

    case WM_PAINT:
      {
        HDC           hDC;
        PAINTSTRUCT   ps;

        hDC   = BeginPaint ( handle, &ps );
        SetDIBitsToDevice( hDC, 0, 0,
                           surface->bmiHeader.biWidth,
                           surface->bmiHeader.biHeight,
                           0, 0, 0,
                           surface->bmiHeader.biHeight,
                           surface->bgrBitmap.buffer,
                           (LPBITMAPINFO)&surface->bmiHeader,
                           DIB_RGB_COLORS );
        EndPaint ( handle, &ps );
        return 0;
      }

    default:
      return DefWindowProc( handle, mess, wParam, lParam );
    }
    return 0;
  }

  static int
  gr_win32_device_init( void )
  {
    WNDCLASS ourClass = {
      /* UINT    style        */ 0,
      /* WNDPROC lpfnWndProc  */ Message_Process,
      /* int     cbClsExtra   */ 0,
      /* int     cbWndExtra   */ 0,
      /* HANDLE  hInstance    */ 0,
      /* HICON   hIcon        */ 0,
      /* HCURSOR hCursor      */ 0,
      /* HBRUSH  hbrBackground*/ 0,
      /* LPCTSTR lpszMenuName */ NULL,
      /* LPCTSTR lpszClassName*/ "FreeTypeTestGraphicDriver"
    };

    /* register window class */

    ourClass.hInstance    = GetModuleHandle( NULL );
    ourClass.hIcon        = LoadIcon(0, IDI_APPLICATION);
    ourClass.hCursor      = LoadCursor(0, IDC_ARROW);
    ourClass.hbrBackground= GetStockObject(BLACK_BRUSH);

    if ( RegisterClass(&ourClass) == 0 )
      return -1;

    /* add global atom */
    ourAtom = GlobalAddAtom( "FreeType.Surface" );

    return 0;
  }

  static void
  gr_win32_device_done( void )
  {
    GlobalDeleteAtom( ourAtom );
  }


  grDevice  gr_win32_device =
  {
    sizeof( grWin32Surface ),
    "win32",

    gr_win32_device_init,
    gr_win32_device_done,

    (grDeviceInitSurfaceFunc) gr_win32_surface_init,

    0,
    0
  };


/* End */
