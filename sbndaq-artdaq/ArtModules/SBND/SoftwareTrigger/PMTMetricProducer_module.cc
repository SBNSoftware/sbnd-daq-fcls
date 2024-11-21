////////////////////////////////////////////////////////////////////////
// Class:       pmtSoftwareTriggerProducer
// Plugin Type: producer (Unknown Unknown)
// File:        pmtSoftwareTriggerProducer_module.cc
//
// Generated at Thu Feb 17 13:22:51 2022 by Patrick Green using cetskelgen
// from  version .

// Authors: Erin Yandel, Patrick Green, Lynn Tung
// Contact: Lynn Tung, lynnt@uchicago.edu 

// Module to produce PMT Flash Metrics
// Input: CAEN 1730 fragments 

// Output: sbnd::trigger::pmtSoftwareTrigger data product

// More information can be found at:
// https://sbnsoftware.github.io/sbndcode_wiki/SBND_Trigger
////////////////////////////////////////////////////////////////////////

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
#include "canvas/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "artdaq/DAQdata/Globals.hh"


#include "artdaq-core/Data/Fragment.hh"
#include "artdaq-core/Data/ContainerFragment.hh"
#include "artdaq-core/Data/RawEvent.hh"

#include "sbndaq-artdaq-core/Overlays/Common/CAENV1730Fragment.hh"
#include "sbndaq-artdaq-core/Overlays/FragmentType.hh"
#include "sbndaq-artdaq-core/Overlays/SBND/TDCTimestampFragment.hh"
#include "sbndaq-artdaq-core/Obj/SBND/pmtSoftwareTrigger.hh"

// ROOT includes
#include "TFile.h"

#include <memory>
#include <algorithm>
#include <valarray>
#include <numeric>


using artdaq::MetricMode;

namespace sbnd {
  namespace trigger {
    class pmtSoftwareTriggerProducer;
  }
}

class sbnd::trigger::pmtSoftwareTriggerProducer : public art::EDProducer {
public:
  explicit pmtSoftwareTriggerProducer(fhicl::ParameterSet const& p);
  // The compiler-generated destructor is fine for non-base
  // classes without bare pointers or other resource use.

  // Plugins should not be copied or assigned.
  pmtSoftwareTriggerProducer(pmtSoftwareTriggerProducer const&) = delete;
  pmtSoftwareTriggerProducer(pmtSoftwareTriggerProducer&&) = delete;
  pmtSoftwareTriggerProducer& operator=(pmtSoftwareTriggerProducer const&) = delete;
  pmtSoftwareTriggerProducer& operator=(pmtSoftwareTriggerProducer&&) = delete;

  // Required functions.
  void produce(art::Event& e) override;

private:

  // Declare member data here.

  // fhicl parameters
  art::Persistable is_persistable_;
  std::vector<std::string> fCAENInstanceLabels; // instance labels for the CAEN V1730 modules

  float    fWvfmPostPercent; // post percent of wvfm, 8 us after trigger = 0.8
  float    fWindowStart;     // fraction of wvfm to start window for metrics, default is as 1-fWvfmPostPercent
  float    fWindowLength;    // in us, window length after fWindowStart, default 1.6 us
  uint16_t fWvfmLength;      // expected waveform length in samples

  uint8_t  fStreamType;  // 0 is random, 1 is beam zero bias, 2 is 1+light, 3 is offbeam zero bias, 4 is 3+light, 5 is crt crossing muon
  uint8_t  fTimingType;  // 0 for SPEC TDC,  1 for rawheader
  uint32_t fNTBDelay;    // in ns, NTB offset
  
  std::string              fSPECTDCModuleLabel;
  std::vector<std::string> fSPECTDCInstanceLabels;
  uint8_t   fSPECTDCTimingChannel; // 1 is bes, 2 is rwm, and 4 is ett
  int32_t   fSPECTDCDelay; // in ns, time difference between tdc ftrig and caen ftrig 

  std::vector<uint16_t> fFragIDs;

  uint16_t fVerbose;

  bool fCalculateBaseline;  
  bool fCountPMTs;          
  bool fCalculatePEMetrics; 
  bool fFindFlashInfo;

  std::vector<float> fInputBaseline; 
  float fPromptWindow;
  float fPrelimWindow; 

