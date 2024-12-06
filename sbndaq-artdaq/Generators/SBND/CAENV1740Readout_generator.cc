//
//  sbndaq-artdaq/Generators/SBND/CAENV1740Readout_generator.cc
//

#define TRACE_NAME "CAENV1740Readout"
#include "artdaq/DAQdata/Globals.hh"

#include "artdaq/Generators/GeneratorMacros.hh"
#include "sbndaq-artdaq/Generators/SBND/CAENV1740Readout.hh"

#include <iostream>
#include <sstream>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include "sbndaq-artdaq/Generators/Common/CAENDecoder.hh"
#include "sbndaq-artdaq-core/Overlays/FragmentType.hh"

#include "boost/date_time/microsec_time_clock.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"

using namespace sbndaq;

// constructor of the CAENV1740Readout. It wants the param set 
// which means the fhicl paramters in CAENV1740Readout.hh

sbndaq::CAENV1740Readout::CAENV1740Readout(fhicl::ParameterSet const& ps) :
  CommandableFragmentGenerator(ps),
  fCAEN(ps),
  fAcqMode(CAEN_DGTZ_SW_CONTROLLED)
{
  uint32_t data;

  TLOG_ARB(TCONFIG,TRACE_NAME) << "CAENV1740Readout()" << TLOG_ENDL;
  TLOG(TCONFIG) << fCAEN;
  loadConfiguration(ps);
  
  last_rcvd_rwcounter=0x0;
  last_sent_seqid=0x1;
  last_sent_ts=0;
  CAEN_DGTZ_ErrorCode retcode;

  fail_GetNext=false;

  fNChannels = fCAEN.nChannels;
  fNGroups = fCAEN.nGroups;
  fBoardID = fCAEN.boardId;

  TLOG(TCONFIG) << ": Using BoardID=" << fBoardID << " with NChannels=" 
		<< fNChannels;

  // jcrespo: hardcoded physical communication channel
  retcode = CAEN_DGTZ_OpenDigitizer(CAEN_DGTZ_OpticalLink, fCAEN.link, 
				    fBoardChainNumber, 0, &fHandle);

  fOK=true;

  if(retcode != CAEN_DGTZ_Success)
  {
    sbndaq::CAENDecoder::checkError(retcode,"OpenDigitizer",fBoardID);
		CAEN_DGTZ_CloseDigitizer(fHandle);
    fHandle = -1;
    fOK = false;
    TLOG(TLVL_ERROR) << ": Fatal error configuring CAEN board at " << 
      fCAEN.link << ", " << fBoardChainNumber;
    TLOG(TLVL_ERROR) << "Terminating process";
    abort();
  }
 
  // check current firmware/software versions
  GetSWInfo();
  
  retcode = CAEN_DGTZ_Reset(fHandle);
  sbndaq::CAENDecoder::checkError(retcode,"Reset",fBoardID);
  
  sleep(1);
  Configure();

  retcode = CAEN_DGTZ_ReadRegister(fHandle,FP_TRG_OUT_CONTROL,&data);
  TLOG(TLVL_INFO) << "FP_TRG_OUT_CONTROL Reg:0x" << std::hex << FP_TRG_OUT_CONTROL << 
    "=0x" << data;

  retcode = CAEN_DGTZ_ReadRegister(fHandle,FP_IO_CONTROL,&data);
  TLOG(TLVL_INFO) << "FP_IO_CONTROL Reg:0x" << std::hex << FP_IO_CONTROL << "=0x" << data;

  retcode = CAEN_DGTZ_ReadRegister(fHandle,FP_LVDS_CONTROL,&data);
  TLOG(TLVL_INFO) << "FP_LVDS_CONTROL Reg:0x" << std::hex << FP_LVDS_CONTROL << "=0x" << 
    data << std::dec;

  if(!fOK)
  {
    CAEN_DGTZ_CloseDigitizer(fHandle);
    TLOG(TLVL_ERROR) << ": Fatal error configuring CAEN board at " << 
      fCAEN.link << ", " << fBoardChainNumber;
    TLOG(TLVL_ERROR) << "Terminating process";
    abort();
  }

  // Set up worker getdata thread.
  share::ThreadFunctor functor = std::bind(&CAENV1740Readout::GetData,this);
  auto worker_functor = share::WorkerThreadFunctorUPtr(new share::WorkerThreadFunctor(functor,"GetDataWorkerThread"));
  auto GetData_worker = share::WorkerThread::createWorkerThread(worker_functor);
  GetData_thread_.swap(GetData_worker);
  TLOG_ARB(TCONFIG,TRACE_NAME) << "GetData worker thread setup." << TLOG_ENDL;

  TLOG(TCONFIG) << "Configuration complete with OK=" << fOK << TLOG_ENDL;


  //epoch time
  fTimeEpoch = boost::posix_time::ptime(boost::gregorian::date(1970,1,1));
}

void sbndaq::CAENV1740Readout::configureInterrupts() 
{
  CAEN_DGTZ_EnaDis_t  state ,stateOut ;
  uint8_t             interruptLevel ,interruptLevelOut ;
  uint32_t            statusId ,statusIdOut ;
  uint16_t            eventNumber   ,eventNumberOut ;
  CAEN_DGTZ_IRQMode_t mode ,modeOut ;
  CAEN_DGTZ_ErrorCode retcode;

  interruptLevel  = 1; // Fixed for CONET
  statusId        = 1;
  eventNumber     = 1;
  mode            = CAEN_DGTZ_IRQ_MODE_RORA;

  if(fInterruptEnable>0) // Enable interrupts
  {
    state           = CAEN_DGTZ_ENABLE;
  }
  else // Disable interrupts
  {
    state           = CAEN_DGTZ_DISABLE;
  }

  TLOG(TLVL_INFO)  << "Configuring Interrupts state=" << uint32_t{ stateOut} << 
    ", interruptLevel=" << uint32_t{interruptLevelOut} 
    << ", statusId=" << uint32_t{statusIdOut} << ", eventNumber=" <<
    uint32_t{eventNumberOut}<<", mode="<< int32_t{modeOut};

  retcode = CAEN_DGTZ_SetInterruptConfig(fHandle,
					 state,
					 interruptLevel,
					 statusId,
					 eventNumber,
					 mode);
  CAENDecoder::checkError(retcode,"SetInterruptConfig",fBoardID);

  retcode = CAEN_DGTZ_GetInterruptConfig (fHandle, 
					  &stateOut, 
					  &interruptLevelOut, 
					  &statusIdOut, 
					  &eventNumberOut, 
					  &modeOut);
  CAENDecoder::checkError(retcode,"GetInterruptConfig",fBoardID);

  if (state != stateOut)
  {
    TLOG_WARNING("CAENV1740Readout") << "Interrupt State was not setup properly, state write/read="
                                     << int32_t{ state} <<"/"<< int32_t{ stateOut};
  }
  if (eventNumber != eventNumberOut)
  {
    TLOG_WARNING("CAENV1740Readout")
      << "Interrupt EventNumber was not setup properly, eventNumber write/read="
      << uint32_t {eventNumber} <<"/"<< uint32_t {eventNumberOut};
  }
  if (statusId != statusIdOut) // jcrespo: from CAENDigitizer manual: In the case of the optical link, the status_id is meaningless.
  {
    TLOG_WARNING("CAENV1740Readout")
      << "Interrupt StatusID was not setup properly, statusID write/read="
      << uint32_t {statusId} <<"/"<< uint32_t {statusIdOut};
  }
  // Mode and InterruptLevel are only defined on VME, not CONET
  // if (interruptLevel != interruptLevelOut)
  // {
  //   TLOG_WARNING("CAENV1740Readout") << "Interrupt State was not setup properly, interruptLevel write/read="
  //                                    << uint32_t{interruptLevel}<< "/" << uint32_t{interruptLevelOut};
  // }
  // if (mode != modeOut)
  // {
  //   TLOG_WARNING("CAENV1740Readout")
  //     << "Interrupt State was not setup properly, mode write/read="
  //     << int32_t { mode }<< "/"<<int32_t { modeOut };
  // }


  uint32_t bitmask = (uint32_t)(0x1FF);
  uint32_t data = (uint32_t)(0x9); //RORA,irq link enabled,vme baseaddress relocation disabled,VME Bus error enabled,level 1
  uint32_t addr = READOUT_CONTROL;
  uint32_t value = 0;
  retcode = CAEN_DGTZ_ReadRegister(fHandle,addr,&value);
  TLOG(TCONFIG) << "CAEN_DGTZ_ReadRegister prior to overwrite of addr=" << std::hex << addr << ", returned value=" << std::bitset<32>(value) ; 

  TLOG_ARB(TCONFIG,TRACE_NAME) << "Setting I/O control register 0x811C " << TLOG_ENDL;
  retcode = sbndaq::CAENV1740Readout::WriteRegisterBitmask(fHandle,addr,data,bitmask);
  sbndaq::CAENDecoder::checkError(retcode,"SetIOControl",fBoardID);
  retcode = CAEN_DGTZ_ReadRegister(fHandle,addr,&value);

  TLOG(TCONFIG) << "CAEN_DGTZ_ReadRegister addr=" << std::hex << addr << 
    " and bitmask=" << std::bitset<32>(bitmask)
                << ", returned value=" << std::bitset<32>(value); 

}

