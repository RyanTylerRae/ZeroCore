///////////////////////////////////////////////////////////////////////////////
///
/// Author: Andrea Ellinger
/// Copyright 2017, DigiPen Institute of Technology
///
///////////////////////////////////////////////////////////////////////////////

#include "Precompiled.h"

namespace Audio
{
  //------------------------------------------------------------------------------------- Sound Node

  //************************************************************************************************
  SoundNode::SoundNode(Zero::StringParam name, const unsigned ID, ExternalNodeInterface* extInt, 
      const bool listenerDependent, const bool generator, const bool isThreaded) :
    SiblingNode(nullptr),
    InProcess(false), 
    ExternalData(extInt), 
    DeleteMe(false), 
    NumMixedChannels(0), 
    MixedListener(nullptr), 
    Threaded(isThreaded),
    Collapse(false), 
    BypassValue(0.0f),
    Name(name),
    NodeID(ID),
    ValidOutputLastMix(false),
    ListenerDependent(listenerDependent),
    Generator(generator)
  {
    // Add to the list of nodes
    if (!Threaded)
    {
      gAudioSystem->AddSoundNode(this, false);
    }
    else
    {
      Version = gAudioSystem->MixVersionNumber - 1;

      // Constructor happens on main thread, so need to send a message to add to threaded list
      gAudioSystem->AddTask(Zero::CreateFunctor(&AudioSystemInternal::AddSoundNode, gAudioSystem, 
        this, true));
    }
  }

  //************************************************************************************************
  SoundNode::~SoundNode()
  {
    // Remove from the list of nodes
    gAudioSystem->RemoveSoundNode(this, Threaded);
  }

  //************************************************************************************************
  void SoundNode::AddInput(SoundNode* newNode)
  {
    // If new node is not valid, return
    if (!newNode)
      return;

    // Check if this node is already an input
    if (Inputs.Contains(newNode))
      return;

    // Add new node to this node's inputs
    Inputs.PushBack(newNode);
    // Add this node to the new node's outputs
    newNode->Outputs.PushBack(this);

    // If not threaded, send a message to the threaded node
    if (!Threaded && newNode->SiblingNode)
    {
      AddTaskForSibling(&SoundNode::AddInput, newNode->SiblingNode);
    }
  }

  //************************************************************************************************
  void SoundNode::RemoveInput(SoundNode* node)
  {
    // Remove node from input list
    Inputs.EraseValue(node);

    // Remove this node from the input node's output list
    node->Outputs.EraseValue(this);

    // If not threaded, send a message to the threaded node
    if (!Threaded)
    {
      if (node->SiblingNode)
        AddTaskForSibling(&SoundNode::RemoveInput, node->SiblingNode);

      if (Inputs.Empty())
      {
        // If there are no more inputs and this node should collapse, call the collapse function
        if (Collapse)
          CollapseNode();
        // If no external data, node is unusable, should be deleted
        else if (!ExternalData)
          DeleteThisNode();
      }

      // Check if this node is now disconnected (sends message or deletes node)
      CheckIfDisconnected();

      // Check if the input node is now disconnected
      node->CheckIfDisconnected();
    }
  }

  //************************************************************************************************
  void SoundNode::InsertNodeBefore(SoundNode* newNode)
  {
    // Don't run this function on threaded nodes
    if (!newNode || Threaded)
      return;

    // If there are no current inputs, just add the new node as input
    if (Inputs.Empty())
    {
      AddInput(newNode);
      return;
    }

    // Add all of this node's inputs to the new node and remove them from this node
    while (!Inputs.Empty())
    {
      newNode->AddInput(Inputs.Back());
      RemoveInput(Inputs.Back());
    }

    // Add new node as only input
    AddInput(newNode);
  }

  //************************************************************************************************
  void SoundNode::InsertNodeAfter(SoundNode* newNode)
  {
    // Don't run this function on threaded nodes
    if (!newNode || Threaded)
      return;

    // If there are no current outputs, just add this to the new node
    if (Outputs.Empty())
    {
      newNode->AddInput(this);
      return;
    }

    // Remove this node from the inputs of all output nodes and add new node
    while (!Outputs.Empty())
    {
      Outputs.Back()->AddInput(newNode);
      Outputs.Back()->RemoveInput(this);
    }

    // Add new node as this node's only output
    newNode->AddInput(this);
  }