  float fFlashThreshold; // for cout/message viewer; signal vs. noise threshold to print
  float fADCThreshold;  
  float fADCtoPE;       
  // end fhicl parameters

  // event-by-event global variables
  // bool foundBeamTrigger;

  // other global variables
  float ticks_to_us;
  float us_to_ticks;

  // pmt information
  std::map<int,int> map_fragid_index;

  bool     getTDCTime(artdaq::Fragment & frag, 
                      std::vector<double> & tdcTime,
                      uint8_t tdcChannel);
  int8_t   getClosestFTrig(double refTime, std::vector<double> & ftrig_v);
  void     getWaveforms(const artdaq::Fragment &frag, std::vector<std::vector<uint16_t>> &wvfm_v);
  uint32_t getStartTime(const artdaq::Fragment &frag);
  uint32_t getTriggerTime(const artdaq::Fragment &frag);
  uint32_t getLength   (const artdaq::Fragment &frag);
  float    estimateBaseline(std::vector<uint32_t> wvfm);
  float    estimateBaseline(std::vector<uint16_t> wvfm);
  void     reconfigure(fhicl::ParameterSet const & p);
  std::vector<uint32_t> sumWvfms(const std::vector<uint32_t>& v1, const std::vector<uint16_t>& v2);
};


sbnd::trigger::pmtSoftwareTriggerProducer::pmtSoftwareTriggerProducer(fhicl::ParameterSet const& p)
  : EDProducer{p}
  {
    this->reconfigure(p);
    // Call appropriate produces<>() functions here.
    produces< std::vector<sbnd::trigger::pmtSoftwareTrigger>>("", is_persistable_);
    // map from fragID to array index 0-7
    for (size_t i=0;i<fFragIDs.size();++i){
      map_fragid_index.insert(std::make_pair(fFragIDs[i],i));
    }

    us_to_ticks = 500.; // ticks per us 
    ticks_to_us = 1/us_to_ticks; // us per ticks
 
  }

void sbnd::trigger::pmtSoftwareTriggerProducer::reconfigure(fhicl::ParameterSet const & p)
{
  // Initialize member data here
  is_persistable_     = p.get<bool>("is_persistable", true) ? art::Persistable::Yes : art::Persistable::No;

  fCAENInstanceLabels = p.get<std::vector<std::string>>("CAENInstanceLabels", {"ContainerCAENV1730"});
  fWvfmPostPercent    = p.get<float>("WvfmPostPercent", 0.8); // trigger is 20% of the way into the wvfm 
  fWindowStart        = p.get<float>("WindowStart", 1-fWvfmPostPercent); // start of window for metrics, default is 1-fWvfmPostPercent (starts at FTRIG)
  fWindowLength       = p.get<float>("WindowLength", 1.8); // in us, window after fWindowStart to look at metrics
  fWvfmLength         = p.get<uint16_t>("WaveformLength", 5000); // expected waveform length in samples

  // 0 is random, 1 is beam zero bias, 2 is 1+light, 3 is offbeam zero bias, 4 is 3+light, 5 is crt crossing muon
  fStreamType         = p.get<uint8_t>("StreamType", 1); 
  // SPEC TDC ETT [0] -> NTB (RawEventHeader) [1]
  fTimingType         = p.get<uint8_t>("TimingType", 0);
  fNTBDelay           = p.get<uint32_t>("NTBDelay", 365000); // units of ns

  fSPECTDCModuleLabel    = p.get<std::string>("SPECTDCModuleLabel", "daq");
  fSPECTDCInstanceLabels = p.get<std::vector<std::string>>("SPECTDCInstanceLabels", {"TDCTIMESTAMP", "ContainerTDCTIMESTAMP" }); 
  // 1 is bes, 2 is rwm, 3 is ftrig, 4 is ett 
  fSPECTDCTimingChannel  = p.get<uint8_t>("SPECTDCTimingChannel", 4);
  fSPECTDCDelay          = p.get<int32_t>("SPECTDCDelay", 140); // difference between caen ftrig and tdc ftrig in ns

  fFragIDs            = p.get<std::vector<uint16_t>>("FragIDs", {40960,40961,40962,40963,40964,40965,40966,40967});

  // relevant for offline debugging only
  fVerbose            = p.get<uint8_t>("Verbose", 0);

  // most likely these will all be off...
  fCalculateBaseline  = p.get<bool>("CalculateBaseline",false);
  fCountPMTs          = p.get<bool>("CountPMTs",false);
  fCalculatePEMetrics = p.get<bool>("CalculatePEMetrics",false);
  fInputBaseline      = p.get<std::vector<float>>("InputBaseline",{14250,2.0});

  fFindFlashInfo      = p.get<bool>("FindFlashInfo",true);

  fPromptWindow       = p.get<float>("PromptWindow", 0.1); // in us 
  fPrelimWindow       = p.get<float>("PrelimWindow", 0.5); // in us
  fFlashThreshold     = p.get<float>("FlashThreshold", 0); // in PE, for cout/message viewer; signal vs. noise threshold to print
  fADCThreshold       = p.get<float>("ADCThreshold", 14200);
  fADCtoPE            = p.get<float>("ADCtoPE", 12.5);   // for gain of 5e6, conversion is about 12.5 
}