void sbndaq::CAENV1740Readout::loadConfiguration(fhicl::ParameterSet const& ps)
{
  // initialize the fhicl parameters (see CAENV1740Readout.hh)
  // the obj ps has a member method that gets the private members
  // fVerbosity, etc.. are priv memb in CAENV1740Readout.hh
  //
  // wes, 16Jan2018: disabling default parameters
  ///
  fFragmentID = ps.get<uint32_t>("fragment_id");
  TLOG(TINFO)<< "fFragmentID=" << fFragmentID;

  fVerbosity = ps.get<int>("Verbosity");
  TLOG(TINFO) << "Verbosity=" << fVerbosity;

  fBoardChainNumber = ps.get<int>("BoardChainNumber"); //0
  TLOG(TINFO)<<"BoardChainNumber=" << fBoardChainNumber;

  fInterruptEnable = ps.get<uint8_t>("InterruptEnable",0); 
  TLOG(TINFO) << "InterruptEnable=" << fInterruptEnable;

  fIRQTimeoutMS = ps.get<uint32_t>("IRQTimeoutMS",500);
  TLOG(TINFO) << "IRQTimeoutMS=" << fIRQTimeoutMS;

  fSWTrigger = ps.get<bool>("SWTrigger"); //false
  TLOG(TINFO)<<"SWTrigger=" << fSWTrigger ;

  fModeLVDS = ps.get<uint32_t>("ModeLVDS"); // LVDS output mode
  TLOG(TINFO)<<"ModeLVDS=" << fModeLVDS;

  fTrigInLevel  = ps.get<uint32_t>("TrigInLevel",0); // TRG_IN on level (1) or edge (0)
  TLOG(TINFO)<<"TrigInLevel=" << fTrigInLevel;

  // jcrespo: not planned for V1740

  // fSelfTriggerMode = ps.get<uint32_t>("SelfTriggerMode"); 
  // TLOG(TINFO)<<"SelfTriggerMode=" << fSelfTriggerMode;

  // fSelfTriggerMask = ps.get<uint32_t>("SelfTriggerMask"); 
  // TLOG(TINFO)<<"SelfTriggerMask=" << std::hex << fSelfTriggerMask << std::dec;

  fGetNextSleep = ps.get<uint32_t>("GetNextSleep"); //1000000
  TLOG(TINFO) << "GetNextSleep=" << fGetNextSleep;

  fCircularBufferSize = ps.get<uint32_t>("CircularBufferSize"); //1000000
  TLOG(TINFO) << "CircularBufferSize=" << fCircularBufferSize;

  // jcrespo: calibration not available for V1740

  // force ADC calibration on config
  // fCalibrateOnConfig = ps.get<bool>("CalibrateOnConfig");
  // TLOG(TINFO) <<"CalibrateOnConfig=" << fCalibrateOnConfig;

  // disable temperature calibration 
  // fLockTempCalibration = ps.get<bool>("LockTempCalibration");
  // TLOG(TINFO) <<"LockTempCalibration=" << fLockTempCalibration;

  // read/write ADC calibration parameters
  // fWriteCalibration = ps.get<bool>("AdcCalibration");
  // TLOG(TINFO) <<"WriteCalibration=" << fWriteCalibration;

  fDumpBinary = ps.get<bool>("DumpBinary", false);
  TLOG(TINFO) <<"fDumpBinary" << fDumpBinary;
  fDumpBinaryDir = ps.get<std::string>("DumpBinaryDir", ".");
  TLOG(TINFO) <<"fDumpBinaryDir=" << fDumpBinaryDir;

  fGetNextFragmentBunchSize  = ps.get<uint32_t>("GetNextFragmentBunchSize");
  TLOG(TINFO) <<"fGetNextFragmentBunchSize=" << fGetNextFragmentBunchSize;

  fMaxEventsPerTransfer = ps.get<uint32_t>("maxEventsPerTransfer",1);
  TLOG(TINFO) <<"fMaxEventsPerTransfer=" << fMaxEventsPerTransfer;

  //Animesh & Aiwu add - for LVDS logic settings
  fLVDSLogicValueG1 = ps.get<uint32_t>("LVDSLogicValueG1"); // LVDS logic value for G1
  TLOG(TINFO)<<"LVDSLogicValueG1=" << fLVDSLogicValueG1;
  fLVDSLogicValueG2 = ps.get<uint32_t>("LVDSLogicValueG2"); // LVDS logic value for G2
  TLOG(TINFO)<<"LVDSLogicValueG2=" << fLVDSLogicValueG2;
  fLVDSLogicValueG3 = ps.get<uint32_t>("LVDSLogicValueG3"); // LVDS logic value for G3
  TLOG(TINFO)<<"LVDSLogicValueG3=" << fLVDSLogicValueG3;
  fLVDSLogicValueG4 = ps.get<uint32_t>("LVDSLogicValueG4"); // LVDS logic value for G4
  TLOG(TINFO)<<"LVDSLogicValueG4=" << fLVDSLogicValueG4;
  fLVDSLogicValueG5 = ps.get<uint32_t>("LVDSLogicValueG5"); // LVDS logic value for G5
  TLOG(TINFO)<<"LVDSLogicValueG5=" << fLVDSLogicValueG5;
  fLVDSLogicValueG6 = ps.get<uint32_t>("LVDSLogicValueG6"); // LVDS logic value for G6
  TLOG(TINFO)<<"LVDSLogicValueG6=" << fLVDSLogicValueG6;
  fLVDSLogicValueG7 = ps.get<uint32_t>("LVDSLogicValueG7"); // LVDS logic value for G7
  TLOG(TINFO)<<"LVDSLogicValueG7=" << fLVDSLogicValueG7;
  fLVDSLogicValueG8 = ps.get<uint32_t>("LVDSLogicValueG8"); // LVDS logic value for G8
  TLOG(TINFO)<<"LVDSLogicValueG8=" << fLVDSLogicValueG8;
  //Animesh & Aiwu add end

  //Animesh & Aiwu add - for LVDS output width
  fLVDSOutWidthC1 = ps.get<uint32_t>("LVDSOutWidthC1"); // LVDS output width Ch1
  TLOG(TINFO)<<"LVDSOutWidthC1=" << fLVDSOutWidthC1;
  fLVDSOutWidthC2 = ps.get<uint32_t>("LVDSOutWidthC2"); // LVDS output width Ch2
  TLOG(TINFO)<<"LVDSOutWidthC2=" << fLVDSOutWidthC2;
  fLVDSOutWidthC3 = ps.get<uint32_t>("LVDSOutWidthC3"); // LVDS output width Ch3
  TLOG(TINFO)<<"LVDSOutWidthC3=" << fLVDSOutWidthC3;
  fLVDSOutWidthC4 = ps.get<uint32_t>("LVDSOutWidthC4"); // LVDS output width Ch4
  TLOG(TINFO)<<"LVDSOutWidthC4=" << fLVDSOutWidthC4;
  fLVDSOutWidthC5 = ps.get<uint32_t>("LVDSOutWidthC5"); // LVDS output width Ch5
  TLOG(TINFO)<<"LVDSOutWidthC5=" << fLVDSOutWidthC5;
  fLVDSOutWidthC6 = ps.get<uint32_t>("LVDSOutWidthC6"); // LVDS output width Ch6
  TLOG(TINFO)<<"LVDSOutWidthC6=" << fLVDSOutWidthC6;
  fLVDSOutWidthC7 = ps.get<uint32_t>("LVDSOutWidthC7"); // LVDS output width Ch7
  TLOG(TINFO)<<"LVDSOutWidthC7=" << fLVDSOutWidthC7;
  fLVDSOutWidthC8 = ps.get<uint32_t>("LVDSOutWidthC8"); // LVDS output width Ch8
  TLOG(TINFO)<<"LVDSOutWidthC8=" << fLVDSOutWidthC8;
  fLVDSOutWidthC9 = ps.get<uint32_t>("LVDSOutWidthC9"); // LVDS output width Ch9
  TLOG(TINFO)<<"LVDSOutWidthC9=" << fLVDSOutWidthC9;
  fLVDSOutWidthC10 = ps.get<uint32_t>("LVDSOutWidthC10"); // LVDS output width Ch10
  TLOG(TINFO)<<"LVDSOutWidthC10=" << fLVDSOutWidthC10;
  fLVDSOutWidthC11 = ps.get<uint32_t>("LVDSOutWidthC11"); // LVDS output width Ch11
  TLOG(TINFO)<<"LVDSOutWidthC11=" << fLVDSOutWidthC11;
  fLVDSOutWidthC12 = ps.get<uint32_t>("LVDSOutWidthC12"); // LVDS output width Ch12
  TLOG(TINFO)<<"LVDSOutWidthC12=" << fLVDSOutWidthC12;
  fLVDSOutWidthC13 = ps.get<uint32_t>("LVDSOutWidthC13"); // LVDS output width Ch13
  TLOG(TINFO)<<"LVDSOutWidthC13=" << fLVDSOutWidthC13;
  fLVDSOutWidthC14 = ps.get<uint32_t>("LVDSOutWidthC14"); // LVDS output width Ch14
  TLOG(TINFO)<<"LVDSOutWidthC14=" << fLVDSOutWidthC14;
  fLVDSOutWidthC15 = ps.get<uint32_t>("LVDSOutWidthC15"); // LVDS output width Ch15
  TLOG(TINFO)<<"LVDSOutWidthC15=" << fLVDSOutWidthC15;
  fLVDSOutWidthC16 = ps.get<uint32_t>("LVDSOutWidthC16"); // LVDS output width Ch16
  TLOG(TINFO)<<"LVDSOutWidthC16=" << fLVDSOutWidthC16;
  //Animesh & Aiwu add end

  // jcrespo: not planned for V1740
  //Animesh & Aiwu add - self trigger polarity
  // fSelfTrigBit = ps.get<uint32_t>("SelfTrigBit"); // LVDS output width Ch16
  // TLOG(TINFO)<<"SelfTrigBit=" << fSelfTrigBit;

  fUseTimeTagForTimeStamp = ps.get<bool>("UseTimeTagForTimeStamp",true);
  TLOG(TINFO) <<"fUseTimeTagForTimeStamp=" << fUseTimeTagForTimeStamp;
  
  fUseTimeTagShiftForTimeStamp = ps.get<bool>("UseTimeTagShiftForTimeStamp",false);
  TLOG(TINFO) <<"fUseTimeTagShiftForTimeStamp=" << fUseTimeTagShiftForTimeStamp;

  fTimeOffsetNanoSec = ps.get<uint32_t>("TimeOffsetNanoSec",0); //0ms by default
  TLOG(TINFO) <<"fTimeOffsetNanoSec=" << fTimeOffsetNanoSec;

  fOutputClk = ps.get<bool>("OutputClk", 0); // To output Motherboard CLK to TRG-OUT, default 0
  TLOG(TINFO)<<"OutputClk=" << fOutputClk;

  fOutputClkPhase = ps.get<bool>("OutputClkPhase", 0); // To output Motherboard CLK PHASE to TRG-OUT, default 0
  TLOG(TINFO)<<"OutputClkPhase=" << fOutputClkPhase;
}

void sbndaq::CAENV1740Readout::Configure()
{
  TLOG_ARB(TCONFIG,TRACE_NAME) << "Configure()" << TLOG_ENDL;

  CAEN_DGTZ_ErrorCode retcode;
  uint32_t readback;

  //Make sure DAQ is off first
  TLOG_ARB(TCONFIG,TRACE_NAME) << "Set Acquisition Mode to SW" << TLOG_ENDL;
  retcode = CAEN_DGTZ_SetAcquisitionMode(fHandle,CAEN_DGTZ_SW_CONTROLLED);
  sbndaq::CAENDecoder::checkError(retcode,"SetAcquisitionMode",fBoardID);
  retcode = CAEN_DGTZ_GetAcquisitionMode(fHandle,(CAEN_DGTZ_AcqMode_t *)&readback);
  CheckReadback("SetAcquisitionMode", fBoardID,(uint32_t)CAEN_DGTZ_SW_CONTROLLED ,readback);

  TLOG_ARB(TCONFIG,TRACE_NAME) << "Stop Acquisition" << TLOG_ENDL;
  retcode = CAEN_DGTZ_SWStopAcquisition(fHandle);
  sbndaq::CAENDecoder::checkError(retcode,"SWStopAcquisition",fBoardID);

  //get info, make sure board is in good communicative state
  retcode = CAEN_DGTZ_GetInfo(fHandle,&fBoardInfo);
  fOK = (retcode==CAEN_DGTZ_Success);

  retcode = CAEN_DGTZ_Reset(fHandle);
  sleep(2);

  ConfigureReadout();
  ConfigureRecordFormat();
  ConfigureTrigger();

  if(fAcqMode==CAEN_DGTZ_SW_CONTROLLED){
    TLOG_ARB(TCONFIG,TRACE_NAME) << "Stop Acquisition" << TLOG_ENDL;
    retcode = CAEN_DGTZ_SWStopAcquisition(fHandle);
    sbndaq::CAENDecoder::checkError(retcode,"SWStopAcquisition",fBoardID);
  }

  ConfigureAcquisition();
  configureInterrupts();

  // jcrespo: comment-region as it is not available for V1740

  //  if (fCalibrateOnConfig)     { RunADCCalibration();  }
  //
  // // Does lock bit clear on V1740 reset?   If not, always call this routine
  // if (fLockTempCalibration )  
  // { 
  //   for ( uint32_t ch=0; ch<CAENV1740Configuration::MAX_CHANNELS; ch++)
  //   {
  //     SetLockTempCalibration(true,ch);
  //   }
  // }

  // // Calibration added by Animesh
  // uint8_t fCalParams;
  // if (fWriteCalibration)  
  //   { 
      
  //     for ( uint32_t ch=0; ch<CAENV1740Configuration::MAX_CHANNELS; ch++)
  // 	{
  // 	  TLOG(TINFO)<<"Chnumber is " <<ch<<TLOG_ENDL;
  // 	  Read_ADC_CalParams_V1740(fHandle, ch,&fCalParams);
  // 	  //  Write_ADC_CalParams_V1740(fHandle, ch,&fCalParams);
  // 	}
      
  //   }
  
  TLOG_ARB(TCONFIG,TRACE_NAME) << "Configure() done." << TLOG_ENDL;
}

// jcrespo: comment-region as it is not available for V1740

// void sbndaq::CAENV1740Readout::RunADCCalibration()
// {
//   TLOG_ARB(TINFO,TRACE_NAME) << "Running calibration..." << TLOG_ENDL;
//   auto retcode = CAEN_DGTZ_Calibrate(fHandle);
//   sbndaq::CAENDecoder::checkError(retcode,"Calibrate",fBoardID);
// }


void CAENV1740Readout::ReadGroupBusyStatus(int handle, uint32_t g, uint32_t& status)
{

  status = 0xdeadbeef;
  uint32_t SPIBusyAddr = 0x1088 + (g<<8);

  auto ret = CAEN_DGTZ_ReadRegister(handle, SPIBusyAddr, &status);
  
  if(ret!=CAEN_DGTZ_Success)
    TLOG(TLVL_WARNING) << "Failed reading busy status for channel " << g;
     
}


// jcrespo: commented as it is not used anymore in this code
// Following SPI code is from CAEN
// CAEN_DGTZ_ErrorCode CAENV1740Readout::ReadSPIRegister(int handle, uint32_t ch, uint32_t address, uint8_t *value)
// {
//   uint32_t SPIBusy = 1;
//   CAEN_DGTZ_ErrorCode retcod = CAEN_DGTZ_Success;
//   uint32_t SPIBusyAddr        = 0x1088 + (ch<<8);
//   uint32_t addressingRegAddr  = 0x80B4;
//   uint32_t valueRegAddr       = 0x10B8 + (ch<<8);
//   uint32_t val;

//   while(SPIBusy) 
//   {
//     if((retcod = CAEN_DGTZ_ReadRegister(handle, SPIBusyAddr, &SPIBusy)) != CAEN_DGTZ_Success)
//     {
//       return CAEN_DGTZ_CommError;
//     }

//     SPIBusy = (SPIBusy>>2)&0x1;
//     if (!SPIBusy) 
//     {
//       if((retcod = CAEN_DGTZ_WriteRegister(handle, addressingRegAddr, address)) != CAEN_DGTZ_Success)
//       { return CAEN_DGTZ_CommError;}

//       if((retcod = CAEN_DGTZ_ReadRegister(handle, valueRegAddr, &val)) != CAEN_DGTZ_Success)
//       { return CAEN_DGTZ_CommError;}
//     }
//     *value = (uint8_t)val;
//     usleep(1000);
//   }
//   return CAEN_DGTZ_Success;
// }

// jcrespo: commented as it is not used anymore in this code
// CAEN_DGTZ_ErrorCode CAENV1740Readout::WriteSPIRegister(int handle, uint32_t ch, uint32_t address, uint8_t value)
// {
//   uint32_t SPIBusy = 1;
//   CAEN_DGTZ_ErrorCode retcod = CAEN_DGTZ_Success;
    
//   uint32_t SPIBusyAddr        = 0x1088 + (ch<<8);
//   uint32_t addressingRegAddr  = 0x80B4;
//   uint32_t valueRegAddr       = 0x10B8 + (ch<<8);

//   while (SPIBusy) 
//   {
//     if((retcod = CAEN_DGTZ_ReadRegister(handle, SPIBusyAddr, &SPIBusy)) != CAEN_DGTZ_Success)
//     {
//       return CAEN_DGTZ_CommError;
//     }

//     SPIBusy = (SPIBusy>>2)&0x1;
//     if (!SPIBusy) 
//     {
//       if((retcod = CAEN_DGTZ_WriteRegister(handle, addressingRegAddr, address)) != CAEN_DGTZ_Success)
//       {  return CAEN_DGTZ_CommError;}
//       if((retcod = CAEN_DGTZ_WriteRegister(handle, valueRegAddr, (uint32_t)value)) != CAEN_DGTZ_Success)
//       { return CAEN_DGTZ_CommError;}
//     }
//     usleep(1000);
//   }
//   return CAEN_DGTZ_Success;
// }

