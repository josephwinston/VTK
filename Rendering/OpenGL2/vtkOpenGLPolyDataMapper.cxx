/*=========================================================================

  Program:   Visualization Toolkit

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkOpenGLPolyDataMapper.h"

#include "vtkglVBOHelper.h"

#include "vtkCommand.h"
#include "vtkCamera.h"
#include "vtkTransform.h"
#include "vtkObjectFactory.h"
#include "vtkMath.h"
#include "vtkPolyData.h"
#include "vtkRenderer.h"
#include "vtkRenderWindow.h"
#include "vtkPointData.h"
#include "vtkCellArray.h"
#include "vtkVector.h"
#include "vtkProperty.h"
#include "vtkMatrix3x3.h"
#include "vtkMatrix4x4.h"
#include "vtkLookupTable.h"
#include "vtkCellData.h"
#include "vtkNew.h"
#include "vtkSmartPointer.h"

#include "vtkLight.h"
#include "vtkLightCollection.h"

#include "vtkOpenGLRenderer.h"
#include "vtkOpenGLRenderWindow.h"
#include "vtkOpenGLShaderCache.h"

#include "vtkFloatArray.h"
#include "vtkImageData.h"
#include "vtkOpenGLTexture.h"

#include "vtkHardwareSelector.h"

// Bring in our fragment lit shader symbols.
#include "vtkglPolyDataVSFragmentLit.h"
#include "vtkglPolyDataFSHeadlight.h"
#include "vtkglPolyDataFSLightKit.h"
#include "vtkglPolyDataFSPositionalLights.h"

// bring in vertex lit shader symbols
#include "vtkglPolyDataVSNoLighting.h"
#include "vtkglPolyDataFS.h"

using vtkgl::replace;

//-----------------------------------------------------------------------------
vtkStandardNewMacro(vtkOpenGLPolyDataMapper)

//-----------------------------------------------------------------------------
vtkOpenGLPolyDataMapper::vtkOpenGLPolyDataMapper()
  : UsingScalarColoring(false)
{
  this->InternalColorTexture = 0;
  this->PopulateSelectionSettings = 1;
  this->LastLightComplexity = -1;
  this->LastSelectionState = false;
  this->LastDepthPeeling = 0;
}


//-----------------------------------------------------------------------------
vtkOpenGLPolyDataMapper::~vtkOpenGLPolyDataMapper()
{
  if (this->InternalColorTexture)
    { // Resources released previously.
    this->InternalColorTexture->Delete();
    this->InternalColorTexture = 0;
    }
}

//-----------------------------------------------------------------------------
void vtkOpenGLPolyDataMapper::ReleaseGraphicsResources(vtkWindow* win)
{
  // FIXME: Implement resource release.
    // We may not want to do this here.
  if (this->InternalColorTexture)
    {
    this->InternalColorTexture->ReleaseGraphicsResources(win);
    }
}


//-----------------------------------------------------------------------------
void vtkOpenGLPolyDataMapper::BuildShader(std::string &VSSource,
                                          std::string &FSSource,
                                          std::string &GSSource,
                                          int lightComplexity, vtkRenderer* ren, vtkActor *actor)
{
  switch (lightComplexity)
    {
    case 0:
        VSSource = vtkglPolyDataVSNoLighting;
        FSSource = vtkglPolyDataFS;
      break;
    case 1:
        VSSource = vtkglPolyDataVSFragmentLit;
        FSSource = vtkglPolyDataFSHeadlight;
      break;
    case 2:
        VSSource = vtkglPolyDataVSFragmentLit;
        FSSource = vtkglPolyDataFSLightKit;
      break;
    case 3:
        VSSource = vtkglPolyDataVSFragmentLit;
        FSSource = vtkglPolyDataFSPositionalLights;
      break;
    }
  GSSource.clear();

  if (this->layout.ColorComponents != 0)
    {
    VSSource = replace(VSSource,"//VTK::Color::Dec",
                                "attribute vec4 scalarColor; varying vec4 vertexColor;");
    VSSource = replace(VSSource,"//VTK::Color::Impl",
                                "vertexColor =  scalarColor;");
    FSSource = replace(FSSource,"//VTK::Color::Dec",
                                "varying vec4 vertexColor;");
    if (this->ScalarMaterialMode == VTK_MATERIALMODE_AMBIENT ||
          (this->ScalarMaterialMode == VTK_MATERIALMODE_DEFAULT && actor->GetProperty()->GetAmbient() > actor->GetProperty()->GetDiffuse()))
      {
      FSSource = replace(FSSource,"//VTK::Color::Impl",
                                    "vec3 ambientColor = vertexColor.rgb; vec3 diffuseColor = diffuseColorUniform.rgb; float opacity = vertexColor.a;");
      }
    else if (this->ScalarMaterialMode == VTK_MATERIALMODE_DIFFUSE ||
          (this->ScalarMaterialMode == VTK_MATERIALMODE_DEFAULT && actor->GetProperty()->GetAmbient() <= actor->GetProperty()->GetDiffuse()))
      {
      FSSource = replace(FSSource,"//VTK::Color::Impl",
                                  "vec3 diffuseColor = vertexColor.rgb; vec3 ambientColor = ambientColorUniform; float opacity = vertexColor.a;");
      }
    else
      {
      FSSource = replace(FSSource,"//VTK::Color::Impl",
                                   "vec3 diffuseColor = vertexColor.rgb; vec3 ambientColor = vertexColor.rgb; float opacity = vertexColor.a;");
      }
    }
  else
    {
    FSSource = replace(FSSource,"//VTK::Color::Impl",
                                  "vec3 ambientColor = ambientColorUniform; vec3 diffuseColor = diffuseColorUniform; float opacity = opacityUniform;");
    }
  // normals?
  if (this->layout.NormalOffset)
    {
    VSSource = replace(VSSource,
                                 "//VTK::Normal::Dec",
                                 "attribute vec3 normalMC; varying vec3 normalVCVarying;");
    VSSource = replace(VSSource,
                                 "//VTK::Normal::Impl",
                                 "normalVCVarying = normalMatrix * normalMC;");
    FSSource = replace(FSSource,
                                 "//VTK::Normal::Dec",
                                 "varying vec3 normalVCVarying;");
    FSSource = replace(FSSource,
                                 "//VTK::Normal::Impl",
                                 "vec3 normalVC; if (!gl_FrontFacing) { normalVC = -normalVCVarying; } else { normalVC = normalVCVarying; }");
    }
  else
    {
    if (actor->GetProperty()->GetRepresentation() == VTK_WIREFRAME)
      {
      // generate a normal for lines, it will be perpendicular to the line
      // and maximally aligned with the camera view direction
      // no clue if this is the best way to do this.
      FSSource = replace(FSSource,"//VTK::Normal::Impl",
                                   "vec3 normalVC;\n"
                                   "if (abs(dot(dFdx(vertexVC.xyz),vec3(1,1,1))) > abs(dot(dFdy(vertexVC.xyz),vec3(1,1,1))))\n"
                                   " { normalVC = normalize(cross(cross(dFdx(vertexVC.xyz), vec3(0,0,1)), dFdx(vertexVC.xyz))); }\n"
                                   "else { normalVC = normalize(cross(cross(dFdy(vertexVC.xyz), vec3(0,0,1)), dFdy(vertexVC.xyz)));}");
      }
    else
      {
      FSSource = replace(FSSource,"//VTK::Normal::Impl",
                                   "vec3 normalVC = normalize(cross(dFdx(vertexVC.xyz), dFdy(vertexVC.xyz)));\n"
                                   "if (normalVC.z < 0) { normalVC = -1.0*normalVC; }"
                                   );
      }
    }
  if (this->layout.TCoordComponents)
    {
    if (this->layout.TCoordComponents == 1)
      {
      VSSource = vtkgl::replace(VSSource,
                                   "//VTK::TCoord::Dec",
                                   "attribute float tcoordMC; varying float tcoordVC;");
      VSSource = vtkgl::replace(VSSource,
                                   "//VTK::TCoord::Impl",
                                   "tcoordVC = tcoordMC;");
      FSSource = vtkgl::replace(FSSource,
                                   "//VTK::TCoord::Dec",
                                   "varying float tcoordVC; uniform sampler2D texture1;");
      FSSource = vtkgl::replace(FSSource,
                                   "//VTK::TCoord::Impl",
                                   "gl_FragColor = gl_FragColor*texture2D(texture1, vec2(tcoordVC,0));");
      }
    else
      {
      VSSource = vtkgl::replace(VSSource,
                                   "//VTK::TCoord::Dec",
                                   "attribute vec2 tcoordMC; varying vec2 tcoordVC;");
      VSSource = vtkgl::replace(VSSource,
                                   "//VTK::TCoord::Impl",
                                   "tcoordVC = tcoordMC;");
      FSSource = vtkgl::replace(FSSource,
                                   "//VTK::TCoord::Dec",
                                   "varying vec2 tcoordVC; uniform sampler2D texture1;");
      FSSource = vtkgl::replace(FSSource,
                                   "//VTK::TCoord::Impl",
                                   "gl_FragColor = gl_FragColor*texture2D(texture1, tcoordVC.st);");
      }
    }


  vtkHardwareSelector* selector = ren->GetSelector();
  bool picking = (ren->GetRenderWindow()->GetIsPicking() || selector != NULL);
  if (picking)
    {
    FSSource = vtkgl::replace(FSSource,
                                 "//VTK::Picking::Dec","uniform vec3 mapperIndex;");
    FSSource = vtkgl::replace(FSSource,
                              "//VTK::Picking::Impl",
                              "if (mapperIndex == vec3(0,0,0))  { "
                              "  int idx = gl_PrimitiveID + 1;"
                              "  gl_FragColor = vec4((idx%256)/255.0, ((idx/256)%256)/255.0, (idx/65536)/255.0, 1.0);"
                              "  } "
                              "else { "
                              "  gl_FragColor = vec4(mapperIndex,1.0);"
                              "  }");
    }

  if (ren->GetLastRenderingUsedDepthPeeling())
    {
    FSSource = vtkgl::replace(FSSource,
      "//VTK::DepthPeeling::Dec",
      "uniform sampler2DRect opaqueZTexture;"
      "uniform sampler2DRect translucentZTexture;");
    FSSource = vtkgl::replace(FSSource,
      "//VTK::DepthPeeling::Impl",
      "float odepth = texture2DRect(opaqueZTexture, gl_FragCoord.xy).r; "
      "if (gl_FragCoord.z >= odepth) { discard; } "
      "float tdepth = texture2DRect(translucentZTexture, gl_FragCoord.xy).r; "
      "if (gl_FragCoord.z <= tdepth) { discard; } "
     // "gl_FragColor = vec4(odepth*odepth,tdepth*tdepth,gl_FragCoord.z*gl_FragCoord.z,1.0);"
      );
    }

  //cout << "VS: " << VSSource << endl;
  //cout << "FS: " << FSSource << endl;
}

//-----------------------------------------------------------------------------
bool vtkOpenGLPolyDataMapper::GetNeedToRebuildShader(vtkgl::CellBO &cellBO, vtkRenderer* ren, vtkActor *actor)
{
  int lightComplexity = 0;

  // wacky backwards compatibility with old VTK lighting
  // soooo there are many factors that determine if a primative is lit or not.
  // three that mix in a complex way are representation POINT, Interpolation FLAT
  // and having normals or not.
  bool needLighting = false;
  bool haveNormals = (this->GetInput()->GetPointData()->GetNormals() != NULL);
  if (actor->GetProperty()->GetRepresentation() == VTK_POINTS)
    {
    needLighting = (actor->GetProperty()->GetInterpolation() != VTK_FLAT && haveNormals);
    }
  else  // wireframe or surface rep
    {
    bool isTrisOrStrips = (&cellBO == &this->tris || &cellBO == &this->triStrips);
    needLighting = (isTrisOrStrips ||
      (!isTrisOrStrips && actor->GetProperty()->GetInterpolation() != VTK_FLAT && haveNormals));
    }

  // do we need lighting?
  if (actor->GetProperty()->GetLighting() && needLighting)
    {
    // consider the lighting complexity to determine which case applies
    // simple headlight, Light Kit, the whole feature set of VTK
    lightComplexity = 1;
    int numberOfLights = 0;
    vtkLightCollection *lc = ren->GetLights();
    vtkLight *light;

    vtkCollectionSimpleIterator sit;
    for(lc->InitTraversal(sit);
        (light = lc->GetNextLight(sit)); )
      {
      float status = light->GetSwitch();
      if (status > 0.0)
        {
        numberOfLights++;
        }

      if (lightComplexity == 1
          && (numberOfLights > 1
            || light->GetIntensity() != 1.0
            || light->GetLightType() != VTK_LIGHT_TYPE_HEADLIGHT))
        {
          lightComplexity = 2;
        }
      if (lightComplexity < 3
          && (light->GetPositional()))
        {
          lightComplexity = 3;
          break;
        }
      }
    }

  if (this->LastLightComplexity != lightComplexity)
    {
    this->LightComplexityChanged.Modified();
    this->LastLightComplexity = lightComplexity;
    }

  if (this->LastDepthPeeling !=
      ren->GetLastRenderingUsedDepthPeeling())
    {
    this->DepthPeelingChanged.Modified();
    this->LastDepthPeeling = ren->GetLastRenderingUsedDepthPeeling();
    }

  vtkHardwareSelector* selector = ren->GetSelector();
  bool picking = (ren->GetIsPicking() || selector != NULL);
  if (this->LastSelectionState != picking)
    {
    this->SelectionStateChanged.Modified();
    this->LastSelectionState = picking;
    }

  // has something changed that would require us to recreate the shader?
  // candidates are
  // property modified (representation interpolation and lighting)
  // input modified
  // light complexity changed
  if (cellBO.ShaderSourceTime < this->GetMTime() ||
      cellBO.ShaderSourceTime < actor->GetMTime() ||
      cellBO.ShaderSourceTime < this->GetInput()->GetMTime() ||
      cellBO.ShaderSourceTime < this->SelectionStateChanged ||
      cellBO.ShaderSourceTime < this->DepthPeelingChanged ||
      cellBO.ShaderSourceTime < this->LightComplexityChanged)
    {
    return true;
    }

  return false;
}

//-----------------------------------------------------------------------------
void vtkOpenGLPolyDataMapper::UpdateShader(vtkgl::CellBO &cellBO, vtkRenderer* ren, vtkActor *actor)
{
  vtkOpenGLRenderWindow *renWin = vtkOpenGLRenderWindow::SafeDownCast(ren->GetRenderWindow());

  // has something changed that would require us to recreate the shader?
  if (this->GetNeedToRebuildShader(cellBO, ren, actor))
    {
    // build the shader source code
    std::string VSSource;
    std::string FSSource;
    std::string GSSource;
    this->BuildShader(VSSource,FSSource,GSSource,this->LastLightComplexity,ren,actor);

    // compile and bind it if needed
    vtkOpenGLShaderCache::CachedShaderProgram *newShader =
      renWin->GetShaderCache()->ReadyShader(VSSource.c_str(),
                                            FSSource.c_str(),
                                            GSSource.c_str());

    // if the shader changed reinitialize the VAO
    if (newShader != cellBO.CachedProgram)
      {
      cellBO.CachedProgram = newShader;
      cellBO.vao.Initialize(); // reset the VAO as the shader has changed
      }

    cellBO.ShaderSourceTime.Modified();
    }
    else
    {
    renWin->GetShaderCache()->ReadyShader(cellBO.CachedProgram);
    }

  this->SetMapperShaderParameters(cellBO, ren, actor);
  this->SetPropertyShaderParameters(cellBO, ren, actor);
  this->SetCameraShaderParameters(cellBO, ren, actor);
  this->SetLightingShaderParameters(cellBO, ren, actor);
  cellBO.vao.Bind();

  this->lastBoundBO = &cellBO;
}


void vtkOpenGLPolyDataMapper::SetMapperShaderParameters(vtkgl::CellBO &cellBO,
                                                      vtkRenderer* ren, vtkActor *actor)
{
  // Now to update the VAO too, if necessary.
  vtkgl::VBOLayout &layout = this->layout;

  if (cellBO.indexCount && this->OpenGLUpdateTime > cellBO.attributeUpdateTime)
    {
    cellBO.vao.Bind();
    if (!cellBO.vao.AddAttributeArray(cellBO.CachedProgram->Program, this->VBO,
                                    "vertexMC", layout.VertexOffset,
                                    layout.Stride, VTK_FLOAT, 3, false))
      {
      vtkErrorMacro(<< "Error setting 'vertexMC' in shader VAO.");
      }
    if (layout.NormalOffset && this->LastLightComplexity > 0)
      {
      if (!cellBO.vao.AddAttributeArray(cellBO.CachedProgram->Program, this->VBO,
                                      "normalMC", layout.NormalOffset,
                                      layout.Stride, VTK_FLOAT, 3, false))
        {
        vtkErrorMacro(<< "Error setting 'normalMC' in shader VAO.");
        }
      }
    if (layout.TCoordComponents)
      {
      if (!cellBO.vao.AddAttributeArray(cellBO.CachedProgram->Program, this->VBO,
                                      "tcoordMC", layout.TCoordOffset,
                                      layout.Stride, VTK_FLOAT, layout.TCoordComponents, false))
        {
        vtkErrorMacro(<< "Error setting 'tcoordMC' in shader VAO.");
        }
      }
    if (layout.ColorComponents != 0)
      {
      if (!cellBO.vao.AddAttributeArray(cellBO.CachedProgram->Program, this->VBO,
                                      "scalarColor", layout.ColorOffset,
                                      layout.Stride, VTK_UNSIGNED_CHAR,
                                      layout.ColorComponents, true))
        {
        vtkErrorMacro(<< "Error setting 'scalarColor' in shader VAO.");
        }
      }
    cellBO.attributeUpdateTime.Modified();
    }


  vtkOpenGLRenderWindow *renWin = vtkOpenGLRenderWindow::SafeDownCast(ren->GetRenderWindow());

  if (layout.TCoordComponents)
    {
    vtkTexture *texture = actor->GetTexture();
    if (this->ColorTextureMap)
      {
      texture = this->InternalColorTexture;
      }
    if (!texture && actor->GetProperty()->GetNumberOfTextures())
      {
      texture = actor->GetProperty()->GetTexture(0);
      }
    int tunit = renWin->GetTextureUnitForTexture(texture);
    cellBO.CachedProgram->Program.SetUniformi("texture1", tunit);
    }

  // if depth peeling set the required uniforms
  if (ren->GetLastRenderingUsedDepthPeeling())
    {
    vtkOpenGLRenderer *oglren = vtkOpenGLRenderer::SafeDownCast(ren);
    int otunit = renWin->GetTextureUnitForTexture(oglren->GetOpaqueZTexture());
    cellBO.CachedProgram->Program.SetUniformi("opaqueZTexture", otunit);

    int ttunit = renWin->GetTextureUnitForTexture(oglren->GetTranslucentZTexture());
    cellBO.CachedProgram->Program.SetUniformi("translucentZTexture", ttunit);
    }

  if (this->LastSelectionState)
    {
    vtkHardwareSelector* selector = ren->GetSelector();
    if (selector)
      {
      if (selector->GetCurrentPass() == vtkHardwareSelector::ID_LOW24)
        {
        float tmp[3] = {0,0,0};
        cellBO.CachedProgram->Program.SetUniform3f("mapperIndex", tmp);
        }
      else
        {
        cellBO.CachedProgram->Program.SetUniform3f("mapperIndex", selector->GetPropColorValue());
        }
      }
    else
      {
      unsigned int idx = ren->GetCurrentPickId();
      float color[3];
      vtkHardwareSelector::Convert(idx, color);
      cellBO.CachedProgram->Program.SetUniform3f("mapperIndex", color);
      }
    }
}

//-----------------------------------------------------------------------------
void vtkOpenGLPolyDataMapper::SetLightingShaderParameters(vtkgl::CellBO &cellBO,
                                                      vtkRenderer* ren, vtkActor *vtkNotUsed(actor))
{
  // for unlit and headlight there are no lighting parameters
  if (this->LastLightComplexity < 2)
    {
    return;
    }

  vtkgl::ShaderProgram &program = cellBO.CachedProgram->Program;

  // for lightkit case there are some parameters to set
  vtkCamera *cam = ren->GetActiveCamera();
  vtkTransform* viewTF = cam->GetModelViewTransformObject();

  // bind some light settings
  int numberOfLights = 0;
  vtkLightCollection *lc = ren->GetLights();
  vtkLight *light;

  vtkCollectionSimpleIterator sit;
  float lightColor[6][3];
  float lightDirection[6][3];
  for(lc->InitTraversal(sit);
      (light = lc->GetNextLight(sit)); )
    {
    float status = light->GetSwitch();
    if (status > 0.0)
      {
      double *dColor = light->GetDiffuseColor();
      double intensity = light->GetIntensity();
      lightColor[numberOfLights][0] = dColor[0] * intensity;
      lightColor[numberOfLights][1] = dColor[1] * intensity;
      lightColor[numberOfLights][2] = dColor[2] * intensity;
      // get required info from light
      double *lfp = light->GetTransformedFocalPoint();
      double *lp = light->GetTransformedPosition();
      double lightDir[3];
      vtkMath::Subtract(lfp,lp,lightDir);
      vtkMath::Normalize(lightDir);
      double *tDir = viewTF->TransformNormal(lightDir);
      lightDirection[numberOfLights][0] = tDir[0];
      lightDirection[numberOfLights][1] = tDir[1];
      lightDirection[numberOfLights][2] = tDir[2];
      numberOfLights++;
      }
    }

  program.SetUniform3fv("lightColor", numberOfLights, lightColor);
  program.SetUniform3fv("lightDirectionVC", numberOfLights, lightDirection);
  program.SetUniformi("numberOfLights", numberOfLights);

  // we are done unless we have positional lights
  if (this->LastLightComplexity < 3)
    {
    return;
    }

  // if positional lights pass down more parameters
  float lightAttenuation[6][3];
  float lightPosition[6][3];
  float lightConeAngle[6];
  float lightExponent[6];
  int lightPositional[6];
  numberOfLights = 0;
  for(lc->InitTraversal(sit);
      (light = lc->GetNextLight(sit)); )
    {
    float status = light->GetSwitch();
    if (status > 0.0)
      {
      double *attn = light->GetAttenuationValues();
      lightAttenuation[numberOfLights][0] = attn[0];
      lightAttenuation[numberOfLights][1] = attn[1];
      lightAttenuation[numberOfLights][2] = attn[2];
      lightExponent[numberOfLights] = light->GetExponent();
      lightConeAngle[numberOfLights] = light->GetConeAngle();
      double *lp = light->GetTransformedPosition();
      lightPosition[numberOfLights][0] = lp[0];
      lightPosition[numberOfLights][1] = lp[1];
      lightPosition[numberOfLights][2] = lp[2];
      lightPositional[numberOfLights] = light->GetPositional();
      numberOfLights++;
      }
    }
  program.SetUniform3fv("lightAttenuation", numberOfLights, lightAttenuation);
  program.SetUniform1iv("lightPositional", numberOfLights, lightPositional);
  program.SetUniform3fv("lightPositionWC", numberOfLights, lightPosition);
  program.SetUniform1fv("lightExponent", numberOfLights, lightExponent);
  program.SetUniform1fv("lightConeAngle", numberOfLights, lightConeAngle);
}

//-----------------------------------------------------------------------------
void vtkOpenGLPolyDataMapper::SetCameraShaderParameters(vtkgl::CellBO &cellBO,
                                                    vtkRenderer* ren, vtkActor *actor)
{
  vtkgl::ShaderProgram &program = cellBO.CachedProgram->Program;

  // set the MCWC matrix for positional lighting
  if (this->LastLightComplexity > 2)
    {
    program.SetUniformMatrix("MCWCMatrix", actor->GetMatrix());
    }

  vtkCamera *cam = ren->GetActiveCamera();

  vtkNew<vtkMatrix4x4> tmpMat;
  tmpMat->DeepCopy(cam->GetModelViewTransformMatrix());

  // compute the combined ModelView matrix and send it down to save time in the shader
  vtkMatrix4x4::Multiply4x4(tmpMat.Get(), actor->GetMatrix(), tmpMat.Get());

  tmpMat->Transpose();
  program.SetUniformMatrix("MCVCMatrix", tmpMat.Get());

  // for lit shaders set normal matrix
  if (this->LastLightComplexity > 0)
    {
    tmpMat->Transpose();

    // set the normal matrix and send it down
    // (make this a function in camera at some point returning a 3x3)
    // Reuse the matrix we already got (and possibly multiplied with model mat.
    if (!actor->GetIsIdentity())
      {
      vtkNew<vtkTransform> aTF;
      aTF->SetMatrix(tmpMat.Get());
      double *scale = aTF->GetScale();
      aTF->Scale(1.0 / scale[0], 1.0 / scale[1], 1.0 / scale[2]);
      tmpMat->DeepCopy(aTF->GetMatrix());
      }
    vtkNew<vtkMatrix3x3> tmpMat3d;
    for(int i = 0; i < 3; ++i)
      {
      for (int j = 0; j < 3; ++j)
        {
        tmpMat3d->SetElement(i, j, tmpMat->GetElement(i, j));
        }
      }
    tmpMat3d->Invert();
    program.SetUniformMatrix("normalMatrix", tmpMat3d.Get());
    }

  vtkMatrix4x4 *tmpProj;
  tmpProj = cam->GetProjectionTransformMatrix(ren); // allocates a matrix
  program.SetUniformMatrix("VCDCMatrix", tmpProj);
  tmpProj->UnRegister(this);
}

//-----------------------------------------------------------------------------
void vtkOpenGLPolyDataMapper::SetPropertyShaderParameters(vtkgl::CellBO &cellBO,
                                                       vtkRenderer*, vtkActor *actor)
{
  vtkgl::ShaderProgram &program = cellBO.CachedProgram->Program;

  // Query the actor for some of the properties that can be applied.
  float opacity = static_cast<float>(actor->GetProperty()->GetOpacity());
  double *aColor = actor->GetProperty()->GetAmbientColor();
  double aIntensity = actor->GetProperty()->GetAmbient();  // ignoring renderer ambient
  float ambientColor[3] = {aColor[0] * aIntensity, aColor[1] * aIntensity, aColor[2] * aIntensity};
  double *dColor = actor->GetProperty()->GetDiffuseColor();
  double dIntensity = actor->GetProperty()->GetDiffuse();
  float diffuseColor[3] = {dColor[0] * dIntensity, dColor[1] * dIntensity, dColor[2] * dIntensity};
  double *sColor = actor->GetProperty()->GetSpecularColor();
  double sIntensity = actor->GetProperty()->GetSpecular();
  float specularColor[3] = {sColor[0] * sIntensity, sColor[1] * sIntensity, sColor[2] * sIntensity};
  float specularPower = actor->GetProperty()->GetSpecularPower();

  program.SetUniformf("opacityUniform", opacity);
  program.SetUniform3f("ambientColorUniform", ambientColor);
  program.SetUniform3f("diffuseColorUniform", diffuseColor);
  // we are done unless we have lighting
  if (this->LastLightComplexity < 1)
    {
    return;
    }
  program.SetUniform3f("specularColor", specularColor);
  program.SetUniformf("specularPower", specularPower);
}

//-----------------------------------------------------------------------------
void vtkOpenGLPolyDataMapper::RenderPieceStart(vtkRenderer* ren, vtkActor *actor)
{
  vtkDataObject *input= this->GetInputDataObject(0, 0);

  vtkHardwareSelector* selector = ren->GetSelector();
  if (selector && this->PopulateSelectionSettings)
    {
    selector->BeginRenderProp();
    if (selector->GetCurrentPass() == vtkHardwareSelector::COMPOSITE_INDEX_PASS)
      {
      selector->RenderCompositeIndex(1);
      }
    if (selector->GetCurrentPass() == vtkHardwareSelector::ID_LOW24 ||
        selector->GetCurrentPass() == vtkHardwareSelector::ID_MID24 ||
        selector->GetCurrentPass() == vtkHardwareSelector::ID_HIGH16)
      {
      // TODO need more smarts in here for mid and high
      selector->RenderAttributeId(0);
      }
    }

  this->TimeToDraw = 0.0;

  // Update the OpenGL if needed.
  if (this->OpenGLUpdateTime < this->GetMTime() ||
      this->OpenGLUpdateTime < actor->GetMTime() ||
      this->OpenGLUpdateTime < input->GetMTime() )
    {
    this->UpdateVBO(actor);
    this->OpenGLUpdateTime.Modified();
    }


  // If we are coloring by texture, then load the texture map.
  // Use Map as indicator, because texture hangs around.
  if (this->InternalColorTexture)
    {
    this->InternalColorTexture->Load(ren);
    }

  // Bind the OpenGL, this is shared between the different primitive/cell types.
  this->VBO.Bind();

  this->lastBoundBO = NULL;

  // Set the PointSize and LineWidget
  glPointSize(actor->GetProperty()->GetPointSize());
  glLineWidth(actor->GetProperty()->GetLineWidth()); // supported by all OpenGL versions

  if ( this->GetResolveCoincidentTopology() )
    {
    glEnable(GL_POLYGON_OFFSET_FILL);
    if ( this->GetResolveCoincidentTopology() == VTK_RESOLVE_SHIFT_ZBUFFER )
      {
      vtkErrorMacro(<< "GetResolveCoincidentTopologyZShift is not supported use Polygon offset instead");
      // do something rough as better than nothing
      double zRes = this->GetResolveCoincidentTopologyZShift(); // 0 is no shift 1 is big shift
      double f = zRes*4.0;
      glPolygonOffset(f,0.0);  // supported on ES2/3/etc
      }
    else
      {
      double f, u;
      this->GetResolveCoincidentTopologyPolygonOffsetParameters(f,u);
      glPolygonOffset(f,u);  // supported on ES2/3/etc
      }
    }
}

//-----------------------------------------------------------------------------
void vtkOpenGLPolyDataMapper::RenderPieceDraw(vtkRenderer* ren, vtkActor *actor)
{
  vtkgl::VBOLayout &layout = this->layout;

  // draw points
  if (this->points.indexCount)
    {
    // Update/build/etc the shader.
    this->UpdateShader(this->points, ren, actor);
    this->points.ibo.Bind();
    glDrawRangeElements(GL_POINTS, 0,
                        static_cast<GLuint>(layout.VertexCount - 1),
                        static_cast<GLsizei>(this->points.indexCount),
                        GL_UNSIGNED_INT,
                        reinterpret_cast<const GLvoid *>(NULL));
    this->points.ibo.Release();
    }

  // draw lines
  if (this->lines.indexCount)
    {
    this->UpdateShader(this->lines, ren, actor);
    this->lines.ibo.Bind();
    if (actor->GetProperty()->GetRepresentation() == VTK_POINTS)
      {
      glDrawRangeElements(GL_POINTS, 0,
                          static_cast<GLuint>(layout.VertexCount - 1),
                          static_cast<GLsizei>(this->lines.indexCount),
                          GL_UNSIGNED_INT,
                          reinterpret_cast<const GLvoid *>(NULL));
      }
    else
      {
      for (size_t eCount = 0; eCount < this->lines.offsetArray.size();
           ++eCount)
        {
        glDrawElements(GL_LINE_STRIP,
          this->lines.elementsArray[eCount],
          GL_UNSIGNED_INT,
          (GLvoid *)(this->lines.offsetArray[eCount]));
        }
      }
    this->lines.ibo.Release();
    }

  // draw polygons
  if (this->tris.indexCount)
    {
    // First we do the triangles, update the shader, set uniforms, etc.
    this->UpdateShader(this->tris, ren, actor);
    this->tris.ibo.Bind();
    if (actor->GetProperty()->GetRepresentation() == VTK_POINTS)
      {
      glDrawRangeElements(GL_POINTS, 0,
                          static_cast<GLuint>(layout.VertexCount - 1),
                          static_cast<GLsizei>(this->tris.indexCount),
                          GL_UNSIGNED_INT,
                          reinterpret_cast<const GLvoid *>(NULL));
      }
    if (actor->GetProperty()->GetRepresentation() == VTK_WIREFRAME)
      {
      // TODO wireframe of triangles is not lit properly right now
      // you either have to generate normals and send them down
      // or use a geometry shader.
      glMultiDrawElements(GL_LINE_LOOP,
                        (GLsizei *)(&this->tris.elementsArray[0]),
                        GL_UNSIGNED_INT,
                        reinterpret_cast<const GLvoid **>(&(this->tris.offsetArray[0])),
                        this->tris.offsetArray.size());
      }
    if (actor->GetProperty()->GetRepresentation() == VTK_SURFACE)
      {
      glDrawRangeElements(GL_TRIANGLES, 0,
                          static_cast<GLuint>(layout.VertexCount - 1),
                          static_cast<GLsizei>(this->tris.indexCount),
                          GL_UNSIGNED_INT,
                          reinterpret_cast<const GLvoid *>(NULL));
      }
    this->tris.ibo.Release();
    }

  // draw strips
  if (this->triStrips.indexCount)
    {
    // Use the tris shader program/VAO, but triStrips ibo.
    this->UpdateShader(this->triStrips, ren, actor);
    this->triStrips.ibo.Bind();
    if (actor->GetProperty()->GetRepresentation() == VTK_POINTS)
      {
      glDrawRangeElements(GL_POINTS, 0,
                          static_cast<GLuint>(layout.VertexCount - 1),
                          static_cast<GLsizei>(this->triStrips.indexCount),
                          GL_UNSIGNED_INT,
                          reinterpret_cast<const GLvoid *>(NULL));
      }
    if (actor->GetProperty()->GetRepresentation() == VTK_WIREFRAME)
      {
      for (size_t eCount = 0;
           eCount < this->triStrips.offsetArray.size(); ++eCount)
        {
        glDrawElements(GL_LINE_STRIP,
          this->triStrips.elementsArray[eCount],
          GL_UNSIGNED_INT,
          (GLvoid *)(this->triStrips.offsetArray[eCount]));
        }
      }
    if (actor->GetProperty()->GetRepresentation() == VTK_SURFACE)
      {
      for (size_t eCount = 0;
           eCount < this->triStrips.offsetArray.size(); ++eCount)
        {
        glDrawElements(GL_TRIANGLE_STRIP,
          this->triStrips.elementsArray[eCount],
          GL_UNSIGNED_INT,
          (GLvoid *)(this->triStrips.offsetArray[eCount]));
        }
      }
    this->triStrips.ibo.Release();
    }

}

//-----------------------------------------------------------------------------
void vtkOpenGLPolyDataMapper::RenderPieceFinish(vtkRenderer* ren, vtkActor *vtkNotUsed(actor))
{
  vtkHardwareSelector* selector = ren->GetSelector();
  if (selector && this->PopulateSelectionSettings)
    {
    selector->EndRenderProp();
    }

  if (this->lastBoundBO)
    {
    this->lastBoundBO->vao.Release();
    }

  this->VBO.Release();

  if ( this->GetResolveCoincidentTopology() )
    {
    glDisable(GL_POLYGON_OFFSET_FILL);
    }

  if (this->InternalColorTexture)
    {
    this->InternalColorTexture->PostRender(ren);
    }

  // If the timer is not accurate enough, set it to a small
  // time so that it is not zero
  if (this->TimeToDraw == 0.0)
    {
    this->TimeToDraw = 0.0001;
    }

  this->UpdateProgress(1.0);
}

//-----------------------------------------------------------------------------
void vtkOpenGLPolyDataMapper::RenderPiece(vtkRenderer* ren, vtkActor *actor)
{
  // Make sure that we have been properly initialized.
  if (ren->GetRenderWindow()->CheckAbortStatus())
    {
    return;
    }

  vtkDataObject *input= this->GetInputDataObject(0, 0);

  if (input == NULL)
    {
    vtkErrorMacro(<< "No input!");
    return;
    }

  this->InvokeEvent(vtkCommand::StartEvent,NULL);
  if (!this->Static)
    {
    this->GetInputAlgorithm()->Update();
    }
  this->InvokeEvent(vtkCommand::EndEvent,NULL);

  // if there are no points then we are done
  if (!this->GetInput()->GetPoints())
    {
    return;
    }

  this->RenderPieceStart(ren, actor);
  this->RenderPieceDraw(ren, actor);
  this->RenderPieceFinish(ren, actor);

  // if EdgeVisibility is on then draw the wireframe also
  this->RenderEdges(ren,actor);
}

void vtkOpenGLPolyDataMapper::RenderEdges(vtkRenderer* ren, vtkActor *actor)
{
  vtkProperty *prop = actor->GetProperty();
  bool draw_surface_with_edges =
    (prop->GetEdgeVisibility() && prop->GetRepresentation() == VTK_SURFACE);
  if (draw_surface_with_edges)
    {
    // store old values
    double f, u;
    this->GetResolveCoincidentTopologyPolygonOffsetParameters(f,u);
    double zRes = this->GetResolveCoincidentTopologyZShift();
    int oldRCT = this->GetResolveCoincidentTopology();
    vtkProperty *oldProp = vtkProperty::New();
    oldProp->DeepCopy(prop);

    // setup new values and render
    if (oldRCT == VTK_RESOLVE_SHIFT_ZBUFFER)
      {
      this->SetResolveCoincidentTopologyZShift(zRes*2.0);
      }
    else
      {
      this->SetResolveCoincidentTopology(VTK_RESOLVE_POLYGON_OFFSET);
      this->SetResolveCoincidentTopologyPolygonOffsetParameters(f+0.5,u*1.5);
      }
    prop->LightingOff();
    prop->SetAmbientColor(prop->GetEdgeColor());
    prop->SetAmbient(1.0);
    prop->SetDiffuse(0.0);
    prop->SetSpecular(0.0);
    prop->SetRepresentationToWireframe();
    this->RenderPieceStart(ren, actor);
    this->RenderPieceDraw(ren, actor);
    this->RenderPieceFinish(ren, actor);

    // restore old values
    prop->SetRepresentationToSurface();
    prop->SetLighting(oldProp->GetLighting());
    prop->SetAmbientColor(oldProp->GetAmbientColor());
    prop->SetAmbient(oldProp->GetAmbient());
    prop->SetDiffuse(oldProp->GetDiffuse());
    prop->SetSpecular(oldProp->GetSpecular());
    this->SetResolveCoincidentTopologyPolygonOffsetParameters(f,u);
    this->SetResolveCoincidentTopologyZShift(zRes);
    this->SetResolveCoincidentTopology(oldRCT);
    oldProp->UnRegister(this);

/*
    // Disable textures when rendering the surface edges.
    // This ensures that edges are always drawn solid.
    glDisable(GL_TEXTURE_2D);

    this->Information->Set(vtkPolyDataPainter::DISABLE_SCALAR_COLOR(), 1);
    this->Information->Remove(vtkPolyDataPainter::DISABLE_SCALAR_COLOR());

    // reset the default.
    glPolygonMode(face, GL_FILL);
    */
   }
}