  //************************************************************************************************
  void SoundNode::ReplaceWith(SoundNode* replacementNode)
  {
    if (Threaded)
      return;

    // Remove this node as an output from all inputs and add to replacement node
    while (!Inputs.Empty())
    {
      SoundNode* inputNode = Inputs.Back();
      // Check if the replacement node is the same as the input node
      if (replacementNode == inputNode)
        // Can't add to itself, just remove from Inputs
        Inputs.PopBack();
      else
      {
        RemoveInput(inputNode);
        if (replacementNode)
          replacementNode->AddInput(inputNode);
      }
    }

    // Remove this node as an input from all outputs and add replacement node
    while (!Outputs.Empty())
    {
      SoundNode* outputNode = Outputs.Back();
      // Check if the replacement node is the same as the output node
      if (replacementNode == outputNode)
        // Can't add to itself, just remove from Outputs
        Outputs.PopBack();
      else
      {
        outputNode->RemoveInput(this);
        if (replacementNode)
          outputNode->AddInput(replacementNode);
      }
    }

    // Don't need to check for deleting node - handled by RemoveInput
  }

  //************************************************************************************************
  void SoundNode::DisconnectInputs()
  {
    // Don't run this function on threaded nodes
    if (Threaded)
      return;

    // Remove this node as output for all inputs
    while (!Inputs.Empty())
      RemoveInput(Inputs.Back());
  }

  //************************************************************************************************
  void SoundNode::DisconnectOutputs()
  {
    // Don't run this function on threaded nodes
    if (Threaded)
      return;

    // Remove this node as input for all outputs
    while (!Outputs.Empty())
      Outputs.Back()->RemoveInput(this);
  }

  //************************************************************************************************
  void SoundNode::DisconnectOnlyThis()
  {
    // Don't run this function on threaded nodes
    if (Threaded)
      return;

    // Add all inputs to all outputs
    forRange(SoundNode* input, Inputs.All())
    {
      forRange(SoundNode* output, Outputs.All())
        output->AddInput(input);
    }

    DisconnectOutputs();
    DisconnectInputs();

    ErrorIf(!Inputs.Empty() || !Outputs.Empty(), "Audio Engine: Node was not fully disconnected");

    // Don't need to check for deleting node - handled by RemoveInput
  }

  //************************************************************************************************
  void SoundNode::DisconnectThisAndAllInputs()
  {
    if (Threaded)
      return;

    // Call this function on all input nodes (removes inputs)
    while (!Inputs.Empty())
      Inputs.Back()->DisconnectThisAndAllInputs();

    // Remove this node as input for all outputs
    while (!Outputs.Empty())
      Outputs.Back()->RemoveInput(this);

    ErrorIf(!Inputs.Empty() || !Outputs.Empty(), "Audio Engine: Node was not fully disconnected");

    // Don't need to check for deleting node - handled by RemoveInput
  }

  //************************************************************************************************
  void SoundNode::DeleteThisNode()
  {
    if (!Threaded)
    {
      if (!SiblingNode || InProcess)
        return;

      InProcess = true;

      // If this node is still connected, disconnect it
      if (!Inputs.Empty() || !Outputs.Empty())
        DisconnectOnlyThis();

      // Send a message to the threaded node
      AddTaskForSibling(&SoundNode::DeleteThisNode);

      // Can't send any more messages to threaded node, so get rid of pointer
      SiblingNode = NULL;

      ExternalData = nullptr;

      InProcess = false;

      // Wait until receive message from threaded node to actually delete
    }
    else
    {
      // Send a message to the non-threaded node
      gAudioSystem->AddTaskThreaded(Zero::CreateFunctor(&SoundNode::OkayToDelete, SiblingNode));

      ErrorIf(!Inputs.Empty() || !Outputs.Empty(), "Audio Engine: Node was not fully disconnected");
      
      // This happens during task handling, and will be the last task for this object,
      // so it's okay to delete immediately
      delete this;
    }
  }

  //************************************************************************************************
  const Zero::Array<SoundNode*>* SoundNode::GetInputs()
  {
    return &Inputs;
  }