void sbnd::trigger::pmtSoftwareTriggerProducer::produce(art::Event& e)
{
  if (fVerbose==1) std::cout << "Processing Run: " << e.run() << ", Subrun: " <<  e.subRun() << ", Event: " << e.id().event() << std::endl;
  // object to store trigger metrics in
  std::unique_ptr<std::vector<sbnd::trigger::pmtSoftwareTrigger>> trig_metrics_v = std::make_unique<std::vector<sbnd::trigger::pmtSoftwareTrigger>>();
  sbnd::trigger::pmtSoftwareTrigger trig_metrics;

  // the reference time stamp, usually the event trigger time, used to find the right FTRIG
  double refTimestamp=0; 
  auto timing_type = fTimingType;

  // section to obtain global timing information 
  art::Handle<artdaq::detail::RawEventHeader> header_handle;
  double raw_timestamp = 0;
  e.getByLabel("daq", "RawEventHeader", header_handle);
  if ((header_handle.isValid())){
    auto rawheader = artdaq::RawEvent(*header_handle);
    raw_timestamp = rawheader.timestamp()%int(1e9) - fNTBDelay;
  }

  if (timing_type==0){
    bool found_tdc_timing_ch = false;
    std::vector<double> tdc_etrig_v;
    tdc_etrig_v.reserve(2);
    for(const std::string &SPECTDCInstanceLabel : fSPECTDCInstanceLabels){
      art::Handle<std::vector<artdaq::Fragment>> tdcHandle;
      e.getByLabel(fSPECTDCModuleLabel, SPECTDCInstanceLabel, tdcHandle);

      if(!tdcHandle.isValid() || tdcHandle->size() == 0)
        continue;

      if(tdcHandle->front().type() == artdaq::Fragment::ContainerFragmentType){
        for(auto cont : *tdcHandle){
          artdaq::ContainerFragment contf(cont);
          if(contf.fragment_type() == sbndaq::detail::FragmentType::TDCTIMESTAMP){
            for(unsigned i = 0; i < contf.block_count(); ++i)
              found_tdc_timing_ch = getTDCTime(*contf[i].get(),tdc_etrig_v,fSPECTDCTimingChannel);
          }
        }
      }
      else if((tdcHandle->front().type() == sbndaq::detail::FragmentType::TDCTIMESTAMP) && (found_tdc_timing_ch==false)){
        for(auto frag : *tdcHandle)
          found_tdc_timing_ch = getTDCTime(frag,tdc_etrig_v,fSPECTDCTimingChannel);
      }
    }
    tdc_etrig_v.shrink_to_fit();
    if ((found_tdc_timing_ch) && (tdc_etrig_v.size()>0)){
      double tdc_etrig = 1e9; // used to **find** the reference time stamp (closest ftrig)
      int32_t min_raw_tdc_diff = 1e9;
      // if there is more than one etrig, use the one closest to the raw timestamp (NTB)
      if (tdc_etrig_v.size()==1) tdc_etrig = tdc_etrig_v[0];
      else{
        for (size_t i=0; i < tdc_etrig_v.size(); i++){
          double raw_diff = tdc_etrig_v[i] - raw_timestamp;
          if (std::abs(raw_diff) < min_raw_tdc_diff){
            tdc_etrig = tdc_etrig_v[i];
            min_raw_tdc_diff = raw_diff;
          }
        }
      }
      if (tdc_etrig!=1e9)
        refTimestamp = tdc_etrig - fSPECTDCDelay;
      else{
        if (fVerbose>=2) TLOG(TLVL_INFO) << "No valid TDC timestamp found. Using NTB..." ;
        timing_type++;
      }
    }
    else{
      if (fVerbose>=2) TLOG(TLVL_INFO) << "No valid TDC timestamp found. Using NTB..." ;
      timing_type++;
    }
  }
  if (timing_type==1){
    if ((header_handle.isValid()) & (raw_timestamp!=0))
      refTimestamp = raw_timestamp;
    else timing_type++;
  }
  if (timing_type>1){
    if (fVerbose>=1) TLOG(TLVL_WARNING)<< "No valid timing reference found. Producing empty PMT metrics." ;
    e.put(std::move(trig_metrics_v));
    return;
  }

  std::vector<std::vector<uint16_t>> wvfms_v;
  auto nboards = fFragIDs.size();
  if (fVerbose>=3) TLOG(TLVL_DEBUG) << "Allocating for " << 15*nboards << " PMT channels";
  wvfms_v.resize(15*nboards); // 15 PMT channels per 1730 fragment, 8 fragments per trigger

  // variables to find the beam fragment index 
  std::vector<double> caen_ftrig_v;
  // storage for the beam fragments 
  // the index inside a container to find the "beam" fragment (closest to the event trigger time)
  size_t etrig_frag_idx = 0;
  // to store the time difference between the beam fragment **trigger** time and event trigger time
  int32_t etrig_frag_dt = 1e9; 

  // loop over fragment handles
  bool foundfragments = false;
  for (const std::string &caen_name : fCAENInstanceLabels){
    art::Handle<std::vector<artdaq::Fragment>> fragmentHandle;
    e.getByLabel("daq", caen_name, fragmentHandle);

    if (!fragmentHandle.isValid() || fragmentHandle->size() == 0){
        TLOG(TLVL_WARNING) << "No CAEN V1730 fragments with label " << caen_name << " found.";
        continue;
    }
    // Container fragment
    // # of containers = # of boards
    // # of entries inside the container = # of triggers 
    if (fragmentHandle->front().type() == artdaq::Fragment::ContainerFragmentType) {
      foundfragments = true;
      for (auto cont : *fragmentHandle) {
	      artdaq::ContainerFragment contf(cont);
        if (contf.fragment_type()==sbndaq::detail::FragmentType::CAENV1730) {
          if (std::find(fFragIDs.begin(), fFragIDs.end(), contf[0].get()->fragmentID()) == fFragIDs.end()) continue;
          if (fVerbose>=3) TLOG(TLVL_INFO) << "Found " << contf.block_count() << " CAEN1730 fragments in container with fragID " << contf[0].get()->fragmentID();

          if (etrig_frag_dt==1e9){
            for (size_t ii = 0; ii < contf.block_count(); ++ii){
              // find the absolute time difference between the fragment trigger time and the reference time stamp 
              auto len = getLength(*contf[ii].get());
              if (len<fWvfmLength) continue;  // ignore extended triggers!!!
              caen_ftrig_v.push_back(getTriggerTime(*contf[ii].get()));
            }
            auto min_idx = getClosestFTrig(refTimestamp, caen_ftrig_v);
            if (min_idx<0){
              if (fVerbose>=1) TLOG(TLVL_WARNING) << "No matching CAEN FTrig found" ;
              e.put(std::move(trig_metrics_v));
              return;
            }
            else{
              etrig_frag_idx = min_idx;
              etrig_frag_dt = caen_ftrig_v[etrig_frag_idx] - refTimestamp;
            }
          }
          getWaveforms(*contf[etrig_frag_idx].get(), wvfms_v);
        }
      } // loop over containers
      if (fVerbose>=2)
        TLOG(TLVL_INFO) <<"Min time difference: " << etrig_frag_dt << " ns\n"
                        <<"Index of closest fragment: " << etrig_frag_idx; 
    } // if 1730 container
  } // loop over handles

  if (foundfragments==false){
      TLOG(TLVL_WARNING)<< "No CAEN fragments found... producing empty PMT metrics." << std::endl;
      e.put(std::move(trig_metrics_v));
      return;
  }
  
    // calculate metrics
  if ((refTimestamp!=0) & (etrig_frag_dt!=1e9)) {    
    trig_metrics.foundBeamTrigger = false;

    // store the difference between the reference time stamp and the closest CAEN FTrig
    trig_metrics.trig_ts = (float)etrig_frag_dt;
    if (fVerbose>=3) TLOG(TLVL_INFO) << "Saving timestamp: " << etrig_frag_dt << " ns";

    float promptPE = 0;
    float prelimPE = 0;

    // to store flash metrics 
    float flash_promptPE = 0;
    float flash_prelimPE = 0;
    float flash_peakPE   = 0;
    float flash_peaktime = 0;

    int nAboveThreshold = 0;

    // create a vector to contain the sum of all the wvfms
    auto wvfm_length = wvfms_v[0].size();
    if (fVerbose>=3) TLOG(TLVL_INFO) << "Initializing flash waveform length to " << wvfm_length;
    std::vector<uint32_t> wvfm_sum(wvfm_length, 0);

    int windowStartBin = fWindowStart*wvfm_length; 
    int windowEndBin = windowStartBin + int(fWindowLength*us_to_ticks);
    auto prelimStart = (windowStartBin - fPrelimWindow*us_to_ticks) < 0 ? 0 : windowStartBin - fPrelimWindow*us_to_ticks;
    auto promptEnd   = windowStartBin + fPromptWindow*us_to_ticks;  
    if ((fVerbose>=3) && (fCountPMTs | fCalculatePEMetrics | fFindFlashInfo)){
      TLOG(TLVL_INFO) << "Using postpercent: " << fWvfmPostPercent << "\n"
                      << "Window start-end bins: [" << windowStartBin << " " << windowEndBin << "]\n" 
                      << "Window start-end times: [" 
                      << (windowStartBin-wvfm_length*(1-fWvfmPostPercent))*ticks_to_us + etrig_frag_dt*1e-3
                      << ", "
                      << (windowEndBin-wvfm_length*(1-fWvfmPostPercent))*ticks_to_us + etrig_frag_dt*1e-3
                      << "]";
    }
    if (fCountPMTs | fCalculatePEMetrics | fFindFlashInfo){
      if (fVerbose>=4) TLOG(TLVL_INFO) << "Starting wvfm loop...";
      for (size_t i_ch = 0; i_ch < wvfms_v.size(); ++i_ch){
        auto wvfm = wvfms_v[i_ch];
        if (wvfm.begin() == wvfm.end()) continue;
        
        // count number of PMTs above threshold
        if (fCountPMTs){
          if (windowEndBin >= (int)wvfm.size()){ windowEndBin = (int)wvfm.size() - 1;}
          for (int bin = windowStartBin; bin < windowEndBin; ++bin){
            auto adc = wvfm[bin];
              if (adc!=0 && adc < fADCThreshold){ nAboveThreshold++; break; }
          }
        }
        else { nAboveThreshold = -9999; }

        if (fCalculatePEMetrics){
          auto baseline = (fCalculateBaseline) ? estimateBaseline(wvfm) : fInputBaseline.at(0);
          // this sums the peaks, not the integral!
          float ch_prelimPE = (baseline-(*std::min_element(wvfm.begin()+prelimStart, wvfm.begin()+windowStartBin)))/fADCtoPE;
          float ch_promptPE = (baseline-(*std::min_element(wvfm.begin()+windowStartBin, wvfm.begin()+promptEnd)))/fADCtoPE;

          promptPE += ch_promptPE;
          prelimPE += ch_prelimPE;
        }
        else  { promptPE = -9999; prelimPE = -9999;}
        if (fFindFlashInfo) wvfm_sum = sumWvfms(wvfm_sum, wvfm);
      } // end wvfm loop
    }
    if (fFindFlashInfo){
      auto flash_baseline = (fCalculateBaseline)? estimateBaseline(wvfm_sum) : fInputBaseline.at(0)*15*nboards;
      if (fVerbose>=4) TLOG(TLVL_INFO) << "Flash baseline is: " << flash_baseline << "\nNow calculating flash metrics...";

      flash_prelimPE = (flash_baseline-(*std::min_element(wvfm_sum.begin()+prelimStart,wvfm_sum.begin()+windowStartBin)))/fADCtoPE;
      flash_promptPE = (flash_baseline-(*std::min_element(wvfm_sum.begin()+windowStartBin,wvfm_sum.begin()+promptEnd)))/fADCtoPE;
      flash_peakPE   = (flash_baseline-(*std::min_element(wvfm_sum.begin(),wvfm_sum.end())))/fADCtoPE;
      auto flash_peak_it = std::min_element(wvfm_sum.begin(),wvfm_sum.end());

      // time is referenced to the start of the waveform; this is correct assuming we grabbed the right fragment!  
      flash_peaktime = ((std::distance(wvfm_sum.begin(), flash_peak_it)) - wvfm_sum.size()*(1-fWvfmPostPercent))*ticks_to_us; // us
    }
    trig_metrics.nAboveThreshold = nAboveThreshold;
    if (fCalculatePEMetrics){
      trig_metrics.promptPE = promptPE;
      trig_metrics.prelimPE = prelimPE;
    }
    if (fFindFlashInfo){
      trig_metrics.promptPE = flash_promptPE;
      trig_metrics.prelimPE = flash_prelimPE;
      trig_metrics.peakPE = flash_peakPE;
      trig_metrics.peaktime = flash_peaktime;
    }
    if (fVerbose>=2){
      if (fCountPMTs) TLOG(TLVL_INFO) << "nPMTs Above Threshold: " << nAboveThreshold ;
      if (fCalculatePEMetrics){
        TLOG(TLVL_INFO)  << "prelim pe: " << prelimPE;
        TLOG(TLVL_INFO)  << "prompt pe: " << promptPE;
      }
      if (fFindFlashInfo && flash_peakPE>fFlashThreshold){
        TLOG(TLVL_INFO) << "Flash Peak PE: " << flash_peakPE << " PE " 
                        << "\nFlash Peak time: " << flash_peaktime << " us";
      }
    }
    trig_metrics_v->push_back(trig_metrics);
    // clear variables to free memory
    wvfms_v.clear(); 
  }  // if found trigger
  else{
    if (fVerbose>=1) TLOG(TLVL_WARNING) << "No valid reference time stamp or CAEN timestamps found. Producing empty PMT metrics.";

    trig_metrics.foundBeamTrigger = false;
    trig_metrics.trig_ts = -9999;
    trig_metrics.nAboveThreshold = -9999;
    trig_metrics.promptPE = -9999;
    trig_metrics.prelimPE = -9999;
    trig_metrics.peakPE   = -9999;
    trig_metrics.peaktime = -9999;
  }
  e.put(std::move(trig_metrics_v));
}