// jcrespo: comment-region as it is not available for V1740

// void sbndaq::CAENV1740Readout::SetLockTempCalibration(bool onOff, uint32_t ch)
// {
//   CAEN_DGTZ_ErrorCode retcod;
//   uint8_t lock, ctrl;
//   TLOG_ARB(TINFO,TRACE_NAME) << "Locking Temperature Calibration Adjustments, channel " << ch << TLOG_ENDL;

//   // Following code comes from CAEN
//   // enter engineering functions
//   retcod = WriteSPIRegister(fHandle, ch, (uint32_t)0x7A, (uint8_t)0x59);
//   sbndaq::CAENDecoder::checkError(retcod,"LockTempCalibration",fBoardID);

//   retcod = WriteSPIRegister(fHandle, ch, (uint32_t)0x7A, (uint8_t)0x1A);
//   sbndaq::CAENDecoder::checkError(retcod,"LockTempCalibration",fBoardID);

//   retcod = WriteSPIRegister(fHandle, ch, (uint32_t)0x7A, (uint8_t)0x11);
//   sbndaq::CAENDecoder::checkError(retcod,"LockTempCalibration",fBoardID);

//   retcod = WriteSPIRegister(fHandle, ch, (uint32_t)0x7A, (uint8_t)0xAC);
//   sbndaq::CAENDecoder::checkError(retcod,"LockTempCalibration",fBoardID);
  
//   // read lock value
//   retcod = ReadSPIRegister (fHandle, ch, (uint32_t)0xA7, &lock);
//   sbndaq::CAENDecoder::checkError(retcod,"LockTempCalibration",fBoardID);

//   // write lock value
//   retcod = WriteSPIRegister(fHandle, ch, (uint32_t)0xA5, lock);
//   sbndaq::CAENDecoder::checkError(retcod,"LockTempCalibration",fBoardID);

//   // enable lock
//   retcod = ReadSPIRegister (fHandle, ch, (uint32_t)0xA4, &ctrl);
//   sbndaq::CAENDecoder::checkError(retcod,"LockTempCalibration",fBoardID);

//   if (onOff) { ctrl |= 0x4;}  // set bit 2
//   else       { ctrl &= ~0x4;}
//   retcod = WriteSPIRegister(fHandle, ch, (uint32_t)0xA4, ctrl);
//   sbndaq::CAENDecoder::checkError(retcod,"LockTempCalibration",fBoardID);

//   retcod = ReadSPIRegister (fHandle, ch, (uint32_t)0xA4, &ctrl);
//   sbndaq::CAENDecoder::checkError(retcod,"LockTempCalibration",fBoardID);
// }

// Animesh added here for Calibration

// ---------------------------------------------------------------------------------------------------------
// Description: Read ADC calibration from ADC chip (via SPI)
// Inputs: handle = board handle
// ch = channel
// Return: 0=OK, negative number = error code
// ---------------------------------------------------------------------------------------------------------

// void sbndaq::CAENV1740Readout::Read_ADC_CalParams_V1740(int handle, int ch, uint8_t *CalParams)
// {
//  //int retcod = 0;
//   CAEN_DGTZ_ErrorCode retcod;
//  // read offset
//  retcod = ReadSPIRegister(handle, ch, 0x20, &CalParams[0]);
//  TLOG(TINFO)<<"Read_ADC-CalParams_ch"<<ch<< ": Params[0]=" << (int)CalParams[0];
//  sbndaq::CAENDecoder::checkError(retcod,"Read_ADC_CalParams_0x20",handle);
//  retcod = ReadSPIRegister(handle, ch, 0x21, &CalParams[1]);
//  TLOG(TINFO)<<"Read_ADC-CalParams_ch"<<ch<< ": Params[1]=" << (int)CalParams[1];
//  sbndaq::CAENDecoder::checkError(retcod,"Read_ADC_CalParams_0x21",handle);
//  retcod = ReadSPIRegister(handle, ch, 0x26, &CalParams[2]);
//   TLOG(TINFO)<<"Read_ADC-CalParams_ch"<<ch<< ": Params[2]=" << (int)CalParams[2];
//  sbndaq::CAENDecoder::checkError(retcod,"Read_ADC_CalParams_0x26",handle);
//  retcod = ReadSPIRegister(handle, ch, 0x27, &CalParams[3]);
//  TLOG(TINFO)<<"Read_ADC-CalParams_ch"<<ch<< ": Params[3]=" << (int)CalParams[3];
//  sbndaq::CAENDecoder::checkError(retcod,"Read_ADC_CalParams_0x27",handle);
 
// // read gain
//  retcod = ReadSPIRegister(handle, ch, 0x22, &CalParams[4]);
//  TLOG(TINFO)<<"Read_ADC-CalParams_"<< ": Params[4]=" << (int)CalParams[4];
//  sbndaq::CAENDecoder::checkError(retcod,"Read_ADC_CalParams_0x22",handle);
//  retcod = ReadSPIRegister(handle, ch, 0x23, &CalParams[5]);
//  TLOG(TINFO)<<"Read_ADC-CalParams_"<< ": Params[5]=" << (int)CalParams[5];
//  sbndaq::CAENDecoder::checkError(retcod,"Read_ADC_CalParams_0x23",handle);
//  retcod = ReadSPIRegister(handle, ch, 0x24, &CalParams[6]);
//  TLOG(TINFO)<<"Read_ADC-CalParams_"<< ": Params[6]=" << (int)CalParams[6];
//  sbndaq::CAENDecoder::checkError(retcod,"Read_ADC_CalParams_0x24",handle);
//  retcod = ReadSPIRegister(handle, ch, 0x28, &CalParams[7]);
//  TLOG(TINFO)<<"Read_ADC-CalParams_"<< ": Params[7]=" << (int)CalParams[7];
//  sbndaq::CAENDecoder::checkError(retcod,"Read_ADC_CalParams_0x28",handle);
//  retcod = ReadSPIRegister(handle, ch, 0x29, &CalParams[8]);
//  TLOG(TINFO)<<"Read_ADC-CalParams_"<< ": Params[8]=" << (int)CalParams[8];
//  sbndaq::CAENDecoder::checkError(retcod,"Read_ADC_CalParams_0x29",handle);
//  retcod = ReadSPIRegister(handle, ch, 0x2A, &CalParams[9]);
//  TLOG(TINFO)<<"Read_ADC-CalParams_"<< ": Params[9]=" << (int)CalParams[9];
//  sbndaq::CAENDecoder::checkError(retcod,"Read_ADC_CalParams_0x2A",handle);
//  // read skew
//  retcod = ReadSPIRegister(handle, ch, 0x70, &CalParams[10]);
//  TLOG(TINFO)<<"Read_ADC-CalParams_"<< ": Params[10]=" << (int)CalParams[10];
//  sbndaq::CAENDecoder::checkError(retcod,"Read_ADC_CalParams_0x70",handle);
//  //return CAEN_DGTZ_Success;
// }

// // ---------------------------------------------------------------------------------------------------------
// // Description: Write ADC calibration to ADC chip (via SPI)
// // Inputs: handle = board handle
// // ch = channel
// // Return: 0=OK, negative number = error code
// // ---------------------------------------------------------------------------------------------------------

// void sbndaq::CAENV1740Readout::Write_ADC_CalParams_V1740(int handle, int ch, uint8_t *CalParams)
// {
//   //int retcod = 0;
//   CAEN_DGTZ_ErrorCode retcod;
//   // Keep parameters frozen
//   retcod = WriteSPIRegister(handle, ch, 0xFE, 0x00);
//   TLOG(TINFO)<<"Write_ADC-CalParams_ch"<<ch<< ": Params[0]=" << (int)CalParams[0];
//   sbndaq::CAENDecoder::checkError(retcod,"Write_ADC_CalParams_0x20",handle);

//   // write offset
//   retcod = WriteSPIRegister(handle, ch, 0x20, CalParams[0]);
//   retcod = WriteSPIRegister(handle, ch, 0x21, CalParams[1]);
//   retcod = WriteSPIRegister(handle, ch, 0x26, CalParams[2]);
//   retcod = WriteSPIRegister(handle, ch, 0x27, CalParams[3]);
  
//   // write gain
//   retcod = WriteSPIRegister(handle, ch, 0x22, CalParams[4]);
//   retcod = WriteSPIRegister(handle, ch, 0x23, CalParams[5]);
//   retcod = WriteSPIRegister(handle, ch, 0x24, CalParams[6]);
//   retcod = WriteSPIRegister(handle, ch, 0x28, CalParams[7]);
//   retcod = WriteSPIRegister(handle, ch, 0x29, CalParams[8]);
//   retcod = WriteSPIRegister(handle, ch, 0x2A, CalParams[9]);
  
//   // write skew
//   retcod = WriteSPIRegister(handle, ch, 0x70, CalParams[10]);
  
//   // Update parameters
//   retcod = WriteSPIRegister(handle, ch, 0xFE, 0x01);
//   retcod = WriteSPIRegister(handle, ch, 0xFE, 0x00);
  
// }

// // Animesh add ends

// jcrespo: comment-region to skip self-trigger configuration for V1740 as it is not planned

// // GVS: new ConfigureSelfTriggerMode() function
//  void sbndaq::CAENV1740Readout::ConfigureSelfTriggerMode()
// {
//   CAEN_DGTZ_ErrorCode retcod = CAEN_DGTZ_Success;

//   retcod = CAEN_DGTZ_SetChannelSelfTrigger(fHandle,
// 					   (CAEN_DGTZ_TriggerMode_t)fSelfTriggerMode,
// 					   fSelfTriggerMask);
//   sbndaq::CAENDecoder::checkError(retcod,"SetChannelSelfTriggerMode",fBoardID);
  

//   // GVS: the following configuration parameters are for SBND. fModeLVDS must be
//   if(fModeLVDS==0){ 
  
//      uint32_t data, data2, bitpair, readBack, aux, aux2;
  
//      // GVS: verify the SelfTrigger values set in each channel.
//      for(uint32_t chn=0; chn<fNChannels; ++chn){
//          retcod = CAEN_DGTZ_GetChannelSelfTrigger(fHandle, chn, (CAEN_DGTZ_TriggerMode_t *)&readBack);
//          sbndaq::CAENDecoder::checkError(retcod,"GetChannelSelfTriggerMode",fBoardID);
//          CheckReadback("ChannelSelfTriggerMode", fBoardID, fSelfTriggerMode, readBack, chn);

    
//         // GVS: inserted triggerLogic for each PAIR of channels.
//         if(chn%2==0){
      
//            retcod = CAEN_DGTZ_ReadRegister(fHandle,SLF_TRG_LG_CH+(chn<<8),&aux);
//            TLOG_ARB(TCONFIG,TRACE_NAME) << "Self-trigger logic to channel " << chn << " old value " << std::hex << aux << std::dec;

//            TLOG_ARB(TCONFIG,TRACE_NAME) << "Set channels " << chn << "/" << chn+1 << " self trigger logic to " << fCAEN.triggerLogic
// 				       /* << " self trigger pulse type to " << fCAEN.ovthValue*/ << TLOG_ENDL;
//            retcod = CAEN_DGTZ_WriteRegister(fHandle,SLF_TRG_LG_CH+(chn<<8),
// 					(fCAEN.triggerLogic & 0x3)/* + ((fCAEN.ovthValue & 0x1) <<2)*/);

//            sbndaq::CAENDecoder::checkError(retcod,"SelfTriggerPulseType",fBoardID);
//            retcod = CAEN_DGTZ_ReadRegister(fHandle,SLF_TRG_LG_CH+(chn<<8),&aux2);
      
//            TLOG_ARB(TCONFIG,TRACE_NAME) << "Self-trigger logic to channel " << chn << " new value " << std::hex << aux2 << std::dec;
//            CheckReadback("SelfTriggerPulseType", fBoardID, aux2, (fCAEN.triggerLogic & 0x3) /*+ ((fCAEN.ovthValue & 0x1) <<2)*/,chn);
//         }
//      }
  
   
//      // GVS: read TRG_OUT register to monitor enabled/disabled pair of channels.
//      retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_TRG_OUT_CONTROL, &bitpair);
//      TLOG_ARB(TCONFIG,TRACE_NAME) << "Front Panel TRG-OUT address 0x8110, value: 0x" << std::hex << bitpair << std::dec;
				    


//      /* Set Majority Mode and Majority Coincidence Window */
//      retcod = CAEN_DGTZ_ReadRegister(fHandle, GLB_TRG_MASK, &data);
//      TLOG_ARB(TCONFIG,TRACE_NAME) << "Global Trigger Mask address 0x810C, old value: 0x" << std::hex << data << std::dec;
  
//      TLOG_ARB(TCONFIG,TRACE_NAME)  << " Set Majority Level to " << fCAEN.majorityLevel << TLOG_ENDL;
//      TLOG_ARB(TCONFIG,TRACE_NAME)  << " Set Maj Coincidence Window to " << fCAEN.majorityCoincidenceWindow << TLOG_ENDL;
				   
//      data |= ((fCAEN.majorityLevel & 0x7)<<24) + ((fCAEN.majorityCoincidenceWindow & 0xF) <<20);
				   
//      retcod = CAEN_DGTZ_WriteRegister(fHandle,GLB_TRG_MASK, data);

//      sbndaq::CAENDecoder::checkError(retcod,"SetMajCoincWindow",fBoardID);
//      retcod = CAEN_DGTZ_ReadRegister(fHandle,GLB_TRG_MASK,&data2);
      
//      TLOG_ARB(TCONFIG,TRACE_NAME) << "Global Trigger Mask address 0x810C, new value: 0x" << std::hex << data2 << std::dec;
//      CheckReadback("SetMajCoincWindow", fBoardID, data, data2);
//   }
// }