  //************************************************************************************************
  const Zero::Array<SoundNode*>* SoundNode::GetOutputs()
  {
    return &Outputs;
  }

  //************************************************************************************************
  bool SoundNode::HasInputs()
  {
    return !Inputs.Empty();
  }

  //************************************************************************************************
  bool SoundNode::HasOutputs()
  {
    return !Outputs.Empty();
  }

  //************************************************************************************************
  void SoundNode::SetExternalInterface(ExternalNodeInterface* externalData)
  {
    if (Threaded)
      return;

    // Check if the external data is null
    if (!externalData)
    {
      // If this node is already marked to be deleted, delete it
      if (DeleteMe)
        // This happens during task handling, and will be the last task for this object,
        // so it's okay to delete immediately
        delete this;
      // If this node has no inputs, call the deletion function
      // (even if it has outputs, no more inputs can be added, so node is unusable)
      else if (Inputs.Empty() && !Generator)
        DeleteThisNode();
    }

    ExternalData = externalData;
  }

  //************************************************************************************************
  bool SoundNode::GetCollapse()
  {
    return Collapse;
  }

  //************************************************************************************************
  void SoundNode::SetCollapse(const bool shouldCollapse)
  {
    Collapse = shouldCollapse;

    // If not threaded, send message to threaded node
    if (!Threaded)
      AddTaskForSibling(&SoundNode::SetCollapse, shouldCollapse);
  }

  //************************************************************************************************
  bool SoundNode::HasAudibleOutput()
  {
    return ValidOutputLastMix;
  }

  //************************************************************************************************
  float SoundNode::GetBypassValue()
  {
    return BypassValue;
  }

  //************************************************************************************************
  void SoundNode::SetBypassValue(const float bypassValue)
  {
    BypassValue = bypassValue;

    // If not threaded, send message to threaded node
    if (!Threaded)
      AddTaskForSibling(&SoundNode::SetBypassValue, bypassValue);
  }

  //************************************************************************************************
  void SoundNode::SendEventToExternalData(AudioEventTypes::Enum eventType)
  {
    if (Threaded)
      return;

    // If the interface exists, send the event
    if (ExternalData)
      ExternalData->SendAudioEvent(eventType);
  }

  //************************************************************************************************
  void SoundNode::SendEventDataToExternalData(EventData* data)
  {
    if (Threaded)
      return;

    // If the interface exists, send the event
    if (ExternalData)
      ExternalData->SendAudioEventData(data);
  }

  //************************************************************************************************
  float SoundNode::GetVolumeChangeFromOutputs()
  {
    if (!Threaded)
      return 0.0f;

    float volume = 0.0f;
    forRange(SoundNode* node, Outputs.All())
      volume += node->GetVolumeChangeFromOutputs();

    return volume;
  }

