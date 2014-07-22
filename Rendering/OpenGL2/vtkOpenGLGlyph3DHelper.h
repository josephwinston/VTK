/*=========================================================================

  Program:   Visualization Toolkit

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
// .NAME vtkOpenGLGlyph3DHelper - PolyDataMapper using OpenGL to render.
// .SECTION Description
// PolyDataMapper that uses a OpenGL to do the actual rendering.

#ifndef __vtkOpenGLGlyph3DHelper_h
#define __vtkOpenGLGlyph3DHelper_h

#include "vtkRenderingOpenGL2Module.h" // For export macro
#include "vtkOpenGLPolyDataMapper.h"

class VTKRENDERINGOPENGL2_EXPORT vtkOpenGLGlyph3DHelper : public vtkOpenGLPolyDataMapper
{
public:
  static vtkOpenGLGlyph3DHelper* New();
  vtkTypeMacro(vtkOpenGLGlyph3DHelper, vtkOpenGLPolyDataMapper)
  void PrintSelf(ostream& os, vtkIndent indent);

  void SetModelTransform(vtkMatrix4x4* matrix)
  {
    this->ModelTransformMatrix = matrix;
  }

  void SetModelColor(unsigned char *color)
  {
    this->ModelColor[0] = color[0];
    this->ModelColor[1] = color[1];
    this->ModelColor[2] = color[2];
    this->ModelColor[3] = color[3];
  }

  // Description
  // Fast path for rendering glyphs comprised of only one type of primative
  void GlyphRender(vtkRenderer* ren, vtkActor* actor, unsigned char rgba[4], vtkMatrix4x4 *gmat, int stage);

protected:
  vtkOpenGLGlyph3DHelper();
  ~vtkOpenGLGlyph3DHelper();

  // Description:
  // Set the shader parameteres related to the Camera
  virtual void SetCameraShaderParameters(vtkgl::CellBO &cellBO, vtkRenderer *ren, vtkActor *act);

  // Description:
  // Set the shader parameteres related to the property
  virtual void SetPropertyShaderParameters(vtkgl::CellBO &cellBO, vtkRenderer *ren, vtkActor *act);

  vtkMatrix4x4* ModelTransformMatrix;
  unsigned char ModelColor[4];

private:
  vtkOpenGLGlyph3DHelper(const vtkOpenGLGlyph3DHelper&); // Not implemented.
  void operator=(const vtkOpenGLGlyph3DHelper&); // Not implemented.
};

#endif