bool sbnd::trigger::pmtSoftwareTriggerProducer::getTDCTime(artdaq::Fragment & frag, std::vector<double> & tdcTime, uint8_t tdcChannel ) {

  bool found_timing_ch = false;
  const auto tsfrag = sbndaq::TDCTimestampFragment(frag);
  const auto ts = tsfrag.getTDCTimestamp();
  if (ts->vals.channel == tdcChannel){
    found_timing_ch = true;
    double tdc_ns = ts->timestamp_ns()%(uint64_t(1e9));
    tdcTime.push_back(tdc_ns);
    if (fVerbose>=3){
      TLOG(TLVL_INFO)     << "TDC CH "<< ts->vals.channel
                          << " -> timestamp: " << ts->timestamp_ns()%(uint64_t(1e9)) << " ns" 
                          << ", name: "
                          << ts->vals.name[0]
                          << ts->vals.name[1]
			                    << ts->vals.name[2]
			                    << ts->vals.name[3]
                          << ts->vals.name[4];
    }
  }
  return found_timing_ch;
}

int8_t sbnd::trigger::pmtSoftwareTriggerProducer::getClosestFTrig(double refTime, std::vector<double> & ftrig_v){
  double min_diff = 1e9;
  int8_t min_idx = -1;
  for (size_t i=0; i<ftrig_v.size(); i++){
    auto iftrig = ftrig_v[i];
    double diff = ftrig_v[i] - refTime;
    if ((fStreamType==1) || (fStreamType==3) || (fStreamType==5)){
      // if zero bias (without light) or CRT crossing, simply find the closest
      if (std::abs(diff) < min_diff){
        min_idx = int8_t(i);
        min_diff = std::abs(diff);
      }
    }
    if ((fStreamType==2) || (fStreamType==4)){
      // if zero bias (with light), FTRIG should be before ETRIG
      if ((diff < 0) && (std::abs(diff) < min_diff)){ 
        min_idx = int8_t(i);
        min_diff = std::abs(diff);
      }
    }
  }
  return min_idx;
}