  //************************************************************************************************
  bool SoundNode::Evaluate(BufferType* outputBuffer, const unsigned numberOfChannels, 
    ListenerNode* listener)
  {
    // Only run on threaded nodes
    if (!Threaded)
      return false;

    bool hasOutput;

    // Check if this version has already been mixed
    if (gAudioSystem->MixVersionNumber == Version)
    {
      // Check for errors 
      Zero::String message;
      if (InProcess)
        message = Zero::String::Format("Loop in sound node structure, disconnected %s", Name.c_str());
      else if (outputBuffer->Size() != MixedOutput.Size())
        message = Zero::String::Format("Mismatch in buffer size requests on sound node, disconnected %s", Name.c_str());
      else if (numberOfChannels != NumMixedChannels)
        message = Zero::String::Format("Mismatch in channel requests on sound node, disconnected %s", Name.c_str());
      if (!message.Empty())
      {
        gAudioSystem->AddTaskThreaded(Zero::CreateFunctor(&ExternalSystemInterface::SendAudioError, 
            gAudioSystem->ExternalInterface, message));

        AddTaskForSiblingThreaded(&SoundNode::DisconnectOnlyThis);

        return false;
      }

      // If this node is saved, or if the listener matches the last mix, can simply copy data
      if (OkayToSave || listener == MixedListener)
      {
        // Copy mixed samples to output buffer if there is real data
        if (ValidOutputLastMix)
          memcpy(outputBuffer->Data(), MixedOutput.Data(), sizeof(float) * outputBuffer->Size());

        hasOutput = ValidOutputLastMix;
      }
      // Otherwise, need to get output again
      else
      {
        InProcess = true;
        MixedListener = listener;

        // Get output
        hasOutput = GetOutputSamples(&MixedOutput, numberOfChannels, listener, false);
        if (!ValidOutputLastMix && hasOutput)
        {
          ValidOutputLastMix = true;

          if (GetSiblingNode())
            gAudioSystem->AddTaskThreaded(Zero::CreateFunctor(&SoundNode::ValidOutputLastMix,
              GetSiblingNode(), ValidOutputLastMix));
        }
        // Copy mixed samples to output buffer if there is real data
        if (hasOutput)
          memcpy(outputBuffer->Data(), MixedOutput.Data(), sizeof(float) * outputBuffer->Size());

        InProcess = false;
      }
    }
    // Hasn't been mixed yet
    else
    {
      InProcess = true;
      Version = gAudioSystem->MixVersionNumber;
      NumMixedChannels = numberOfChannels;
      MixedListener = listener;

      // Set mixed array to same size as output array
      MixedOutput.Resize(outputBuffer->Size());

      // Check whether this node's data can be saved
      OkayToSave = !ListenerDependent;
      // If this node can be saved, check all its inputs to verify
      // (if any inputs can't be saved, neither can this node)
      if (OkayToSave)
      {
        forRange(SoundNode* input, Inputs.All())
        {
          if (!input->OkayToSave)
          {
            OkayToSave = false;
            break;
          }
        }
      }

      // Get output
      hasOutput = GetOutputSamples(&MixedOutput, numberOfChannels, listener, true);

      // If the output state has changed, notify the non-threaded node
      if (ValidOutputLastMix != hasOutput && GetSiblingNode())
          gAudioSystem->AddTaskThreaded(Zero::CreateFunctor(&SoundNode::ValidOutputLastMix,
            GetSiblingNode(), hasOutput));

      ValidOutputLastMix = hasOutput;

      // Copy mixed samples to output buffer if there is real data
      if (hasOutput)
      {
        ErrorIf(outputBuffer->Size() != MixedOutput.Size(), "Buffer sizes do not match when evaluating sound node");
        memcpy(outputBuffer->Data(), MixedOutput.Data(), sizeof(float) * MixedOutput.Size());
      }

      // Mark as finished
      InProcess = false;
    }

    return hasOutput;
  }

  //************************************************************************************************
  bool SoundNode::AccumulateInputSamples(const unsigned howManySamples, const unsigned numberOfChannels, 
    ListenerNode* listener)
  {
    if (!Threaded)
      return false;

    // No sources, do nothing
    if (Inputs.Empty())
      return false;

    BufferType tempBuffer(howManySamples);
    bool isThereInput(false);

    // Reset buffer
    InputSamples.Resize(howManySamples);

    // Get samples from all inputs
    forRange(SoundNode* input, Inputs.All())
    {
      // Check if this input has actual output data
      if (input->Evaluate(&tempBuffer, numberOfChannels, listener))
      {
        // If this is the first input data, just swap the buffers
        if (!isThereInput)
        {
          isThereInput = true;
          InputSamples.Swap(tempBuffer);
        }
        // Otherwise add the new samples to the existing ones
        else
        {
          for (BufferRange myData = InputSamples.All(), newData = tempBuffer.All(); !myData.Empty(); 
            myData.PopFront(), newData.PopFront())
          {
            myData.Front() += newData.Front();
          }
        }
      }
    }

    return isThereInput;
  }

  //************************************************************************************************
  bool SoundNode::HasExternalInterface()
  {
    return ExternalData != nullptr;
  }

  //************************************************************************************************
  SoundNode* SoundNode::GetSiblingNode()
  {
    return SiblingNode;
  }

  //************************************************************************************************
  void SoundNode::SetSiblingNodes(SoundNode* threadedNode)
  {
    // Only run this on the non-threaded node
    if (!Threaded && threadedNode)
    {
      // Set this node's sibling pointer
      SiblingNode = threadedNode;
      // Send the threaded node's sibling pointer (this is called in the non-threaded
      // node's constructor so don't need to add a task for the other thread)
      threadedNode->SiblingNode = this;
    }
  }

