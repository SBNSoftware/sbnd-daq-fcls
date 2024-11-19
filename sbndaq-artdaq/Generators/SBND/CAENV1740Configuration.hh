//
//  CAENV1740Configuration.h   (based on sbndaq-artdaq/Generators/Commmon/CAENConfiguration.h)
//

#ifndef _CAENV1740Configuration_h
#define _CAENV1740Configuration_h

#include "sbndaq-artdaq/Generators/Common/CAENDecoder.hh"
#include "fhiclcpp/ParameterSet.h"
#include <iostream>

namespace sbndaq
{
class CAENV1740Configuration
{
  public:
  enum
  {
    MAX_BOARDS = 4,
    MAX_GROUPS = 8,
    MAX_CHANNELS = 64
  };

  virtual ~CAENV1740Configuration() {}
  CAENV1740Configuration(fhicl::ParameterSet const & ps);

    int  link;
    int  nBoards;
    int  enableReadout;
    int  boardId;
    int  recordLength;
    int  postPercent;
    int  eventsPerInterrupt;
    int  irqWaitTime;
    bool allowTriggerOverlap;
  //int  dynamicRange; // not available for V1740
    int  ioLevel;
    int  nChannels;
    int  nGroups;
  // jcrespo: comment trigger parameters not planned for V1740
  //int  triggerPolarity;
  //uint16_t triggerThresholds[MAX_CHANNELS];
  //uint8_t   triggerPulseWidth;
    int  extTrgMode;
    int  swTrgMode;
  //int  selfTrgMode;
    int  acqMode;
    int  debugLevel;
    int  runSyncMode;
    int  readoutMode;
  //    int  analogMode;
    int  testPattern;
    int  pedestal[MAX_GROUPS];
    int  groupEnable[MAX_GROUPS];
    uint32_t  groupEnableMask;
    // int  ovthValue;         
    // int  triggerLogic;  
    // int  majorityLevel; 
    // int  majorityCoincidenceWindow;

    void print(std::ostream & os = std::cout);
};
}

std::ostream& operator<<(std::ostream& os, const sbndaq::CAENV1740Configuration& e);

#include <trace.h>
namespace {
template <>
inline TraceStreamer &TraceStreamer::
operator<<(const sbndaq::CAENV1740Configuration &r) {
  std::ostringstream s;
  s << r;
  return *this;
}
} // namespace

#endif