// jcrespo: end of comment-region to skip self-trigger configuration for V1740 as it is not planned

void sbndaq::CAENV1740Readout::ConfigureClkToTrgOut()
{
  /* Check to output ONLY CLK OR CLK PHASE */
  if ( fOutputClk && fOutputClkPhase ){
    TLOG(TLVL_ERROR) << "Error configuring output clock: Cannot output clock and its phase at the same time." << std::endl;
    abort();
  } 

  CAEN_DGTZ_ErrorCode retcod = CAEN_DGTZ_Success;
  uint32_t data;

  /* Check the output of the 0x811C */
  retcod = CAEN_DGTZ_ReadRegister(fHandle,FP_IO_CONTROL, &data);
  sbndaq::CAENDecoder::checkError(retcod,"ClkToTrgOutCheckError",fBoardID);

  uint32_t value16 = 0x1; 
  uint32_t value18 = 0x0;
  if (fOutputClk) value18 = 0x1;
  if (fOutputClkPhase) value18 = 0x2;
  data |= ((value16 & 0x3)<<16) + ((value18 &0x3)<<18);

  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_IO_CONTROL, data);
  sbndaq::CAENDecoder::checkError(retcod,"ClkToTrgOutCheckError",fBoardID);

  TLOG_ARB(TCONFIG,TRACE_NAME) << "Front Panel IO Control address 0x811C, new value: 0x" << std::hex << data << std::dec;
  TLOG(TINFO) << "Front Panel IO Control address 0x811C, new value: 0x" << std::hex << data << std::dec;
}

// jcrespo: not changing anything below since it should be irrelevant for V1740 planned use
void sbndaq::CAENV1740Readout::ConfigureLVDS()
{
  CAEN_DGTZ_ErrorCode retcod = CAEN_DGTZ_Success;
  uint32_t data,readBack,ioMode;

  // Always set output to "New LVDS features"
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_IO_CONTROL, &ioMode);
  sbndaq::CAENDecoder::checkError(retcod,"ReadFPOutputConfig",fBoardID);

  // Construct mode mask
  data = fModeLVDS | (fModeLVDS << 4) | (fModeLVDS << 8) | (fModeLVDS << 12);
  TLOG(TINFO) << "ModelLVDS: 0x" << 
      std::hex << data << std::dec;
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_CONTROL, data);
  sbndaq::CAENDecoder::checkError(retcod,"WriteLVDSOutputConfig",fBoardID);

  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_CONTROL, &readBack);
  sbndaq::CAENDecoder::checkError(retcod,"ReadLVDSOutputConfig",fBoardID);

  CheckReadback("LVDSOutputConfig", fBoardID, data, readBack);

  // If TRIGGER mode, send them out TRG-OUT NIM
  if ( fModeLVDS == LVDS_TRIGGER )
  {
    retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_TRG_OUT_CONTROL, &data);
    sbndaq::CAENDecoder::checkError(retcod,"ReadTRGOutputConfig",fBoardID);

    //    data |= ( ENABLE_LVDS_TRIGGER | ENABLE_TRG_OUT );
    // wes and bill commenting out 10/14/2020 to get DaisyChain to work
    //data |= ( ENABLE_TRG_OUT );
    //data &= ~ TRIGGER_LOGIC ; // Choose OR Logic

    retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_TRG_OUT_CONTROL, data);
    sbndaq::CAENDecoder::checkError(retcod,"WriteTRGOutputConfig",fBoardID);

    retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_TRG_OUT_CONTROL, &readBack);
    sbndaq::CAENDecoder::checkError(retcod,"ReadTRGOutputConfig",fBoardID);
    
    TLOG(TINFO) << "TrgOutputConfig: 0x" << 
      std::hex << data << std::dec;
    CheckReadback("TRGOutputConfig", fBoardID, data, readBack);

    // Put LVDS into OUTPUT mode and send to TRG-OUT
    ioMode |= (LVDS_IO | ENABLE_NEW_LVDS);
    ioMode &= ~DISABLE_TRG_OUT_LEMO ;
  }
  else
  {
    // Put LVDS into INPUT mode
    ioMode &= ~(LVDS_IO | DISABLE_TRG_OUT_LEMO);
  }

  if ( fTrigInLevel )
  {
    ioMode |= TRG_IN_LEVEL;
  }
  else
  {
    ioMode &= ~(TRG_IN_LEVEL);
  }

  TLOG(TINFO) << "FPOutputConfig: 0x" << 
    std::hex << ioMode << std::dec;
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_IO_CONTROL, ioMode);
  sbndaq::CAENDecoder::checkError(retcod,"WriteFPOutputConfig",fBoardID);

  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_IO_CONTROL, &readBack);
  sbndaq::CAENDecoder::checkError(retcod,"ReadFPOutputConfig",fBoardID);

  CheckReadback("FPOutputConfig", fBoardID, ioMode, readBack);

  //Animesh & Aiwu add - to set/read registers for LVDS logic values setting
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G1, fLVDSLogicValueG1);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G2, fLVDSLogicValueG2);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G3, fLVDSLogicValueG3);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G4, fLVDSLogicValueG4);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G5, fLVDSLogicValueG5);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G6, fLVDSLogicValueG6);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G7, fLVDSLogicValueG7);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G8, fLVDSLogicValueG8);

  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G1, &readBack);
  TLOG(TINFO) << "LVDS Logic for G1: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G2, &readBack);
  TLOG(TINFO) << "LVDS  Logic for G2: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G3, &readBack);
  TLOG(TINFO) << "LVDS  Logic for G3: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G4, &readBack);
  TLOG(TINFO) << "LVDS  Logic for G4: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G5, &readBack);
  TLOG(TINFO) << "LVDS  Logic for G5: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G6, &readBack);
  TLOG(TINFO) << "LVDS  Logic for G6: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G7, &readBack);
  TLOG(TINFO) << "LVDS  Logic for G7: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G8, &readBack);
  TLOG(TINFO) << "LVDS  Logic for G8: 0x" << std::hex << readBack << std::dec;
  //Animesh & Aiwu add ends

  //Animesh & Aiwu add - to set/read registers for LVDS output width values setting
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch1, fLVDSOutWidthC1);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch2, fLVDSOutWidthC2);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch3, fLVDSOutWidthC3);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch4, fLVDSOutWidthC4);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch5, fLVDSOutWidthC5);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch6, fLVDSOutWidthC6);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch7, fLVDSOutWidthC7);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch8, fLVDSOutWidthC8);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch9, fLVDSOutWidthC9);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch10, fLVDSOutWidthC10);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch11, fLVDSOutWidthC11);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch12, fLVDSOutWidthC12);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch13, fLVDSOutWidthC13);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch14, fLVDSOutWidthC14);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch15, fLVDSOutWidthC15);
  retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_OutWidth_Ch16, fLVDSOutWidthC16);
  
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch1, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch1: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch2, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch2: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch3, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch3: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch4, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch4: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch5, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch5: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch6, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch6: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch7, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch7: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch8, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch8: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch9, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch9: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch10, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch10: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch11, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch11: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch12, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch12: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch13, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch13: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch14, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch14: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch15, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch15: 0x" << std::hex << readBack << std::dec;
  retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_OutWidth_Ch16, &readBack);
  TLOG(TINFO) << "LVDS  Logic output width for Ch16: 0x" << std::hex << readBack << std::dec;
  //Animesh & Aiwu add ends

  // jcrespo: not planned for V1740
  //Animesh & Aiwu add - test self trigger polarity
  // retcod = CAEN_DGTZ_WriteRegister(fHandle, CONFIG_READ_ADDR, fSelfTrigBit);
  // retcod = CAEN_DGTZ_ReadRegister(fHandle, CONFIG_READ_ADDR, &readBack);
  // TLOG(TINFO) << "Address 0x8000, values inside: 0x" << std::hex << readBack << std::dec;
  //Animesh & Aiwu end
}

void sbndaq::CAENV1740Readout::ConfigureRecordFormat()
{
  TLOG_ARB(TCONFIG,TRACE_NAME) << "ConfigureRecordFormat()" << TLOG_ENDL;
  CAEN_DGTZ_ErrorCode retcode;
  uint32_t readback;

  //channel masks for readout(?)
  TLOG_ARB(TCONFIG,TRACE_NAME) << "SetGroupEnableMask " << fCAEN.groupEnableMask << TLOG_ENDL;
  retcode = CAEN_DGTZ_SetGroupEnableMask(fHandle,fCAEN.groupEnableMask);
  sbndaq::CAENDecoder::checkError(retcode,"SetGroupEnableMask",fBoardID);
  retcode = CAEN_DGTZ_GetGroupEnableMask(fHandle,&readback);
  sbndaq::CAENDecoder::checkError(retcode,"GetGroupEnableMask",fBoardID);
  CheckReadback("GROUP_ENABLE_MASK", fBoardID, fCAEN.groupEnableMask, readback);

  //record length
  TLOG_ARB(TCONFIG,TRACE_NAME) << "SetRecordLength " << fCAEN.recordLength << TLOG_ENDL;
  retcode = CAEN_DGTZ_SetRecordLength(fHandle,fCAEN.recordLength);
  sbndaq::CAENDecoder::checkError(retcode,"SetRecordLength",fBoardID);
  retcode = CAEN_DGTZ_GetRecordLength(fHandle,&readback);
  sbndaq::CAENDecoder::checkError(retcode,"GetRecordLength",fBoardID);
  CheckReadback("RECORD_LENGTH", fBoardID, fCAEN.recordLength, readback);

  // jcrespo: setting the decimation factor explicitly is required before setting the post trigger
  // (following /home/nfs/sbnd/wavedump-3.10.6/src/WaveDump.c)
  uint16_t decimationFactor = 1;
  //uint16_t readDecimationFactor;
  TLOG_ARB(TCONFIG,TRACE_NAME) << "SetDecimation " << decimationFactor << TLOG_ENDL;
  retcode = CAEN_DGTZ_SetDecimationFactor(fHandle, decimationFactor);
  sbndaq::CAENDecoder::checkError(retcode,"SetDecimationFactor",fBoardID);
  // Function is not used in /home/nfs/sbnd/wavedump-3.10.6/src/WaveDump.c
  //retcode = CAEN_DGTZ_GetDecimationFactor(fHandle,&readDecimationFactor); // function requires a *uint16_t
  //sbndaq::CAENDecoder::checkError(retcode,"GetDecimationFactor",fBoardID);
  //CheckReadback("DECIMATION_FACTOR", fBoardID, decimationFactor, readDecimationFactor);

  //post trigger size
  TLOG_ARB(TCONFIG,TRACE_NAME) << "SetPostTriggerSize " << (unsigned int)(fCAEN.postPercent) << TLOG_ENDL;
  retcode = CAEN_DGTZ_SetPostTriggerSize(fHandle,(unsigned int)(fCAEN.postPercent));
  sbndaq::CAENDecoder::checkError(retcode,"SetPostTriggerSize",fBoardID);
  retcode = CAEN_DGTZ_GetPostTriggerSize(fHandle,&readback);
  sbndaq::CAENDecoder::checkError(retcode,"GetPostTriggerSize",fBoardID);
  CheckReadback("POST_TRIGGER_SIZE", fBoardID, fCAEN.postPercent, readback);

  // Test: scan over percentages
  /*
  for(int percent = 100; percent >= 0; percent--){
    std::cout << "SetPostTriggerSize " << (unsigned int)(percent) << "\n";
    retcode = CAEN_DGTZ_SetPostTriggerSize(fHandle,percent);
    std::cout << "   retcode   " << retcode << "\n";
    sbndaq::CAENDecoder::checkError(retcode,"SetPostTriggerSize",fBoardID);

    retcode = CAEN_DGTZ_GetPostTriggerSize(fHandle,&readback);
    std::cout << "GotPostTriggerSize " << readback << "\n";
    std::cout << "   retcode   " << retcode << std::endl;
    sbndaq::CAENDecoder::checkError(retcode,"GetPostTriggerSize",fBoardID);

    CheckReadback("POST_TRIGGER_SIZE", fBoardID, percent, readback);
  }
  */
  TLOG_ARB(TCONFIG,TRACE_NAME) << "ConfigureRecordFormat() done." << TLOG_ENDL;
}

sbndaq::CAENV1740Readout::~CAENV1740Readout()
{
  TLOG_ARB(TCONFIG,TRACE_NAME) << "~CAENV1740Readout()" << TLOG_ENDL;

  if(fBuffer != NULL){
    fBuffer.reset();
  }

  TLOG_ARB(TCONFIG,TRACE_NAME) << "~CAENV1740Readout() done." << TLOG_ENDL;
}