std::vector<uint32_t> sbnd::trigger::pmtSoftwareTriggerProducer::sumWvfms(const std::vector<uint32_t>& v1, const std::vector<uint16_t>& v2) 
{
  size_t result_len = (v1.size() > v2.size()) ? v1.size() : v2.size();
  std::vector<uint32_t>  result(result_len,0);
  for (size_t i = 0; i < result_len; i++){
    auto value1 = (i < v1.size()) ? v1[i] : 0;
    auto value2 = (i < v2.size()) ? v2[i] : 0;
    result.at(i) = value1 + value2;
  }
  return result;
}

void sbnd::trigger::pmtSoftwareTriggerProducer::getWaveforms(const artdaq::Fragment &frag, std::vector<std::vector<uint16_t>> & wvfm_v)
{
  sbndaq::CAENV1730Fragment bb(frag);
  auto const* md = bb.Metadata();
  sbndaq::CAENV1730Event const* event_ptr = bb.Event();
  sbndaq::CAENV1730EventHeader header = event_ptr->Header;
  
  uint32_t ttt = header.triggerTimeTag*8; // downsampled trigger time tag; TTT points to end of wvfm w.r.t. pps

  int fragId = static_cast<int>(frag.fragmentID());
  int findex = map_fragid_index.find(fragId)->second;

  size_t nChannels = md->nChannels;
  uint32_t ev_size_quad_bytes = header.eventSize;
  uint32_t evt_header_size_quad_bytes = sizeof(sbndaq::CAENV1730EventHeader)/sizeof(uint32_t);
  uint32_t data_size_double_bytes = 2*(ev_size_quad_bytes - evt_header_size_quad_bytes);
  uint32_t wvfm_length = data_size_double_bytes/nChannels;

  if (fVerbose>=4){
    TLOG(TLVL_INFO) << "FragID " <<  fragId << ": downsampled TTT is " << ttt 
                    << "\n\tNumber of channels: " << nChannels 
                    << "\n\tChannel waveform length = " << wvfm_length;
  }
  //--access waveforms in fragment and save
  const uint16_t* data_begin = reinterpret_cast<const uint16_t*>(frag.dataBeginBytes()
					+ sizeof(sbndaq::CAENV1730EventHeader));
  const uint16_t* value_ptr =  data_begin;
  uint16_t value = 0;
  size_t ch_offset = 0;

  // loop over channels
  if (nChannels==16) nChannels--; // last waveform isn't a PMT
  for (size_t i_ch = 0; i_ch < nChannels; ++i_ch){
    if (fVerbose>=4)
      TLOG(TLVL_INFO) << "Getting waveform for Channel idx: " << i_ch + nChannels*findex;
    wvfm_v[i_ch + nChannels*findex].resize(wvfm_length);
    ch_offset = (size_t)(i_ch * wvfm_length);
    //--loop over waveform samples
    for(size_t i_t = 0; i_t < wvfm_length; ++i_t){
      value_ptr = data_begin + ch_offset + i_t; // pointer arithmetic
      value = *(value_ptr);
      wvfm_v[i_ch + nChannels*findex][i_t] = value;
    } //--end loop samples
  } //--end loop channels
}

