/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkOpenGLTexture.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
// .NAME vtkOpenGLTexture - OpenGL texture map
// .SECTION Description
// vtkOpenGLTexture is a concrete implementation of the abstract class
// vtkTexture. vtkOpenGLTexture interfaces to the OpenGL rendering library.

#ifndef __vtkOpenGLTexture_h
#define __vtkOpenGLTexture_h

#include "vtkRenderingOpenGL2Module.h" // For export macro
#include "vtkTexture.h"
//BTX
#include "vtkWeakPointer.h" // needed for vtkWeakPointer.
//ETX

class vtkWindow;
class vtkRenderWindow;
class vtkPixelBufferObject;

class VTKRENDERINGOPENGL2_EXPORT vtkOpenGLTexture : public vtkTexture
{
public:
  static vtkOpenGLTexture *New();
  vtkTypeMacro(vtkOpenGLTexture, vtkTexture);
  virtual void PrintSelf(ostream& os, vtkIndent indent);

  // Description:
  // Implement base class method.
  void Load(vtkRenderer*);

  // Descsription:
  // Clean up after the rendering is complete.
  virtual void PostRender(vtkRenderer*);

  // Description:
  // Release any graphics resources that are being consumed by this texture.
  // The parameter window could be used to determine which graphic
  // resources to release. Using the same texture object in multiple
  // render windows is NOT currently supported.
  void ReleaseGraphicsResources(vtkWindow*);

  // Description:
  // Get the openGL texture name to which this texture is bound.
  vtkGetMacro(Index, long);

  // Description
  // copy the renderers read buffer into this texture
  void CopyTexImage(vtkRenderer *ren, int x, int y, int width, int height);

  // Description
  // Provide for specifying a format for the texture
  // GL_DEPTH
  vtkGetMacro(TextureFormat,int);
  vtkSetMacro(TextureFormat,int);

  // Description
  // What type of texture map GL_TEXTURE_2D versus GL_TEXTURE_RECTANGLE
  vtkGetMacro(TextureType,int);
  vtkSetMacro(TextureType,int);

protected:
//BTX
  vtkOpenGLTexture();
  ~vtkOpenGLTexture();

  vtkTimeStamp   LoadTime;
  unsigned int Index; // actually GLuint
  vtkWeakPointer<vtkRenderWindow> RenderWindow;   // RenderWindow used for previous render

  int TextureFormat;
  int TextureType;

  // used when the texture exceeds the GL limit
  unsigned char *ResampleToPowerOfTwo(int &xsize, int &ysize,
                                      unsigned char *dptr, int bpp);



private:
  vtkOpenGLTexture(const vtkOpenGLTexture&);  // Not implemented.
  void operator=(const vtkOpenGLTexture&);  // Not implemented.

  // Description:
  // Handle loading in extension support
  virtual void Initialize(vtkRenderer * ren);

//ETX
};

#endif