//Taken from wavedump
//  handle : Digitizer handle
//  address: register address
//  data   : value to write to register
//  bitmask: bitmask to override only the bits that need to change while leaving the rest unchanged
CAEN_DGTZ_ErrorCode sbndaq::CAENV1740Readout::WriteRegisterBitmask(int32_t handle, uint32_t address, 
								   uint32_t data, uint32_t bitmask) 
{
  //int32_t ret = CAEN_DGTZ_Success;
  CAEN_DGTZ_ErrorCode  ret = CAEN_DGTZ_Success;
  uint32_t d32 = 0xFFFFFFFF;
  uint32_t d32Out;

  ret = CAEN_DGTZ_ReadRegister(handle, address, &d32);
  if(ret != CAEN_DGTZ_Success){
    TLOG(TLVL_ERROR) << "Failed reading a register; address=0x" << std::hex << address;
    abort();
  }

  data &= bitmask;
  d32 &= ~bitmask;
  d32 |= data;
  ret = CAEN_DGTZ_WriteRegister(handle, address, d32);

  if(ret != CAEN_DGTZ_Success) {
    TLOG(TLVL_ERROR) << "Failed writing a register; address=0x" << std::hex << address;
    abort();
  }

  ret = CAEN_DGTZ_ReadRegister(handle, address, &d32Out);
  if(ret != CAEN_DGTZ_Success){
    TLOG(TLVL_ERROR) << "Failed reading a register; address=0x" << std::hex << address
                     << ", value=" << std::bitset<32>(d32) << ", bitmask=" << std::bitset<32>(bitmask);
    abort();
  }

  if( d32 != d32Out ) {
    TLOG(TLVL_ERROR) << "Read and write values disagree; address=0x" << std::hex << address
                     <<", read value=" << std::bitset<32>(d32Out) << ", write value="<< std::bitset<32>(d32)
                     << ", bitmask="<< std::bitset<32>(bitmask);
    abort();
  }

  return ret;
}

void sbndaq::CAENV1740Readout::ConfigureDataBuffer()
{
  TLOG_ARB(TSTART,TRACE_NAME) << "ConfigureDataBuffer()" << TLOG_ENDL;

  CAEN_DGTZ_ErrorCode retcode;

  retcode = CAEN_DGTZ_SetMaxNumEventsBLT(fHandle,fMaxEventsPerTransfer);
  sbndaq::CAENDecoder::checkError(retcode,"SetMaxNumEventsBLT",fBoardID);

  //we do this shenanigans so we can get the BufferSize. We then allocate our own...
  char* myBuffer=NULL;
  retcode = CAEN_DGTZ_MallocReadoutBuffer(fHandle,&myBuffer,&fBufferSize);
  sbndaq::CAENDecoder::checkError(retcode,"MallocReadoutBuffer",fBoardID);
  
  fBuffer.reset(new uint16_t[fBufferSize/sizeof(uint16_t)]);

  TLOG_ARB(TSTART,TRACE_NAME) << "Created Buffer of size " << fBufferSize << std::endl << TLOG_ENDL;  

  retcode = CAEN_DGTZ_FreeReadoutBuffer(&myBuffer);
  sbndaq::CAENDecoder::checkError(retcode,"FreeReadoutBuffer",fBoardID);

  TLOG_ARB(TSTART,TRACE_NAME) << "Configuring Circular Buffer of size " << fCircularBufferSize << TLOG_ENDL;
  fPoolBuffer.allocate(fBufferSize,fCircularBufferSize,true);
  fPoolBuffer.debugInfo();

  std::lock_guard<std::mutex> lock(fTimestampMapMutex);
  fTimestampMap.clear();
}

void sbndaq::CAENV1740Readout::ConfigureTrigger()
{
  TLOG_ARB(TCONFIG,TRACE_NAME) << "ConfigureTrigger()" << TLOG_ENDL;

  CAEN_DGTZ_ErrorCode retcode;
  uint32_t readback;
  uint32_t addr;

  //set the trigger configurations
  TLOG_ARB(TCONFIG,TRACE_NAME) << "SetSWTriggerMode" << fCAEN.swTrgMode << TLOG_ENDL;
  retcode = CAEN_DGTZ_SetSWTriggerMode(fHandle,(CAEN_DGTZ_TriggerMode_t)(fCAEN.swTrgMode));
  sbndaq::CAENDecoder::checkError(retcode,"SetSWTriggerMode",fBoardID);
  retcode = CAEN_DGTZ_GetSWTriggerMode(fHandle,(CAEN_DGTZ_TriggerMode_t *)&readback);
  CheckReadback("SetSWTriggerMode", fBoardID,fCAEN.swTrgMode,readback);

  TLOG_ARB(TCONFIG,TRACE_NAME) << "SetExtTriggerMode" << fCAEN.extTrgMode << TLOG_ENDL;
  retcode = CAEN_DGTZ_SetExtTriggerInputMode(fHandle,(CAEN_DGTZ_TriggerMode_t)(fCAEN.extTrgMode));
  sbndaq::CAENDecoder::checkError(retcode,"SetExtTriggerInputMode",fBoardID);
  retcode = CAEN_DGTZ_GetExtTriggerInputMode(fHandle,(CAEN_DGTZ_TriggerMode_t *)&readback);
  CheckReadback("SetExtTriggerInputMode", fBoardID,fCAEN.extTrgMode,readback);

  // jcrespo: comment-region as it is not planned for V1740

  // for(uint32_t ch=0; ch<fNChannels; ++ch)
  // {

  //   TLOG_ARB(TCONFIG,TRACE_NAME) << "Set channel " << ch
  // 				 << " trigger threshold to " << fCAEN.triggerThresholds[ch] << TLOG_ENDL;
  //   retcode = CAEN_DGTZ_SetChannelTriggerThreshold(fHandle,ch,fCAEN.triggerThresholds[ch]); //0x8000
  //   sbndaq::CAENDecoder::checkError(retcode,"SetChannelTriggerThreshold",fBoardID);
  //   retcode = CAEN_DGTZ_GetChannelTriggerThreshold(fHandle,ch,&readback);
  //   CheckReadback("SetChannelTriggerThreshold",fBoardID,fCAEN.triggerThresholds[ch],readback);

  //   //GVS: the following configuration parameters are for SBND. fModeLVDS must be 0
  //     if(fModeLVDS==0){
  //     TLOG_ARB(TCONFIG,TRACE_NAME) << "Set Trigger Polarity " << fCAEN.triggerPolarity << " to channel: " << ch << TLOG_ENDL;
  //     retcode = CAEN_DGTZ_SetTriggerPolarity(fHandle, ch,(CAEN_DGTZ_TriggerPolarity_t)(fCAEN.triggerPolarity));
  //     sbndaq::CAENDecoder::checkError(retcode,"SetTriggerPolarity",fBoardID);
  //     retcode = CAEN_DGTZ_GetTriggerPolarity(fHandle, ch,(CAEN_DGTZ_TriggerPolarity_t *)&readback);
  //     CheckReadback("SetTriggerPolarity", fBoardID,fCAEN.triggerPolarity,readback, ch);

    
  //     //GVS: pulse width must be set per channel, not per pair of channel. This contradicts what manual says!
  //     TLOG_ARB(TCONFIG,TRACE_NAME) << "Set channels " << ch << " trigger pulse width to " << fCAEN.triggerPulseWidth << TLOG_ENDL;
  //     retcode = CAEN_DGTZ_WriteRegister(fHandle,TRG_OUT_WIDTH_CH+(ch<<8),fCAEN.triggerPulseWidth);
  //     sbndaq::CAENDecoder::checkError(retcode,"SetChannelTriggerPulseWidth",fBoardID);
  //     retcode = CAEN_DGTZ_ReadRegister(fHandle,TRG_OUT_WIDTH_CH+(ch<<8),&readback);
  //     CheckReadback("SetChannelTriggerPulseWidth",fBoardID,fCAEN.triggerPulseWidth,readback, ch);

  //     //pulse width only set in pairs, but doesn't hurt to do it for all channels I guess
  //     /* TLOG_ARB(TCONFIG,TRACE_NAME) << "Set channels " << ch << "/" << ch+1 
  // 				   << " trigger pulse width to " << (int)(fCAEN.triggerPulseWidth) << TLOG_ENDL;
  //     retcode = CAEN_DGTZ_WriteRegister(fHandle,0x1070+(ch<<8),fCAEN.triggerPulseWidth);
  //     sbndaq::CAENDecoder::checkError(retcode,"SetChannelTriggerPulseWidth",fBoardID);
  //     retcode = CAEN_DGTZ_ReadRegister(fHandle,0x1070+(ch<<8),&readback);
  //     CheckReadback("SetChannelTriggerPulseWidth",fBoardID,fCAEN.triggerPulseWidth,readback); */
  //   }
  // }

  // for ICARUS
  // if(fModeLVDS!=0){ ConfigureLVDS();  }
	
  // for clock synchronization studies
  if( fOutputClk || fOutputClkPhase ){ ConfigureClkToTrgOut(); } 

  //  ConfigureSelfTriggerMode(); // jcrespo: not planned for V1740

  TLOG_ARB(TCONFIG,TRACE_NAME) << "SetTriggerMode" << fCAEN.extTrgMode << TLOG_ENDL;
  retcode = CAEN_DGTZ_SetExtTriggerInputMode(fHandle,(CAEN_DGTZ_TriggerMode_t)(fCAEN.extTrgMode));
  sbndaq::CAENDecoder::checkError(retcode,"SetExtTriggerInputMode",fBoardID);
  retcode = CAEN_DGTZ_GetExtTriggerInputMode(fHandle,(CAEN_DGTZ_TriggerMode_t *)&readback);
  CheckReadback("SetExtTriggerInputMode", fBoardID,fCAEN.extTrgMode,readback);  

  TLOG_ARB(TCONFIG,TRACE_NAME) << "SetTriggerOverlap" << fCAEN.allowTriggerOverlap << TLOG_ENDL;
  if ( fCAEN.allowTriggerOverlap )
  {
    addr = CONFIG_SET_ADDR;
  }
  else
  {
    addr = CONFIG_CLEAR_ADDR;
  }
  retcode = CAEN_DGTZ_WriteRegister(fHandle, addr, TRIGGER_OVERLAP_MASK);
  sbndaq::CAENDecoder::checkError(retcode,"SetTriggerOverlap",fBoardID);

  //level=1 for TTL, =0 for NIM
  TLOG_ARB(TCONFIG,TRACE_NAME) << "SetIOLevel " << (CAEN_DGTZ_IOLevel_t)(fCAEN.ioLevel) << TLOG_ENDL;
  retcode = CAEN_DGTZ_SetIOLevel(fHandle,(CAEN_DGTZ_IOLevel_t)(fCAEN.ioLevel));
  sbndaq::CAENDecoder::checkError(retcode,"SetIOLevel",fBoardID);
  retcode = CAEN_DGTZ_GetIOLevel(fHandle,(CAEN_DGTZ_IOLevel_t *)&readback);
  CheckReadback("SetIOLevel", fBoardID,fCAEN.ioLevel,readback);

}

void sbndaq::CAENV1740Readout::ConfigureReadout()
{
  TLOG_ARB(TCONFIG,TRACE_NAME) << "ConfigureReadout()" << TLOG_ENDL;

  CAEN_DGTZ_ErrorCode retcode;
  uint32_t readback;
  uint32_t addr,mask;
  uint32_t value=0;
  
  TLOG_ARB(TCONFIG,TRACE_NAME) << "SetRunSyncMode " << (CAEN_DGTZ_RunSyncMode_t)(fCAEN.runSyncMode) << TLOG_ENDL;
  retcode = CAEN_DGTZ_SetRunSynchronizationMode(fHandle,
						(CAEN_DGTZ_RunSyncMode_t)(fCAEN.runSyncMode));
  sbndaq::CAENDecoder::checkError(retcode,"SetRunSynchronizationMode",fBoardID);
  retcode = CAEN_DGTZ_GetRunSynchronizationMode(fHandle,(CAEN_DGTZ_RunSyncMode_t*)&readback);
  CheckReadback("SetRunSynchronizationMode",fBoardID,fCAEN.runSyncMode,readback);
  
  mask = ( 1 << TEST_PATTERN_t::TEST_PATTERN_S );
  addr = (fCAEN.testPattern)
    ? CAEN_DGTZ_BROAD_CH_CONFIGBIT_SET_ADD
    : CAEN_DGTZ_BROAD_CH_CLEAR_CTRL_ADD;
  TLOG_ARB(TCONFIG,TRACE_NAME) << "SetTestPattern addr=" << addr << ", mask=" << mask << TLOG_ENDL;
  retcode = CAEN_DGTZ_WriteRegister(fHandle,addr,mask);
  sbndaq::CAENDecoder::checkError(retcode,"SetTestPattern",fBoardID);

  //Global Registers

  // jcrespo: comment-region as it is not available for V1740  
  // TLOG_ARB(TCONFIG,TRACE_NAME) << "SetDyanmicRange " << fCAEN.dynamicRange << TLOG_ENDL;
  // mask = (uint32_t)(fCAEN.dynamicRange);
  // addr = DYNAMIC_RANGE;
  // retcode = CAEN_DGTZ_WriteRegister(fHandle,addr,mask);
  // sbndaq::CAENDecoder::checkError(retcode,"SetDynamicRange",fBoardID);

  addr = ACQ_CONTROL;
  retcode = CAEN_DGTZ_WriteRegister(fHandle,addr,uint32_t{0x28});
  sbndaq::CAENDecoder::checkError(retcode,"SetTriggerMode",fBoardID);
  retcode = CAEN_DGTZ_ReadRegister(fHandle,addr,&value);
  sbndaq::CAENDecoder::checkError(retcode,"GetTriggerMode",fBoardID);

  TLOG(TCONFIG) << "CAEN_DGTZ_ReadRegister addr=" << std::hex << addr << ", returned value=" << std::bitset<32>(value) ; 

  // jcrespo: this work different from th V1730 for the V1740 due to the 8-ch groups
  // jcrespo: for individual channel configuration see note below
  /* Note from the CAENDigitizer Library manual: from AMC FPGA firmware release 0.10 on, it is possible to apply an 8-bit positive digital offset individually to
     each channel inside a group of the x740 digitizer to finely correct the baseline mismatch. This function is not supported
     bythe CAENdigitizer library, but the user can refer to the register’s documentation. */

  for(uint32_t g=0; g<fNGroups; ++g){
    TLOG_ARB(TCONFIG,TRACE_NAME) << "Set channel group " << g << " DC offset to " << fCAEN.pedestal[g] << TLOG_ENDL;
    retcode = CAEN_DGTZ_SetGroupDCOffset(fHandle,g,fCAEN.pedestal[g]);
    sbndaq::CAENDecoder::checkError(retcode,"SetGroupDCOffset",fBoardID);
    retcode = CAEN_DGTZ_GetGroupDCOffset(fHandle,g,&readback);
    CheckReadback("SetGroupDCOffset",fBoardID,fCAEN.pedestal[g],readback,g);
  }

  TLOG_ARB(TCONFIG,TRACE_NAME) << "ConfigureReadout() done." << TLOG_ENDL;
}