uint32_t sbnd::trigger::pmtSoftwareTriggerProducer::getStartTime(const artdaq::Fragment &frag){
  sbndaq::CAENV1730Fragment bb(frag);
  auto const* md = bb.Metadata();
  sbndaq::CAENV1730Event const* event_ptr = bb.Event();
  sbndaq::CAENV1730EventHeader header = event_ptr->Header;
  
  uint32_t ttt = header.triggerTimeTag*8; // downsampled trigger time tag; TTT points to end of wvfm w.r.t. pps

  size_t nChannels = md->nChannels;
  uint32_t ev_size_quad_bytes = header.eventSize;
  uint32_t evt_header_size_quad_bytes = sizeof(sbndaq::CAENV1730EventHeader)/sizeof(uint32_t);
  uint32_t data_size_double_bytes = 2*(ev_size_quad_bytes - evt_header_size_quad_bytes);
  auto wvfm_length = data_size_double_bytes/nChannels;

  uint32_t wvfm_start = ttt - wvfm_length*ticks_to_us*1e3; // ns, start of waveform w.r.t. pps
  return wvfm_start;
}

uint32_t sbnd::trigger::pmtSoftwareTriggerProducer::getTriggerTime(const artdaq::Fragment &frag){
  uint32_t timestamp = frag.timestamp()%uint(1e9);
  sbndaq::CAENV1730Fragment bb(frag);
  auto const* md = bb.Metadata();
  sbndaq::CAENV1730Event const* event_ptr = bb.Event();
  sbndaq::CAENV1730EventHeader header = event_ptr->Header;
  
  uint32_t ttt = header.triggerTimeTag*8;
  size_t nChannels = md->nChannels;
  uint32_t ev_size_quad_bytes = header.eventSize;
  uint32_t evt_header_size_quad_bytes = sizeof(sbndaq::CAENV1730EventHeader)/sizeof(uint32_t);
  uint32_t data_size_double_bytes = 2*(ev_size_quad_bytes - evt_header_size_quad_bytes);
  uint32_t wvfm_length = data_size_double_bytes/nChannels;

  uint32_t trigger_ts; 
  if (timestamp!=ttt){
    // this assumes that if timestamp != ttt... then the time tag shift is enabled in the caen configuration
    // use it to set the correct postpercent
    trigger_ts = timestamp;
    auto updated_postpercent = float(ttt - timestamp)/(wvfm_length*ticks_to_us*1e3); // ns over ns
    // in case we're looking at an extended fragment, make sure the value is sensible!
    if ( updated_postpercent > 0 && updated_postpercent < 1.0){
      if (fWindowStart==(1-fWvfmPostPercent)) fWindowStart = 1-updated_postpercent;
      fWvfmPostPercent = updated_postpercent; 
    }  
  }
  else if (timestamp==ttt){    
    // if the time tag shift is not enabled in the caen configuration
    // use the postpercent fcl parameter
    trigger_ts = ttt - 2*wvfm_length*fWvfmPostPercent;
  }
  return trigger_ts;
}