//-------------------------------------------------------------------------
void vtkOpenGLPolyDataMapper::ComputeBounds()
{
  if (!this->GetInput())
    {
    vtkMath::UninitializeBounds(this->Bounds);
    return;
    }
  this->GetInput()->GetBounds(this->Bounds);
}

//-------------------------------------------------------------------------
void vtkOpenGLPolyDataMapper::UpdateVBO(vtkActor *act)
{
  vtkPolyData *poly = this->GetInput();
  if (poly == NULL)// || !poly->GetPointData()->GetNormals())
    {
    return;
    }

  // For vertex coloring, this sets this->Colors as side effect.
  // For texture map coloring, this sets ColorCoordinates
  // and ColorTextureMap as a side effect.
  // I moved this out of the conditional because it is fast.
  // Color arrays are cached. If nothing has changed,
  // then the scalars do not have to be regenerted.
  this->MapScalars(act->GetProperty()->GetOpacity());

  // If we are coloring by texture, then load the texture map.
  if (this->ColorTextureMap)
    {
    if (this->InternalColorTexture == 0)
      {
      this->InternalColorTexture = vtkOpenGLTexture::New();
      this->InternalColorTexture->RepeatOff();
      }
    this->InternalColorTexture->SetInputData(this->ColorTextureMap);
    }

  bool cellScalars = false;
  if (this->ScalarVisibility)
    {
    // We must figure out how the scalars should be mapped to the polydata.
    if ( (this->ScalarMode == VTK_SCALAR_MODE_USE_CELL_DATA ||
          this->ScalarMode == VTK_SCALAR_MODE_USE_CELL_FIELD_DATA ||
          this->ScalarMode == VTK_SCALAR_MODE_USE_FIELD_DATA ||
          !poly->GetPointData()->GetScalars() )
         && this->ScalarMode != VTK_SCALAR_MODE_USE_POINT_FIELD_DATA
         && this->Colors)
      {
      cellScalars = true;
      }
    }

  // if we have cell scalars then we have to
  // explode the data
  vtkCellArray *prims[4];
  prims[0] =  poly->GetVerts();
  prims[1] =  poly->GetLines();
  prims[2] =  poly->GetPolys();
  prims[3] =  poly->GetStrips();
  std::vector<unsigned int> cellPointMap;
  std::vector<unsigned int> pointCellMap;
  if (cellScalars)
    {
    vtkgl::CreateCellSupportArrays(poly, prims, cellPointMap, pointCellMap);
    }

  // do we have texture maps?
  bool haveTextures = (this->ColorTextureMap || act->GetTexture() || act->GetProperty()->GetNumberOfTextures());

  // Set the texture if we are going to use texture
  // for coloring with a point attribute.
  // fixme ... make the existence of the coordinate array the signal.
  vtkDataArray *tcoords = NULL;
  if (haveTextures)
    {
    if (this->InterpolateScalarsBeforeMapping && this->ColorCoordinates)
      {
      tcoords = this->ColorCoordinates;
      }
    else
      {
      tcoords = poly->GetPointData()->GetTCoords();
      }
    }

  // Iterate through all of the different types in the polydata, building OpenGLs
  // and IBOs as appropriate for each type.
  this->layout =
    CreateVBO(poly->GetPoints(),
              cellPointMap.size() > 0 ? cellPointMap.size() : poly->GetPoints()->GetNumberOfPoints(),
              (act->GetProperty()->GetInterpolation() != VTK_FLAT) ? poly->GetPointData()->GetNormals() : NULL,
              tcoords,
              this->Colors ? (unsigned char *)this->Colors->GetVoidPointer(0) : NULL,
              this->Colors ? this->Colors->GetNumberOfComponents() : 0,
              this->VBO,
              cellPointMap.size() > 0 ? &cellPointMap.front() : NULL,
              pointCellMap.size() > 0 ? &pointCellMap.front() : NULL);

  // create the IBOs
  this->points.indexCount = CreatePointIndexBuffer(prims[0],
                                                        this->points.ibo);

  if (act->GetProperty()->GetRepresentation() == VTK_POINTS)
    {
    this->lines.indexCount = CreatePointIndexBuffer(prims[1],
                         this->lines.ibo);

    this->tris.indexCount = CreatePointIndexBuffer(prims[2],
                                                this->tris.ibo);
    this->triStrips.indexCount = CreatePointIndexBuffer(prims[3],
                         this->triStrips.ibo);
    }
  else // WIREFRAME OR SURFACE
    {
    this->lines.indexCount = CreateMultiIndexBuffer(prims[1],
                           this->lines.ibo,
                           this->lines.offsetArray,
                           this->lines.elementsArray, false);

    if (act->GetProperty()->GetRepresentation() == VTK_WIREFRAME)
      {
      this->tris.indexCount = CreateMultiIndexBuffer(prims[2],
                                             this->tris.ibo,
                                             this->tris.offsetArray,
                                             this->tris.elementsArray, false);
      this->triStrips.indexCount = CreateMultiIndexBuffer(prims[3],
                           this->triStrips.ibo,
                           this->triStrips.offsetArray,
                           this->triStrips.elementsArray, true);
      }
   else // SURFACE
      {
      this->tris.indexCount = CreateTriangleIndexBuffer(prims[2],
                                                this->tris.ibo,
                                                poly->GetPoints());
      this->triStrips.indexCount = CreateMultiIndexBuffer(prims[3],
                           this->triStrips.ibo,
                           this->triStrips.offsetArray,
                           this->triStrips.elementsArray, false);
      }
    }

  // free up new cell arrays
  if (cellScalars)
    {
    for (int primType = 0; primType < 4; primType++)
      {
      prims[primType]->UnRegister(this);
      }
    }
}