void sbndaq::CAENV1740Readout::ConfigureAcquisition()
{
  TLOG_ARB(TCONFIG,TRACE_NAME) << "ConfigureAcquisition()" << TLOG_ENDL;

  CAEN_DGTZ_ErrorCode retcode;
  uint32_t readback;

  TLOG_ARB(TCONFIG,TRACE_NAME) << "SetAcqMode " << (CAEN_DGTZ_AcqMode_t)(fCAEN.acqMode) << TLOG_ENDL;
  retcode = CAEN_DGTZ_SetAcquisitionMode(fHandle,(CAEN_DGTZ_AcqMode_t)(fCAEN.acqMode));
  sbndaq::CAENDecoder::checkError(retcode,"SetAcquisitionMode",fBoardID);
  retcode = CAEN_DGTZ_GetAcquisitionMode(fHandle,(CAEN_DGTZ_AcqMode_t*)&readback);
  CheckReadback("SetAcquisitionMode",fBoardID,fCAEN.acqMode,readback);

  // jcrespo: comment blocks below as it is not planned for V1740
  // TLOG_ARB(TCONFIG,TRACE_NAME) << "SetAnalogMonOutputMode " << (CAEN_DGTZ_AnalogMonitorOutputMode_t)(fCAEN.analogMode) << TLOG_ENDL;
  // retcode = CAEN_DGTZ_SetAnalogMonOutput(fHandle,(CAEN_DGTZ_AnalogMonitorOutputMode_t)(fCAEN.analogMode));
  // sbndaq::CAENDecoder::checkError(retcode,"SetAnalogMonOutputMode",fBoardID);
  
  // // GetAnalogMonOutput function does not work for V1730s, use register access instead
  // retcode = CAEN_DGTZ_ReadRegister(fHandle,CAEN_DGTZ_MON_MODE_ADD,&readback);
  // CheckReadback("SetAnalogMonOutputMode",fBoardID,fCAEN.analogMode,readback);

  TLOG_ARB(TCONFIG,TRACE_NAME) << "ConfigureAcquisition() done." << TLOG_ENDL;
}

bool sbndaq::CAENV1740Readout::WaitForTrigger()
{

  TLOG_ARB(TSTATUS,TRACE_NAME) << "WaitForTrigger()" << TLOG_ENDL;

  CAEN_DGTZ_ErrorCode retcode;

  uint32_t acqStatus;
  retcode = CAEN_DGTZ_ReadRegister(fHandle,
				   CAEN_DGTZ_ACQ_STATUS_ADD,
				   &acqStatus);
  if(retcode!=CAEN_DGTZ_Success){
    TLOG_WARNING("CAENV1740Readout")
      << "Trying ReadRegister ACQUISITION_STATUS again." << TLOG_ENDL;
    retcode = CAEN_DGTZ_ReadRegister(fHandle,
				     CAEN_DGTZ_ACQ_STATUS_ADD,
				     &acqStatus);
  }
  sbndaq::CAENDecoder::checkError(retcode,"ReadRegister ACQ_STATUS",fBoardID);

  TLOG_ARB(TSTATUS,TRACE_NAME) << " Acq status = " << acqStatus << TLOG_ENDL;
  return (acqStatus & ACQ_STATUS_MASK_t::EVENT_READY);

}

void sbndaq::CAENV1740Readout::start()
{

  if(fVerbosity>0)
    TLOG_INFO("CAENV1740Readout") << "start()" << TLOG_ENDL;
  TLOG_ARB(TSTART,TRACE_NAME) << "start()" << TLOG_ENDL;
  
  ConfigureDataBuffer();
  total_data_size = 0;
  last_sent_seqid = 0;
  
  if((CAEN_DGTZ_AcqMode_t)(fCAEN.acqMode)==CAEN_DGTZ_AcqMode_t::CAEN_DGTZ_SW_CONTROLLED)
    {
      CAEN_DGTZ_ErrorCode retcode;
      TLOG_ARB(TSTART,TRACE_NAME) << "SWStartAcquisition" << TLOG_ENDL;
      retcode = CAEN_DGTZ_SWStartAcquisition(fHandle);
      sbndaq::CAENDecoder::checkError(retcode,"SWStartAcquisition",fBoardID);
    }
  
  fEvCounter=0;
  fOverflowCounter=0;
  CAEN_DGTZ_ErrorCode retcod;

  // jcrespo: calibration not available for V1740

  // // Animesh add ADC registers here

  // if (fWriteCalibration)  
  //   { 
  //     for ( uint32_t ch=0; ch<CAENV1740Configuration::MAX_CHANNELS; ++ch)
  //       {
  //         retcod = WriteSPIRegister(fHandle, ch, 0xFE, 0x00);
  //         // TLOG(TINFO)<<"Write_ADC-CalParams_ch"<<ch<< ": Params[0]=" << CalParams[0]; 
  //         // sbndaq::CAENDecoder::checkError(retcod,"Write_ADC_CalParams_0x20",handle);
      
  //         // write offset
  //         retcod = WriteSPIRegister(fHandle, ch, 0x20, 114);
  //         retcod = WriteSPIRegister(fHandle, ch, 0x21, 107);
  //         retcod = WriteSPIRegister(fHandle, ch, 0x26, 122);
  //         retcod = WriteSPIRegister(fHandle, ch, 0x27, 76);
      
  //         // write gain
  //         retcod = WriteSPIRegister(fHandle, ch, 0x22, 14);
  //         retcod = WriteSPIRegister(fHandle, ch, 0x23, 128);
  //         retcod = WriteSPIRegister(fHandle, ch, 0x24, 127);
  //         retcod = WriteSPIRegister(fHandle, ch, 0x28, 14);
  //         retcod = WriteSPIRegister(fHandle, ch, 0x29, 135);
  //         retcod = WriteSPIRegister(fHandle, ch, 0x2A, 125);
      
  //         // write skew
  //         retcod = WriteSPIRegister(fHandle, ch, 0x70, 129);
      
  //         // Update parameters
  //         retcod = WriteSPIRegister(fHandle, ch, 0xFE, 0x01);
  //         retcod = WriteSPIRegister(fHandle, ch, 0xFE, 0x00);
  //       }
  // }
  // //  Animesh ends
  
  uint32_t readBack;
  
  // Animesh Check trigger threshold here
  // jcrespo: not planned for V1740   
  // for(uint32_t ch=0; ch<fNChannels; ++ch)
  //   {
  //     retcod = CAEN_DGTZ_GetChannelTriggerThreshold(fHandle,ch,&readBack);
  //     TLOG(TINFO) << "Trigger threshold before run start for ch " << ch << " is " << readBack << TLOG_ENDL;    
  //   }
    
  // Animesh end 
  
  //  uint32_t readBack;
  //Animesh & Aiwu add - to set/read registers for LVDS logic values setting

  if(fModeLVDS!=0){
    retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G1, fLVDSLogicValueG1);
    retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G2, fLVDSLogicValueG2);
    retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G3, fLVDSLogicValueG3);
    retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G4, fLVDSLogicValueG4);
    retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G5, fLVDSLogicValueG5);
    retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G6, fLVDSLogicValueG6);
    retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G7, fLVDSLogicValueG7);
    retcod = CAEN_DGTZ_WriteRegister(fHandle, FP_LVDS_Logic_G8, fLVDSLogicValueG8);
    
    
    retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G1, &readBack);
    TLOG(TINFO) << "After Start Register for G1: 0x" <<retcod<< std::hex << readBack << std::dec;
    retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G2, &readBack);
    TLOG(TINFO) << " After Start Register for G2: 0x" << std::hex << readBack << std::dec;
    retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G3, &readBack);
    TLOG(TINFO) << " After Start Register for G3: 0x" << std::hex << readBack << std::dec;
    retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G4, &readBack);
    TLOG(TINFO) << " After Start Register for G4: 0x" << std::hex << readBack << std::dec;
    retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G5, &readBack);
    TLOG(TINFO) << " After Start Register for G5: 0x" << std::hex << readBack << std::dec;
    retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G6, &readBack);
    TLOG(TINFO) << " After Start Register for G6: 0x" << std::hex << readBack << std::dec;
    retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G7, &readBack);
    TLOG(TINFO) << "After Start Register for G7: 0x" << std::hex << readBack << std::dec;
    retcod = CAEN_DGTZ_ReadRegister(fHandle, FP_LVDS_Logic_G8, &readBack);
    TLOG(TINFO) << "After Start Register for G8: 0x" << std::hex << readBack << std::dec;
  }


  fTimePollBegin = boost::posix_time::microsec_clock::universal_time();
  GetData_thread_->start();
  
  // Check the baseline values
  for(uint32_t i_g=0; i_g<fNGroups; ++i_g)
  {
    retcod = CAEN_DGTZ_GetGroupDCOffset(fHandle,i_g,&readBack);
    TLOG(TINFO)<<"DC offset or baseline before run start for Group " << i_g << " is " << readBack << TLOG_ENDL;    
  }   

  if( fDumpBinary ){
    // Get timestamp for binary file name
    time_t t = time(0);
    struct tm ltm = *localtime( &t );
    sprintf(binFileName, "%s/rawbin_V1740_ID%05i_run%06i_%4i.%02i.%02i-%02i.%02i.%02i.dat",
	    fDumpBinaryDir.c_str(), fBoardID, artdaq::CommandableFragmentGenerator::run_number(),
	    ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday, ltm.tm_hour, ltm.tm_min, ltm.tm_sec);
    
    TLOG(TINFO) << "Opening raw binary file " << binFileName;
    binFile.open( binFileName, std::ofstream::out | std::ofstream::binary ); // temp
  }

  TLOG_ARB(TSTART,TRACE_NAME) << "start() done." << TLOG_ENDL;
}

void sbndaq::CAENV1740Readout::stop()
{
  if(fVerbosity>0)
    TLOG_INFO("CAENV1740Readout") << "stop()" << TLOG_ENDL;
  TLOG_ARB(TSTOP,TRACE_NAME) << "stop()" << TLOG_ENDL;

  GetData_thread_->stop();

  CAEN_DGTZ_ErrorCode retcode;
  TLOG_ARB(TSTOP,TRACE_NAME) << "SWStopAcquisition" << TLOG_ENDL;
  retcode = CAEN_DGTZ_SWStopAcquisition(fHandle);
  sbndaq::CAENDecoder::checkError(retcode,"SWStopAcquisition",fBoardID);

  if(fBuffer != NULL){
    fBuffer.reset();
  }
  
  if( fDumpBinary ){
    TLOG(TINFO) << "Closing raw binary file " << binFileName;
    binFile.close(); // temp
  }

  TLOG_ARB(TSTOP,TRACE_NAME) << "stop() done." << TLOG_ENDL;
}

// This function is called internally by the art framework and should not be called in the boardreader itself
// The two relevant fcl parameters are: "poll_hardware_status" (true/false) and "hardware_poll_interval_us" (period in us)
bool sbndaq::CAENV1740Readout::checkHWStatus_(){

  for(size_t g=0; g<CAENV1740Configuration::MAX_GROUPS; ++g)
  {
    // jcrespo: comment-region as it is not available for V1740
    /*
    std::ostringstream tempStream;
    tempStream << "CAENV1730.Card" << fBoardID
		  << ".Channel" << ch << ".Temp"; 
    */
    std::ostringstream statStream;
    statStream << "CAENV1740.Card" << fBoardID
		  << ".Channel" << g << ".Status"; 
    std::ostringstream memfullStream;
    memfullStream << "CAENV1740.Card" << fBoardID
		  << ".Channel" << g << ".MemoryFull"; 
   
    CAEN_DGTZ_ErrorCode retcod;

    // jcrespo: comment-region as it is not available for V1740

    // if ( fCAEN.temperatureCheckMask & ( 1 << ch ) ) // Channels temperature readout enabled?
    // {
    //   retcod = CAEN_DGTZ_ReadTemperature(fHandle, ch, &(ch_temps[ch]));
    //   TLOG_ARB(TTEMP,TRACE_NAME) << tempStream.str()
    // 				 << ": " << ch_temps[ch] << "  C"
    // 				 << TLOG_ENDL;
      
    //   metricMan->sendMetric(tempStream.str(), int(ch_temps[ch]), "C", 11,
    // 			    artdaq::MetricMode::Average);

    //   //Need 3 requirements to shut down for high temperature: 
    //   // 1. Can successfully read temperature: return code = 0 since S/N 164 can fail to read temperature during acquisition
    //   // 2. Temperature < non_physical temperature, 200C since S/N 164 can produce non-physical temperature
    //   // 3. Temperature > Requirement , 70C for V1730 and 85C for V1730S
    //   if ( retcod == CAEN_DGTZ_Success && ch_temps[ch] < V1730_UNPHYSICAL_TEMPERATURE )
    //   {  
    // 	if ( ch_temps[ch] > fCAEN.maxTemp ) 
    // 	{
    // 	   // V1730(S) shuts down at 70(85) celsius, give a warning ahead of that
    // 	   TLOG(TLVL_ERROR) << "SHUTTING DOWN CAENV1730 BoardID " << fBoardID << " : "
    // 			     << " Channel " << ch
    // 			     << " temperature " << ch_temps[ch]
    // 			     << " > " << fCAEN.maxTemp << " degrees Celsius."
    // 			     << " ReadTemperature Return Code = " << retcod
    // 			     << TLOG_ENDL;
    // 	}
    //   }
    //   else
    //   {
    // 	    // Ignore unphysical temperatures/bad return codes from S/N 164.  CAEN advises not to read temperatures 
    // 	    //   while the readout is running, but we cannot do that.  Only one sensors on one 
    // 	    //   V1730 has ever malfunctioned.
    // 	    // S/N 164 sometimes returns a non-physical temperature, ignore it and move on
    // 	    TLOG(TLVL_WARNING) << "CAENV1730 BoardID " << fBoardID << " : "
    // 			       << " Channel " << ch
    // 			       << " unphysical temperature " << ch_temps[ch] << " degrees Celsius."
    // 			       << " ReadTemperature Return Code = " << retcod
    // 			       << TLOG_ENDL;
    //    }
    // }

    ReadGroupBusyStatus(fHandle,g,g_status[g]);
    TLOG_ARB(TTEMP,TRACE_NAME) << statStream.str()
			       << std::hex
                               << ": 0x" << g_status[g]
			       << std::dec << TLOG_ENDL;


    if(g_status[g]==0xdeadbeef){
      TLOG(TLVL_WARNING) << "Failed reading busy status for group " << g;
    }
    else{
      metricMan->sendMetric(statStream.str(), int(g_status[g]), "", 11,
			    artdaq::MetricMode::LastPoint);

      metricMan->sendMetric(memfullStream.str(), int((g_status[g] & 0x1)), "", 11,
			    artdaq::MetricMode::LastPoint);

      /*
      metricMan->sendMetric("MemoryEmpty", int((ch_status[ch] & 0x2)>>1), "", 1,
			    artdaq::MetricMode::LastPoint,tempStream.str());

      metricMan->sendMetric("DACBusy", int((ch_status[ch] & 0x4)>>2), "", 1,
			    artdaq::MetricMode::LastPoint,tempStream.str());

      metricMan->sendMetric("ADCPowerDown", int((ch_status[ch] & 0x100)>>8), "", 1,
			    artdaq::MetricMode::LastPoint,tempStream.str());
      */
    }


  }

  return true;
}

