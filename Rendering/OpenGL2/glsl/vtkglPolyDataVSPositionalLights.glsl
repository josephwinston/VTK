/*=========================================================================

  Program:   Visualization Toolkit

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
// the lighting model for this shader is complex
// and supports the full VTK light API

// all variables that represent positions or directions have a suffix
// indicating the coordinate system they are in. The possible values are
// MC - Model Coordinates
// WC - WC world coordinates
// VC - View Coordinates
// DC - Display Coordinates

attribute vec4 vertexMC;
attribute vec3 normalMC;

// material property values
//VTK::Color::Dec
uniform vec3 ambientColor; // intensity weighted color
uniform vec3 specularColor; // intensity weighted color
uniform float specularPower;

// camera and actor matrix values
uniform mat4 MCWCMatrix;  // model to world matrix
uniform mat4 MCVCMatrix;  // combined Model to View transform
uniform mat4 VCDCMatrix;  // the camera's projection matrix
uniform mat3 normalMatrix; // transform model coordinate directions to view coordinates


uniform int numberOfLights; // only allow for up to 6 active lights
uniform vec3 lightColor[6]; // intensity weighted color
uniform vec3 lightDirectionVC[6]; // normalized
uniform vec3 lightPositionWC[6];
uniform vec3 lightAttenuation[6];
uniform float lightConeAngle[6];
uniform float lightExponent[6];
uniform int lightPositional[6];

// the resulting vertex color to be passed down to the fragment shader
varying vec4 fcolor;

void main()
{
  // compute the projected vertex position
  vec4 vertexVC = MCVCMatrix * vertexMC;
  gl_Position = VCDCMatrix * vertexVC;

  // now compute the vertex color
  vec3 normalVC = normalMatrix * normalMC;
  vec3 viewDirectionVC = normalize(vec3(0.0, 0.0, 1.0) - vertexVC.xyz);

  vec3 diffuse = vec3(0,0,0);
  vec3 specular = vec3(0,0,0);
  vec3 vertLightDirectionVC;

  for (int lightNum = 0; lightNum < numberOfLights; lightNum++)
    {
    float attenuation = 1.0;

    // directional
    if (lightPositional[lightNum] == 0)
      {
      vertLightDirectionVC = lightDirectionVC[lightNum];
      }
    else
      {
      vec4 vertexWC = MCWCMatrix * vertexMC;
      vec3 lightDirectionWC = vec3(vertexWC.xyz - lightPositionWC[lightNum]);
      float distanceWC = length(lightDirectionWC);
      vertLightDirectionVC = normalize(normalMatrix * lightDirectionWC);
      attenuation = 1.0 /
        (lightAttenuation[lightNum].x
         + lightAttenuation[lightNum].y * distanceWC
         + lightAttenuation[lightNum].z * distanceWC * distanceWC);

      // per OpenGL standard cone angle is 90 or less for a spot light
      if (lightConeAngle[lightNum] <= 90.0)
        {
        float coneDot = dot(vertLightDirectionVC, lightDirectionVC[lightNum]);
        // if inside the cone
        if (coneDot >= cos(radians(lightConeAngle[lightNum])))
          {
          attenuation = attenuation * pow(coneDot, lightExponent[lightNum]);
          }
        else
          {
          attenuation = 0.0;
          }
        }
      }

    // diffuse and specular lighting
    float df = max(0.0, attenuation*dot(normalVC, -vertLightDirectionVC));
    diffuse += (df * lightColor[lightNum]);

    if (dot(normalVC, -vertLightDirectionVC) > 0.0)
      {
      float sf = attenuation*pow( max(0.0, dot(
        reflect(vertLightDirectionVC, normalVC), viewDirectionVC)), specularPower);
      specular += (sf * lightColor[lightNum]);
      }
    }

  diffuse = diffuse * diffuseColor.rgb;
  specular = specular * specularColor;
  fcolor = vec4(ambientColor + diffuse + specular, diffuseColor.a);
}



