/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkOpenGLShaderCache.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkOpenGLShaderCache.h"
#include <GL/glew.h>

#include "vtkOpenGLRenderWindow.h"
#include "vtkOpenGL.h"
#include "vtkOpenGLError.h"

#include <math.h>

#include "vtkObjectFactory.h"

#include "vtkglVBOHelper.h"

#include "vtksys/MD5.h"

class vtkOpenGLShaderCache::Private
{
public:
  vtksysMD5* md5;

  // map of hash to shader program structs
  std::map<std::string, CachedShaderProgram *> ShaderPrograms;

  Private()
  {
  md5 = vtksysMD5_New();
  }

  ~Private()
  {
  vtksysMD5_Delete(this->md5);
  }

  //-----------------------------------------------------------------------------
  void ComputeMD5(const char* content,
                  const char* content2,
                  const char* content3,
                  std::string &hash)
  {
    unsigned char digest[16];
    char md5Hash[33];
    md5Hash[32] = '\0';

    vtksysMD5_Initialize(this->md5);
    vtksysMD5_Append(this->md5, reinterpret_cast<const unsigned char *>(content), (int)strlen(content));
    vtksysMD5_Append(this->md5, reinterpret_cast<const unsigned char *>(content2), (int)strlen(content2));
    vtksysMD5_Append(this->md5, reinterpret_cast<const unsigned char *>(content3), (int)strlen(content3));
    vtksysMD5_Finalize(this->md5, digest);
    vtksysMD5_DigestToHex(digest, md5Hash);

    hash = md5Hash;
  }


};

// ----------------------------------------------------------------------------
vtkStandardNewMacro(vtkOpenGLShaderCache);

// ----------------------------------------------------------------------------
vtkOpenGLShaderCache::vtkOpenGLShaderCache() : Internal(new Private)
{
  this->LastShaderBound  = NULL;
}

// ----------------------------------------------------------------------------
vtkOpenGLShaderCache::~vtkOpenGLShaderCache()
{
  delete this->Internal;
}

// return NULL if there is an issue
vtkOpenGLShaderCache::CachedShaderProgram *vtkOpenGLShaderCache::ReadyShader(
  const char *vertexCode,
  const char *fragmentCode,
  const char *geometryCode)
{
  CachedShaderProgram *shader = this->GetShader(vertexCode, fragmentCode, geometryCode);
  if (!shader)
    {
    return NULL;
    }

  // compile if needed
  if (!shader->Compiled && !this->CompileShader(shader))
    {
    return NULL;
    }

  // bind if needed
  if (!this->BindShader(shader))
    {
    return NULL;
    }

  return shader;
}

// return NULL if there is an issue
vtkOpenGLShaderCache::CachedShaderProgram *vtkOpenGLShaderCache::ReadyShader(
    vtkOpenGLShaderCache::CachedShaderProgram *shader)
{
  // compile if needed
  if (!shader->Compiled && !this->CompileShader(shader))
    {
    return NULL;
    }

  // bind if needed
  if (!this->BindShader(shader))
    {
    return NULL;
    }

  return shader;
}

vtkOpenGLShaderCache::CachedShaderProgram *vtkOpenGLShaderCache::GetShader(
  const char *vertexCode,
  const char *fragmentCode,
  const char *geometryCode)
{
  // compute the MD5 and the check the map
  std::string result;
  this->Internal->ComputeMD5(vertexCode, fragmentCode, geometryCode, result);

  // does it already exist?
  typedef std::map<std::string,vtkOpenGLShaderCache::CachedShaderProgram*>::const_iterator SMapIter;
  SMapIter found = this->Internal->ShaderPrograms.find(result);
  if (found == this->Internal->ShaderPrograms.end())
    {
    // create one
    vtkOpenGLShaderCache::CachedShaderProgram *sps = new vtkOpenGLShaderCache::CachedShaderProgram();
    sps->VS.SetSource(vertexCode);
    sps->VS.SetType(vtkgl::Shader::Vertex);
    sps->FS.SetSource(fragmentCode);
    sps->FS.SetType(vtkgl::Shader::Fragment);
    if (geometryCode != NULL)
      {
      sps->GS.SetSource(geometryCode);
      sps->GS.SetType(vtkgl::Shader::Geometry);
      }
    sps->Compiled = false;
    sps->md5Hash = result;
    this->Internal->ShaderPrograms.insert(std::make_pair(result, sps));
    return sps;
    }
  else
    {
    return found->second;
    }
}

// return 0 if there is an issue
int vtkOpenGLShaderCache::CompileShader(vtkOpenGLShaderCache::CachedShaderProgram* shader)
{
  if (!shader->VS.Compile())
    {
    vtkErrorMacro(<< shader->VS.GetError());
    return 0;
    }
  if (!shader->FS.Compile())
    {
    vtkErrorMacro(<< shader->FS.GetError());
    return 0;
    }
  if (!shader->Program.AttachShader(shader->VS))
    {
    vtkErrorMacro(<< shader->Program.GetError());
    return 0;
    }
  if (!shader->Program.AttachShader(shader->FS))
    {
    vtkErrorMacro(<< shader->Program.GetError());
    return 0;
    }
  if (!shader->Program.Link())
    {
    vtkErrorMacro(<< "Links failed: " << shader->Program.GetError());
    return 0;
    }

  shader->Compiled = true;
  return 1;
}


int vtkOpenGLShaderCache::BindShader(vtkOpenGLShaderCache::CachedShaderProgram* shader)
{
  if (this->LastShaderBound == shader)
    {
    return 1;
    }

  // release prior shader
  if (this->LastShaderBound)
    {
    this->LastShaderBound->Program.Release();
    }
  shader->Program.Bind();
  this->LastShaderBound = shader;
  return 1;
}


// ----------------------------------------------------------------------------
void vtkOpenGLShaderCache::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
}