bool sbndaq::CAENV1740Readout::GetData() {
  TLOG(TGETDATA)<< "Begin of GetData()";

  CAEN_DGTZ_ErrorCode retcod;

  if(fSWTrigger) {
    usleep(fGetNextSleep);
    TLOG(TGETDATA) << "Sending SW trigger..." << TLOG_ENDL;
    retcod = CAEN_DGTZ_SendSWtrigger(fHandle);
    TLOG(TGETDATA) << "CAEN_DGTZ_SendSWtrigger returned " << int{retcod};
  }

  // read the data from the buffer of the card
  // this_data_size is the size of the acq window

  return readWindowDataBlocks();

}// CAENV1740Readout::GetData()

bool sbndaq::CAENV1740Readout::readWindowDataBlocks() {

  if(fail_GetNext) {
    TLOG(TLVL_ERROR) << "(FragID=" << fFragmentID << ")"
		     << "Not calling CAEN_DGTZ_ReadData due a previous critical error...";
    ::usleep(50000);
    return false;
  }

  TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"    
		 << "Begin.";

  //wait for one event, then interrupt
  CAEN_DGTZ_ErrorCode retcode = CAEN_DGTZ_IRQWait(fHandle, fIRQTimeoutMS);

  //if we have a timeout condition, return
  if (retcode == CAEN_DGTZ_Timeout) {

    //end of this poll
    fTimePollEnd = boost::posix_time::microsec_clock::universal_time();
    
    TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"
		   << "Exiting after a timeout. Poll time was " 
		   << (fTimePollEnd - fTimePollBegin).total_milliseconds() << " ms.";
    
    //update the polling time for the next poll
    fTimePollBegin = fTimePollEnd;

    //go again!
    return true;
  }

  TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"
		 << "No timeout. TimePollBegin=" 
		 << fTimePollBegin << " TimePollEnd=" << fTimePollEnd;

  uint32_t read_data_size = 1;
  size_t n_reads=0;

  //gianluca won't let me do a do while
  //we want to do ReadData until there is no more data to read

  TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"
		 << "Start while loop read. " << read_data_size; 
  while(read_data_size!=0){

    TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"
		   << "Last read data size was " << read_data_size; 

    //reset read_data_size to 0, just in case
    read_data_size = 0;

    //get a block of data from the PoolBuffer. Hopefully doesn't take very long.
    auto block =  fPoolBuffer.takeFreeBlock();
    if(!block) {
      TLOG(TLVL_ERROR) << "(FragID=" << fFragmentID << ")"
		       << "PoolBuffer is empty; last received trigger eventCounter=" <<last_rcvd_rwcounter;
      TLOG(TLVL_ERROR) << "(FragID=" << fFragmentID << ")"
		       << "PoolBuffer status: freeBlockCount=" << fPoolBuffer.freeBlockCount()
                       << "(FragID=" << fFragmentID << ")"
		       <<", activeBlockCount=" << fPoolBuffer.activeBlockCount();
      TLOG(TLVL_ERROR) << "(FragID=" << fFragmentID << ")"
		       << "Critical error; aborting boardreader process....";				

      fail_GetNext = true;

      std::this_thread::yield();
      return false;
    }
    TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"
		   << "Got a free DataBlock from PoolBuffer";


    //call ReadData
    TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"
		   << "Calling ReadData(fHandle="<<fHandle<< ",bufp=" << (void*)block->begin
                   << ",&block.size="<<(void*)&(block->size) << ")";

    retcode = CAEN_DGTZ_ReadData(fHandle,CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT,
                                (char*)block->begin,&read_data_size);

    if(read_data_size==0) { 
      fPoolBuffer.returnFreeBlock(block);
      break;
    }

    ++n_reads;
    
    block->verify_redzone();
    block->data_size= read_data_size;

    TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"
		   << "This read data size was " << read_data_size; 

    //check to make sure no errors on readout.
    if (retcode !=CAEN_DGTZ_Success) {
      TLOG(TLVL_ERROR) << "CAEN_DGTZ_ReadData returned non zero return code; return code=" << int{retcode};
      fPoolBuffer.returnFreeBlock(block);
      std::this_thread::yield();
      return false;
    }

    fTimePollEnd = boost::posix_time::microsec_clock::universal_time();
    TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"
		   << "CAEN_DGTZ_ReadData complete with returned data size " << block->data_size
                   << " retcod=" << int{retcode};


    const auto header = reinterpret_cast<CAENV1740EventHeader const *>(block->begin);
    
    TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")" 
    // jcrespo: promote fragment message to TINFO
    //TLOG(TINFO) << "(FragID=" << fFragmentID << ")"
		   << ": XA_EVENT_COUNTER=" << header->eventCounter
		   << ", XA_EVENT_SIZE=" << header->eventSize
		   << ", XA_TIME_TAG=" << header->triggerTimeTag;

    // jcrespo: Dump binary here following NevisTPC2Stream
    if( fDumpBinary ) binFile.write( (char*)block->begin, block->data_size );

    const size_t header_event_size = sizeof(uint32_t)* header->eventSize; 
    if(block->data_size != header_event_size ) {
      TLOG(TLVL_ERROR) << "(FragID=" << fFragmentID << ")"
		       <<"Wrong event size; returned="
                       << block->data_size << ", header=" << header_event_size
		       << ". XA_EVENT_COUNTER=" << header->eventCounter
		       << ", XA_EVENT_SIZE=" << header->eventSize
		       << ", XA_TIME_TAG=" << header->triggerTimeTag 
		       << ". DROPPING THIS FRAGMENT.";
      fPoolBuffer.returnFreeBlock(block);
      break;
    }

    //do all the timestamp assignment
    //first reference against epoch
    fTimeDiffPollBegin = fTimePollBegin - fTimeEpoch;
    fTimeDiffPollEnd = fTimePollEnd - fTimeEpoch;

    //then calculate the mean poll time
    fMeanPollTime = fTimeDiffPollBegin.total_nanoseconds()/2 + fTimeDiffPollEnd.total_nanoseconds()/2;
    fMeanPollTimeNS = fMeanPollTime%(1000000000);
    fTTT=0;
    fTTT_ns = -1;

    if(fUseTimeTagForTimeStamp){
      fTTT = uint32_t{header->triggerTimeTag}; // 
      fTTT_ns = fTTT*8;
      
      // Scheme borrowed from what Antoni developed for CRT.
      // See https://sbn-docdb.fnal.gov/cgi-bin/private/DisplayMeeting?sessionid=7783
      fTS = fMeanPollTime - fMeanPollTimeNS + fTTT_ns
	+ (fTTT_ns - (long)fMeanPollTimeNS < -500000000) * 1000000000
	- (fTTT_ns - (long)fMeanPollTimeNS >  500000000) * 1000000000
	- fTimeOffsetNanoSec;
    }
    else if(fUseTimeTagShiftForTimeStamp){
      fTTT = uint32_t{header->triggerTimeTag}; // 
      // TTT is 8 ticks/ns, record length is 16 ticks/ns. See CAEN V1740 manuals for details
      fTTT_ns = (fTTT*8.0) - (((double)fCAEN.recordLength * 16.0) * ((double)fCAEN.postPercent / 100.0)); //in 1 ns
      
      // Scheme borrowed from what Antoni developed for CRT.
      // See https://sbn-docdb.fnal.gov/cgi-bin/private/DisplayMeeting?sessionid=7783
      fTS = fMeanPollTime - fMeanPollTimeNS + fTTT_ns
	+ (fTTT_ns - (long)fMeanPollTimeNS < -500000000) * 1000000000
	- (fTTT_ns - (long)fMeanPollTimeNS >  500000000) * 1000000000
	- fTimeOffsetNanoSec;
    }
    else{
      fTS = fTimeDiffPollEnd.total_nanoseconds() - fTimeOffsetNanoSec;;
    }

    //put lock in local scope
    {
      std::lock_guard<std::mutex> lock(fTimestampMapMutex);
      fTimestampMap[uint32_t{header->eventCounter}] = fTS;
    }

    //print out timestamping info
    TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"
		   << "TIMESTAMP " << fFragmentID 
		   << ": Poll begin/end/mean/ns = " << fTimeDiffPollBegin.total_nanoseconds()
		   << "/" << fTimeDiffPollEnd.total_nanoseconds() 
		   << "/" << fMeanPollTime
		   << "/" << fMeanPollTimeNS;
    TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"
		   << "TIMESTAMP " << fFragmentID 
		   << ": TTT/TTT_ns/TS_ns = " << fTTT << "/" << fTTT_ns << "/" << fTS;
    TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"
		   << "TIMESTAMP " << fFragmentID 
		   << ": Timestamp for event " << header->eventCounter << " = " << fTS;


    //check trigger event counter gaps: this is a 24-bit counter in the CAEN board
    //if the run is long, it can overflow --> do not throw errors in that case
    auto readoutwindow_trigger_counter_gap= uint32_t{header->eventCounter} - last_rcvd_rwcounter;
    if( readoutwindow_trigger_counter_gap > 1u && last_rcvd_rwcounter < max_rwcounter ){
      TLOG (TLVL_DEBUG) << "(FragID=" << fFragmentID << ")"
			<< "Missing triggers; previous trigger eventCounter / gap  = " << last_rcvd_rwcounter << " / "
			<< readoutwindow_trigger_counter_gap <<", freeBlockCount=" <<fPoolBuffer.freeBlockCount() 
			<< ", activeBlockCount=" <<fPoolBuffer.activeBlockCount() << ", fullyDrainedCount=" << fPoolBuffer.fullyDrainedCount();
    }    
    last_rcvd_rwcounter = uint32_t{header->eventCounter};
    
    //return active block
    fPoolBuffer.returnActiveBlock(block);
    
    TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"
		   << "CAEN_DGTZ_ReadData returned DataBlock header.eventCounter=" 
		   << header->eventCounter << ", header.eventSize=" << header_event_size;

  }//end while read_data_size is not zero

  TLOG(TGETDATA) << "(FragID=" << fFragmentID << ")"
		 << "n_reads=" << n_reads; 
  
  //update the polling time for the next poll
  fTimePollBegin = fTimePollEnd;

  //and go again!
  return true;
}


// this is really the DAQ part where the server reads data from 
// the card and stores them
bool sbndaq::CAENV1740Readout::getNext_(artdaq::FragmentPtrs & fragments){
  if(fail_GetNext) throw std::runtime_error("Critical error; stopping boardreader process...." ) ;
  return readSingleWindowFragments(fragments);
}

