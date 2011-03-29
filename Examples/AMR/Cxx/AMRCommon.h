/*=========================================================================

  Program:   Visualization Toolkit
  Module:    AMRCommon.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
// .NAME AMRCommon.h -- Encapsulates common functionality for AMR data.
//
// .SECTION Description
// This header encapsulates some common functionality for AMR data to
// simplify and expedite the development of examples.

#ifndef AMRCOMMON_H_
#define AMRCOMMON_H_

#include "vtkHierarchicalBoxDataSet.h"
#include "vtkMultiBlockDataSet.h"

#include "vtkXMLHierarchicalBoxDataWriter.h"
#include "vtkXMLHierarchicalBoxDataReader.h"
#include "vtkXMLMultiBlockDataWriter.h"

namespace AMRCommon {



// Description:
// Writes the given AMR dataset to a *.vth file with the given prefix.
void WriteAMRData( vtkHierarchicalBoxDataSet *amrData, std::string prefix )
{
  // Sanity check
  assert( "pre: AMR dataset is NULL!" && (amrData != NULL) );

  vtkXMLHierarchicalBoxDataWriter *myAMRWriter=
   vtkXMLHierarchicalBoxDataWriter::New();
  assert( "pre: AMR Writer is NULL!" && (myAMRWriter != NULL) );

   std::ostringstream oss;
   oss << prefix << "." << myAMRWriter->GetDefaultFileExtension();

   myAMRWriter->SetFileName( oss.str().c_str() );
   myAMRWriter->SetInput( amrData );
   myAMRWriter->Write();
   myAMRWriter->Delete();
}

//------------------------------------------------------------------------------
// Description:
// Reads AMR data to the given data-structure from the prescribed file.
vtkHierarchicalBoxDataSet* ReadAMRData( std::string file )
{
  // Sanity check
//  assert( "pre: AMR dataset is NULL!" && (amrData != NULL) );

  vtkXMLHierarchicalBoxDataReader *myAMRReader=
   vtkXMLHierarchicalBoxDataReader::New();
  assert( "pre: AMR Reader is NULL!" && (myAMRReader != NULL) );

  std::ostringstream oss;
  oss.str("");
  oss.clear();
  oss << file << ".vthb";

  std::cout << "Reading AMR Data from: " << oss.str() << std::endl;
  std::cout.flush();

  myAMRReader->SetFileName( oss.str().c_str() );
  myAMRReader->Update();

  vtkHierarchicalBoxDataSet *amrData =
   vtkHierarchicalBoxDataSet::SafeDownCast( myAMRReader->GetOutput() );
  assert( "post: AMR data read is NULL!" && (amrData != NULL) );
  return( amrData );
}

//------------------------------------------------------------------------------
// Description:
// Writes the given multi-block data to an XML file with the prescribed prefix
void WriteMultiBlockData( vtkMultiBlockDataSet *mbds, std::string prefix )
{
  // Sanity check
  assert( "pre: Multi-block dataset is NULL" && (mbds != NULL) );
  vtkXMLMultiBlockDataWriter *writer = vtkXMLMultiBlockDataWriter::New();

  std::ostringstream oss;
  oss.str(""); oss.clear();
  oss << prefix << "." << writer->GetDefaultFileExtension();
  writer->SetFileName( oss.str( ).c_str( ) );
  writer->SetInput( mbds );
  writer->Write();
  writer->Delete();
}




} // END namespace

#endif /* AMRCOMMON_H_ */