uint32_t sbnd::trigger::pmtSoftwareTriggerProducer::getLength(const artdaq::Fragment &frag){
  sbndaq::CAENV1730Fragment bb(frag);
  auto const* md = bb.Metadata();
  size_t nChannels = md->nChannels;

  sbndaq::CAENV1730Event const* event_ptr = bb.Event();
  sbndaq::CAENV1730EventHeader header = event_ptr->Header;
  uint32_t ev_size_quad_bytes = header.eventSize;
  uint32_t evt_header_size_quad_bytes = sizeof(sbndaq::CAENV1730EventHeader)/sizeof(uint32_t);
  uint32_t data_size_double_bytes = 2*(ev_size_quad_bytes - evt_header_size_quad_bytes);
  uint32_t wvfm_length = data_size_double_bytes/nChannels;

  return wvfm_length;
}


float sbnd::trigger::pmtSoftwareTriggerProducer::estimateBaseline(std::vector<uint32_t> wvfm){
  // use a copy of 'wvfm' because nth_element might do something weird!
  const auto median_it = wvfm.begin() + wvfm.size() / 2;
  std::nth_element(wvfm.begin(), median_it , wvfm.end());
  auto median = *median_it;
  return median;
}

float sbnd::trigger::pmtSoftwareTriggerProducer::estimateBaseline(std::vector<uint16_t> wvfm){
  // use a copy of 'wvfm' because nth_element might do something weird!
  const auto median_it = wvfm.begin() + wvfm.size() / 2;
  std::nth_element(wvfm.begin(), median_it , wvfm.end());
  auto median = *median_it;
  return median;
}

DEFINE_ART_MODULE(sbnd::trigger::pmtSoftwareTriggerProducer)