  //************************************************************************************************
  void SoundNode::AddBypass(BufferType* outputBuffer)
  {
    if (!Threaded)
      return;

    // If some of the node's output should be bypassed, adjust the output buffer 
    // with a percentage of the input buffer
    if (BypassValue > 0.0f)
    {
      for (BufferRange outputRange = outputBuffer->All(), inputRange = InputSamples.All();
        !outputRange.Empty(); outputRange.PopFront(), inputRange.PopFront())
      {
        outputRange.Front() = (inputRange.Front() * BypassValue) 
          + (outputRange.Front() * (1.0f - BypassValue));
      }
    }
  }

  //************************************************************************************************
  void SoundNode::CheckIfDisconnected()
  {
    if (Threaded)
      return;

    // Check if this node is now disconnected
    if (Inputs.Empty() && Outputs.Empty())
    {
      // Send a message to the external interface if it exists
      if (ExternalData)
        ExternalData->SendAudioEvent(AudioEventTypes::NodeDisconnected);
      // Otherwise, delete the node
      else
        DeleteThisNode();
    }
  }

  //************************************************************************************************
  void SoundNode::OkayToDelete()
  {
    if (Threaded)
      return;

    // If there is still an external interface, set the flag to delete when it's removed
    if (ExternalData)
      DeleteMe = true;
    // Otherwise, can delete now (this node is always non-threaded)
    else if (!DeleteMe)
      // This happens during task handling, and will be the last task for this object,
      // so it's okay to delete immediately
      delete this;
  }

  //------------------------------------------------------------------------------------ Output Node

  //************************************************************************************************
  OutputNode::OutputNode(Zero::StringParam name, ExternalNodeInterface* nodeInt, bool isThreaded) :
    SoundNode(name, 0, nodeInt, false, false, isThreaded)
  {
    if (!isThreaded)
      SetSiblingNodes(new OutputNode("ThreadedOutputNode", nodeInt, true));
  }

  //************************************************************************************************
  bool OutputNode::GetOutputSamples(BufferType* outputBuffer, const unsigned numberOfChannels,
    ListenerNode* listener, const bool firstRequest)
  {
    if (!Threaded)
      return false;

    // Get input
    bool isThereOutput = AccumulateInputSamples(outputBuffer->Size(), numberOfChannels, nullptr);

    // Move input to output buffer
    if (isThereOutput)
      InputSamples.Swap(*outputBuffer);

    return isThereOutput;
  }

  //************************************************************************************************
  float OutputNode::GetVolumeChangeFromOutputs()
  {
    return 1.0f;
  }

  //--------------------------------------------------------------------------- Simple Collapse Node

  //************************************************************************************************
  void SimpleCollapseNode::CollapseNode()
  {
    if (Threaded)
      return;

    // If node has inputs, don't collapse
    if (HasInputs())
      return;

    // If there are no more inputs, remove this node from all outputs
    DisconnectOutputs();
  }

  //----------------------------------------------------------------------------------- Combine Node

  //************************************************************************************************
  CombineNode::CombineNode(Zero::StringParam name, unsigned ID, ExternalNodeInterface* extInt, 
      bool isThreaded) :
    SimpleCollapseNode(name, ID, extInt, false, false, isThreaded)
  {
    if (!Threaded)
      SetSiblingNodes(new CombineNode(name, ID, extInt, true));
  }

  //************************************************************************************************
  bool CombineNode::GetOutputSamples(BufferType* outputBuffer, const unsigned numberOfChannels,
    ListenerNode* listener, const bool firstRequest)
  {
    if (!Threaded)
      return false;

    // Get input
    bool isThereOutput = AccumulateInputSamples(outputBuffer->Size(), numberOfChannels, listener);

    // Move input to output buffer
    if (isThereOutput)
      InputSamples.Swap(*outputBuffer);

    return isThereOutput;
  }

  //------------------------------------------------------------------------- Combine And Pause Node