//-----------------------------------------------------------------------------
bool vtkOpenGLPolyDataMapper::GetIsOpaque()
{
  // Straight copy of what the vtkPainterPolyDataMapper was doing.
  if (this->ScalarVisibility &&
    this->ColorMode == VTK_COLOR_MODE_DEFAULT)
    {
    vtkPolyData* input =
      vtkPolyData::SafeDownCast(this->GetInputDataObject(0, 0));
    if (input)
      {
      int cellFlag;
      vtkDataArray* scalars = this->GetScalars(input,
        this->ScalarMode, this->ArrayAccessMode, this->ArrayId,
        this->ArrayName, cellFlag);
      if (scalars && scalars->IsA("vtkUnsignedCharArray") &&
        (scalars->GetNumberOfComponents() ==  4 /*(RGBA)*/ ||
         scalars->GetNumberOfComponents() == 2 /*(LuminanceAlpha)*/))
        {
        vtkUnsignedCharArray* colors =
          static_cast<vtkUnsignedCharArray*>(scalars);
        if ((colors->GetNumberOfComponents() == 4 && colors->GetValueRange(3)[0] < 255) ||
          (colors->GetNumberOfComponents() == 2 && colors->GetValueRange(1)[0] < 255))
          {
          // If the opacity is 255, despite the fact that the user specified
          // RGBA, we know that the Alpha is 100% opaque. So treat as opaque.
          return false;
          }
        }
      }
    }
  return this->Superclass::GetIsOpaque();
}


//-----------------------------------------------------------------------------
void vtkOpenGLPolyDataMapper::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}