bool sbndaq::CAENV1740Readout::readSingleWindowFragments(artdaq::FragmentPtrs & fragments){
  TLOG(TGETNEXT) << "Begin of readSingleWindowFragments()" ;

  static auto start= std::chrono::steady_clock::now();

  std::chrono::duration<double> delta = std::chrono::steady_clock::now()-start;

  if (delta.count() >0.005*fGetNextFragmentBunchSize) {
     metricMan->sendMetric("Laggy getNext",1,"count",11,artdaq::MetricMode::Accumulate);
     TLOG (TLVL_DEBUG) << "Time spent outside of getNext_() " << delta.count()*1000 << " ms. Last seen fragment sequenceID=" << last_sent_seqid;
   }

  if(fPoolBuffer.activeBlockCount() == 0){
    TLOG(TGETNEXT) << "PoolBuffer has no data.  Laast last seen fragment sequenceID=" << last_sent_seqid
                   << "; Sleep for " << fGetNextSleep << " us and return.";
    ::usleep(fGetNextSleep);
    start= std::chrono::steady_clock::now();
    return true;
  }

  double max_fragment_create_time = 0.0;
  double min_fragment_create_time = 10000.0;
  struct timespec now;
  clock_gettime(CLOCK_REALTIME,&now);
  // const auto metadata = CAENV1740FragmentMetadata(fNChannels,fCAEN.recordLength,now.tv_sec,now.tv_nsec,ch_temps);
  const auto metadata = CAENV1740FragmentMetadata(fNChannels,fCAEN.recordLength,now.tv_sec,now.tv_nsec);
  const auto fragment_datasize_bytes = metadata.ExpectedDataSize();
  TLOG(TMAKEFRAG)<< "Created CAENV1740FragmentMetadata with expected data size of "
                 << fragment_datasize_bytes << " bytes.";

  //auto fragment_count=fGetNextFragmentBunchSize;

  //just get anything that's there...
  while(fPoolBuffer.activeBlockCount()){

    start= std::chrono::steady_clock::now();
    auto fragment_uptr=artdaq::Fragment::FragmentBytes(fragment_datasize_bytes,fEvCounter,fFragmentID,sbndaq::detail::FragmentType::CAENV1740,metadata);


    using sbndaq::PoolBuffer;
    PoolBuffer::DataRange<decltype(artdaq::Fragment())> range{fragment_uptr->dataBegin(),fragment_uptr->dataEnd()};

    if(!fPoolBuffer.read(range)) break;

    const auto header = reinterpret_cast<CAENV1740EventHeader const *>(fragment_uptr->dataBeginBytes());
   
    // get eventCounter, which is the key to get the correct timestamp from the map
    // for longer runs it can overflow (24 bit) --> play safe and don't use it as sequence id
    // build sequence id keeping track of overflows (avoids repeated seqIDs in the same run)
    const auto readoutwindow_event_counter = uint32_t {header->eventCounter};
    TLOG(TMAKEFRAG)<< "CAENV1740Fragment event counter " << readoutwindow_event_counter;

    uint64_t readoutwindow_sequence_id = uint64_t{readoutwindow_event_counter + fOverflowCounter*(max_rwcounter+1u)};
    fragment_uptr->setSequenceID(readoutwindow_sequence_id);

    size_t ts_count;
    {
      std::lock_guard<std::mutex> lock(fTimestampMapMutex);
      ts_count = fTimestampMap.count(readoutwindow_event_counter);
    }
    int ts_loop=0;

    while(ts_loop<3 && ts_count==0){
      TLOG(TLVL_WARNING) << " TIMESTAMP FOR SEQID " << readoutwindow_sequence_id << " EVCOUNTER " << readoutwindow_event_counter << " not found in fTimestampMap!"
			 << " Will sleep for 200 ms and try again. Times tried = " << ts_loop;
      ::usleep(200000);
      ts_loop++;
      {
	std::lock_guard<std::mutex> lock(fTimestampMapMutex);
	ts_count = fTimestampMap.count(readoutwindow_event_counter);
      }
    }

    //check where we are now in time
    artdaq::Fragment::timestamp_t ts_frag,ts_now;
    {
      using namespace boost::gregorian;
      using namespace boost::posix_time;
      
      ptime t_now(microsec_clock::universal_time());
      ptime time_t_epoch(date(1970,1,1));
      time_duration diff = t_now - time_t_epoch;
      
      ts_now = diff.total_nanoseconds();
    }

    ts_count=0;
    {
      std::lock_guard<std::mutex> lock(fTimestampMapMutex);
      ts_count = fTimestampMap.count(readoutwindow_event_counter);
    }

    if(ts_count>0){
      std::lock_guard<std::mutex> lock(fTimestampMapMutex);      
      ts_frag = fTimestampMap.at(readoutwindow_event_counter);
      fTimestampMap.erase(readoutwindow_event_counter);
    }
    else{
      TLOG(TLVL_ERROR) << " TIMESTAMP FOR SEQID " << readoutwindow_sequence_id << " EVCOUNTER " << readoutwindow_event_counter << " not found in fTimestampMap!"
		       << " Will generate new one now...";

      if(fUseTimeTagForTimeStamp){
	const auto TTT = uint32_t {header->triggerTimeTag};
	
	using namespace boost::gregorian;
	using namespace boost::posix_time;
	
	ptime t_now(second_clock::universal_time());
	ptime time_t_epoch(date(1970,1,1));
	time_duration diff = t_now - time_t_epoch;
	uint32_t t_offset_s = diff.total_seconds();
	uint64_t t_offset_ticks = diff.total_seconds()*125000000; //in 8ns ticks
	uint64_t t_truetriggertime = t_offset_ticks + TTT;
	TLOG_ARB(TMAKEFRAG,TRACE_NAME) << "time offset = " << t_offset_ticks << " ns since the epoch"<< TLOG_ENDL;
	
	ts_frag = (t_truetriggertime*8); //in 1ns ticks
      }
      else if(fUseTimeTagShiftForTimeStamp){
	const auto TTT = uint32_t {header->triggerTimeTag};
	
	using namespace boost::gregorian;
	using namespace boost::posix_time;
	
	ptime t_now(second_clock::universal_time());
	ptime time_t_epoch(date(1970,1,1));
	time_duration diff = t_now - time_t_epoch;
	uint32_t t_offset_s = diff.total_seconds();
	uint64_t t_offset_ticks = diff.total_seconds()*125000000; //in 8ns ticks
	uint64_t t_truetriggertime = t_offset_ticks + TTT;
	TLOG_ARB(TMAKEFRAG,TRACE_NAME) << "time offset = " << t_offset_ticks << " ns since the epoch"<< TLOG_ENDL;

	// TTT is 8 ticks/ns, record length is 16 ticks/ns. See CAEN V1740 manuals for details
	ts_frag = (t_truetriggertime*8.0) - (((double)fCAEN.recordLength * 16.0) * ((double)fCAEN.postPercent / 100.0)); //in 1ns ticks
      }
      else{
	using namespace boost::gregorian;
	using namespace boost::posix_time;
	
	ptime t_now(microsec_clock::universal_time());
	ptime time_t_epoch(date(1970,1,1));
	time_duration diff = t_now - time_t_epoch;
	
	ts_frag = diff.total_nanoseconds() - fTimeOffsetNanoSec;;
      }
    }

    TLOG_ARB(TMAKEFRAG,TRACE_NAME) << "fragment timestamp in 1ns ticks = " << ts_frag << TLOG_ENDL;
    TLOG_ARB(TMAKEFRAG,TRACE_NAME) << "Difference to now in ns is = " << (ts_now - ts_frag) << TLOG_ENDL;

    if( ts_frag>ts_now )
      TLOG(TLVL_WARNING) << "Fragment assigned timestamp is after timestamp from fragment creation! Causality problem!!"
			 << "ts_frag - ts_now = " << ts_frag - ts_now << " ns!"
			 << TLOG_ENDL;

    else if( (ts_now-ts_frag)>5e9 ){
      TLOG(TLVL_ERROR) << "Fragment being packged more than 5 seconds after timestamp: "
		       << "ts_now - ts_frag = " << ts_now-ts_frag << " ns!"
		       << TLOG_ENDL;
    }
    else if( (ts_now-ts_frag)>1e9 ){
      TLOG(TLVL_WARNING) << "Fragment being packged more than 1 second after timestamp: "
			 << "ts_now - ts_frag = " << ts_now-ts_frag << " ns!"
			 << TLOG_ENDL;
    }
    metricMan->sendMetric("FragmentCreationGapMax", (ts_now-ts_frag), "ns", 12, artdaq::MetricMode::Maximum);
    metricMan->sendMetric("FragmentCreationGapAvg", (ts_now-ts_frag), "ns", 12, artdaq::MetricMode::Average);


    fragment_uptr->setTimestamp( ts_frag );

    // check for possible gaps in the sequence IDs: compare current one with last sent
    // throw errors if gap > 1 or order is not correct
    auto readoutwindow_sequence_id_gap= readoutwindow_sequence_id - last_sent_seqid;

    TLOG(TMAKEFRAG)<<"Created fragment " << fFragmentID << " sequenceID " << readoutwindow_sequence_id << " for event " << readoutwindow_event_counter
    // jcrespo: promote fragment message to TINFO
    //TLOG(TINFO)<<"Created fragment " << fFragmentID << " sequenceID " << readoutwindow_sequence_id << " for event " << readoutwindow_event_counter
                   << " triggerTimeTag " << header->triggerTimeTag << " ts=" << ts_frag;
    
    if( readoutwindow_sequence_id_gap > 1u ){
      if ( last_sent_seqid > 0 )
      {
	TLOG (TLVL_DEBUG) << "Missing data; previous fragment sequenceID / gap  = " << last_sent_seqid << " / "
                        << readoutwindow_sequence_id_gap;
	metricMan->sendMetric("Missing Fragments", uint64_t{readoutwindow_sequence_id_gap}, "frags", 11, artdaq::MetricMode::Accumulate);
      }
    }

    if( readoutwindow_sequence_id < last_sent_seqid )
      {
	TLOG(TLVL_ERROR) << "SequenceIDs processed out of order!! "
			 << readoutwindow_sequence_id << " < " << last_sent_seqid << TLOG_ENDL;
      }
    if( last_sent_ts > ts_frag)
      {
	TLOG(TLVL_ERROR) << "Timestamps out of order!! Last event later than current one."
			 << ts_frag << " < " << last_sent_ts << TLOG_ENDL;
      }

    fragments.emplace_back(nullptr);
    std::swap(fragments.back(),fragment_uptr);
   
    // keep track of CAEN event counter overflows
    // this is important to avoid repeating sequenceIDs
    if( readoutwindow_event_counter == max_rwcounter ) fOverflowCounter++;
    fEvCounter++;

    last_sent_seqid = readoutwindow_sequence_id;
    last_sent_ts = ts_frag;
    delta = std::chrono::steady_clock::now()-start;

    min_fragment_create_time=std::min(delta.count(),min_fragment_create_time);
    max_fragment_create_time=std::max(delta.count(),max_fragment_create_time);

    if (delta.count() >0.0005 ) {
      metricMan->sendMetric("Laggy Fragments",1,"frags",11,artdaq::MetricMode::Maximum);
      TLOG (TLVL_DEBUG+1) << "Creating a fragment with setSequenceID=" << last_sent_seqid <<  " took " << delta.count()*1000 << " ms";
//TRACE_CNTL("modeM", 0);
    }
  }

  metricMan->sendMetric("Fragment Create Time  Max",max_fragment_create_time,"s",11,artdaq::MetricMode::Accumulate);
 // metricMan->sendMetric("Fragment Create Time  Min" ,min_fragment_create_time,"s",1,artdaq::MetricMode::Accumulate);

  TLOG(TGETNEXT) << "End of readSingleWindowFragments(); returning " << fragments.size() << " fragments.";

  start= std::chrono::steady_clock::now();

  return true;
}


void sbndaq::CAENV1740Readout::CheckReadback(std::string label,
					      int boardID,
					      uint32_t wrote,
					      uint32_t readback,
					      int channelID)
{
  if (wrote != readback){

    std::stringstream channelLabel(" ");
    if (channelID >= 0)
      channelLabel << " Ch/Grp " << channelID;
    
    std::stringstream text;
    text << " " << label << 
      " ReadBack error BoardId " << boardID << channelLabel.str() 
	 << " wrote " << wrote << " read " << readback;
    TLOG(TLVL_ERROR ) << "" << text.str();

    //sbndaq::CAENException e(CAEN_DGTZ_DigitizerNotReady,
    //			     text.str(), boardId);
    //throw(e);
  }
  
}

void sbndaq::CAENV1740Readout::GetSWInfo(){

  int retcod=0;
  CAEN_DGTZ_BoardInfo_t info;

  // CAEV1740 S/N and firmware
  retcod = CAEN_DGTZ_GetInfo(fHandle,&info);
  if( retcod == CAEN_DGTZ_Success ){
    TLOG(TLVL_INFO) << info.ModelName << " S/N: " << info.SerialNumber 
                    << "\nFirmware ROC: " << info.ROC_FirmwareRel 
                    << "\nFirmware AMC: " << info.AMC_FirmwareRel;
  }

  // CAEN software releases
  char CommSWrel[30], VMESWrel[30], DGTZSWrel[30];
  retcod = CAEN_DGTZ_SWRelease( DGTZSWrel );
  retcod = CAENVME_SWRelease( VMESWrel );
  retcod = CAENComm_SWRelease( CommSWrel );
  TLOG(TLVL_INFO) << "Software releases"
		  << "\ncaendigitizer: " << DGTZSWrel
                  << "\ncaenvme: " << VMESWrel
                  << "\ncaencomm: " << CommSWrel;
  
  // A3818 firmware / driver
  short Device = 0;
  int32_t BHandle;

  if( CAENVME_Init2(cvA3818, &fCAEN.link, Device, &BHandle) == cvSuccess ) {
  
    char fwrev[100];
    char drrev[100];
    auto ret = CAENVME_BoardFWRelease(BHandle,fwrev);

    std::ostringstream a3818Stream;
    a3818Stream << "A3818 firmware: ";
    if (!ret) a3818Stream << fwrev << "\n";
    else a3818Stream << CAENVME_DecodeError(ret) << "\n";

    ret = CAENVME_DriverRelease( BHandle, drrev );
    a3818Stream << "A3818 driver: ";
    if (!ret) a3818Stream << drrev;
    else a3818Stream << CAENVME_DecodeError(ret);
   
    TLOG(TLVL_INFO) << a3818Stream.str();

    CAENVME_End(BHandle);
  }
}

DEFINE_ARTDAQ_COMMANDABLE_GENERATOR(sbndaq::CAENV1740Readout)
