///////////////////////////////////////////////////////////////////////////////
///
/// Author: Andrea Ellinger
/// Copyright 2017, DigiPen Institute of Technology
///
///////////////////////////////////////////////////////////////////////////////

#include "Precompiled.h"

namespace Audio
{
  //------------------------------------------------------------------------ Dynamics Processor Node

  //************************************************************************************************
  DynamicsProcessorNode::DynamicsProcessorNode(Zero::StringParam name, const unsigned ID, 
      ExternalNodeInterface *extInt, const bool isThreaded) :
    SimpleCollapseNode(name, ID, extInt, false, false, isThreaded),
    Filter(nullptr),
    InputGainDB(0),
    ThresholdDB(0),
    AttackMSec(20),
    ReleaseMSec(1000),
    Ratio(1),
    OutputGainDB(0),
    KneeWidth(0),
    ProcessorType(DynamicsProcessorTypes::Compressor)
  {
    if (!Threaded)
    {
      SetSiblingNodes(new DynamicsProcessorNode(name, ID, extInt, true));
    }
    else
    {
      Filter = new DynamicsProcessor();
    }
  }

  //************************************************************************************************
  DynamicsProcessorNode::~DynamicsProcessorNode()
  {
    if (Filter)
      delete Filter;
  }

  //************************************************************************************************
  float DynamicsProcessorNode::GetInputGain()
  {
    return InputGainDB;
  }

  //************************************************************************************************
  void DynamicsProcessorNode::SetInputGain(const float gainDB)
  {
    InputGainDB = gainDB;

    if (!Threaded)
      AddTaskForSibling(&DynamicsProcessorNode::SetInputGain, gainDB);
    else if (Filter)
      Filter->SetInputGain(gainDB);
  }

  //************************************************************************************************
  float DynamicsProcessorNode::GetThreshold()
  {
    return ThresholdDB;
  }

  //************************************************************************************************
  void DynamicsProcessorNode::SetThreshold(const float thresholdDB)
  {
    ThresholdDB = thresholdDB;

    if (!Threaded)
      AddTaskForSibling(&DynamicsProcessorNode::SetThreshold, thresholdDB);
    else if (Filter)
      Filter->SetThreshold(thresholdDB);
  }

  //************************************************************************************************
  float DynamicsProcessorNode::GetAttackMSec()
  {
    return AttackMSec;
  }

  //************************************************************************************************
  void DynamicsProcessorNode::SetAttackMSec(const float attack)
  {
    AttackMSec = attack;

    if (!Threaded)
      AddTaskForSibling(&DynamicsProcessorNode::SetAttackMSec, attack);
    else if (Filter)
      Filter->SetAttackMSec(attack);
  }

  //************************************************************************************************
  float DynamicsProcessorNode::GetReleaseMSec()
  {
    return ReleaseMSec;
  }

  //************************************************************************************************
  void DynamicsProcessorNode::SetReleaseMsec(const float release)
  {
    ReleaseMSec = release;

    if (!Threaded)
      AddTaskForSibling(&DynamicsProcessorNode::SetReleaseMsec, release);
    else if (Filter)
      Filter->SetReleaseMSec(release);
  }

  //************************************************************************************************
  float DynamicsProcessorNode::GetRatio()
  {
    return Ratio;
  }

  //************************************************************************************************
  void DynamicsProcessorNode::SetRatio(const float ratio)
  {
    Ratio = ratio;

    if (!Threaded)
      AddTaskForSibling(&DynamicsProcessorNode::SetRatio, ratio);
    else if (Filter)
      Filter->SetRatio(ratio);
  }

  //************************************************************************************************
  float DynamicsProcessorNode::GetOutputGain()
  {
    return OutputGainDB;
  }

  //************************************************************************************************
  void DynamicsProcessorNode::SetOutputGain(const float gainDB)
  {
    OutputGainDB = gainDB;

    if (!Threaded)
      AddTaskForSibling(&DynamicsProcessorNode::SetOutputGain, gainDB);
    else if (Filter)
      Filter->SetOutputGain(gainDB);
  }

  //************************************************************************************************
  float DynamicsProcessorNode::GetKneeWidth()
  {
    return KneeWidth;
  }

  //************************************************************************************************
  void DynamicsProcessorNode::SetKneeWidth(const float knee)
  {
    KneeWidth = knee;

    if (!Threaded)
      AddTaskForSibling(&DynamicsProcessorNode::SetKneeWidth, knee);
    else if (Filter)
      Filter->SetKneeWidth(knee);
  }

  //************************************************************************************************
  DynamicsProcessorTypes::Enum DynamicsProcessorNode::GetType()
  {
    return ProcessorType;
  }

  //************************************************************************************************
  void DynamicsProcessorNode::SetType(const DynamicsProcessorTypes::Enum type)
  {
    ProcessorType = type;

    if (!Threaded)
      AddTaskForSibling(&DynamicsProcessorNode::SetType, type);
    else if (Filter)
      Filter->SetType((DynamicsProcessor::ProcessorTypes)type);
  }

  //************************************************************************************************
  bool DynamicsProcessorNode::GetOutputSamples(BufferType *outputBuffer, const unsigned numberOfChannels,
    ListenerNode *listener, const bool firstRequest)
  {
    if (!Threaded)
      return false;

    unsigned bufferSize = outputBuffer->Size();

    // Get input and return if there is no data
    if (!AccumulateInputSamples(bufferSize, numberOfChannels, listener))
      return false;

    // Apply filter
    Filter->ProcessBuffer(InputSamples.Data(), InputSamples.Data(), outputBuffer->Data(),
      numberOfChannels, bufferSize);

    AddBypass(outputBuffer);

    return true;
  }

}