  //************************************************************************************************
  CombineAndPauseNode::CombineAndPauseNode(Zero::StringParam name, unsigned ID, 
      ExternalNodeInterface* extInt, bool isThreaded) :
    SimpleCollapseNode(name, ID, extInt, false, false, isThreaded),
    Paused(false),
    Pausing(false),
    Muted(false),
    Muting(false),
    Interpolating(false)
  {
    if (!Threaded)
      SetSiblingNodes(new CombineAndPauseNode(name, ID, extInt, true));
  }

  //************************************************************************************************
  bool CombineAndPauseNode::GetPaused()
  {
    return Paused;
  }

  //************************************************************************************************
  void CombineAndPauseNode::SetPaused(const bool paused)
  {
    if (!Threaded)
    {
      AddTaskForSibling(&CombineAndPauseNode::SetPaused, paused);

      Paused = paused;
    }
    else
    {
      // If we should pause and we are currently not paused and not pausing
      if (paused && !Paused && !Pausing)
      {
        // If we are muted we don't need to interpolate volume
        if (Muted)
        {
          Paused = true;
        }
        else
        {
          Pausing = true;

          // Only set the interpolator if we haven't already set it for muting
          if (!Muting)
          {
            Interpolating = true;
            VolumeInterpolator.SetValues(1.0f, 0.0f, PropertyChangeFrames);
          }
        }
      }
      // If we should un-pause and we are currently paused
      else if (!paused && Paused)
      {
        Paused = Pausing = false;

        // If we are muted or muting we don't need to interpolate volume
        if (!Muted && !Muting)
        {
          Interpolating = true;
          VolumeInterpolator.SetValues(0.0f, 1.0f, PropertyChangeFrames);
        }
      }
    }
  }

  //************************************************************************************************
  bool CombineAndPauseNode::GetMuted()
  {
    return Muted;
  }

  //************************************************************************************************
  void CombineAndPauseNode::SetMuted(bool muted)
  {
    if (!Threaded)
    {
      AddTaskForSibling(&CombineAndPauseNode::SetMuted, muted);

      Muted = muted;
    }
    else
    {
      // If we should mute and we are currently not muted and not muting
      if (muted && !Muted && !Muting)
      {
        // If we are paused we don't need to interpolate volume
        if (Paused)
        {
          Muted = true;
        }
        else
        {
          Muting = true;

          // Only set the interpolator if we haven't already set it for pausing
          if (!Pausing)
          {
            Interpolating = true;
            VolumeInterpolator.SetValues(1.0f, 0.0f, PropertyChangeFrames);
          }
        }
      }
      // If we should un-mute and we are currently muted
      else if (!muted && Muted)
      {
        Muted = Muting = false;

        // If we are paused or pausing we don't need to interpolate volume
        if (!Paused && !Pausing)
        {
          Interpolating = true;
          VolumeInterpolator.SetValues(0.0f, 1.0f, PropertyChangeFrames);
        }
      }
    }
  }

  //************************************************************************************************
  bool CombineAndPauseNode::GetOutputSamples(BufferType* outputBuffer, const unsigned numberOfChannels,
    ListenerNode* listener, const bool firstRequest)
  {
    if (!Threaded)
      return false;

    // Check if we are paused (don't need to process or return audio)
    if (Paused)
      return false;

    // Get input
    bool isThereOutput = AccumulateInputSamples(outputBuffer->Size(), numberOfChannels, listener);

    // Check if we are muted (need to process audio, don't need to return any)
    if (Muted)
      return false;

    // Move input to output buffer
    if (isThereOutput)
    {
      // Check if we're interpolating volume
      if (Interpolating)
      {
        // Apply volume adjustment
        float volume;
        BufferRange inputRange = InputSamples.All();
        for (unsigned i = 0; i < outputBuffer->Size(); i += numberOfChannels)
        {
          volume = VolumeInterpolator.NextValue();

          // Apply the volume multiplier to all samples
          for (unsigned j = 0; j < numberOfChannels; ++j, inputRange.PopFront())
            inputRange.Front() *= volume;
        }

        // Check if we're done interpolating
        if (VolumeInterpolator.Finished())
        {
          Interpolating = false;
          if (Pausing)
            Paused = true;
          if (Muting)
            Muted = true;
        }
      }

      InputSamples.Swap(*outputBuffer);
    }

    return isThereOutput;
  }

}