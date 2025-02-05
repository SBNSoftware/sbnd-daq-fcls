#define TNAME "TriggerBoard"

#include <stdexcept>

#include "json/json.h"
#include "json/reader.h"

#include "sbndaq-artdaq/Generators/SBND/TriggerBoardReader.hh"
#include "artdaq/Generators/GeneratorMacros.hh"
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "trace.h"
#include "artdaq/DAQdata/Globals.hh"
#include "sbndaq-artdaq-core/Overlays/FragmentType.hh"

#include "sbndaq-artdaq-core/Overlays/SBND/PTB_content.h"
#include "sbndaq-artdaq-core/Overlays/SBND/PTBFragment.hh"

#include "artdaq-core/Utilities/SimpleLookupPolicy.hh"

#include "canvas/Utilities/Exception.h"
#include "cetlib_except/exception.h"
#include "fhiclcpp/ParameterSet.h"

#include <fstream>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <string>
#include <bitset>

#include <unistd.h>
#include <thread>

int numGates[5] ={0,0,0,0,0}; //Index is numGate for different HLT--> numGates[0] = HLT 1, numGates[1] = HLT 2, numGates[2] = HLT 3, numGates[3] = HLT 4, numGates[4] = HLT 6
uint64_t prevHLT01TS =0;
uint64_t prevHLT02TS =0;
uint64_t prevHLT03TS =0;
uint64_t prevHLT04TS =0;
uint64_t prevHLT06TS =0;

bool isVerbose;

sbndaq::TriggerBoardReader::TriggerBoardReader(fhicl::ParameterSet const & ps)
  :
  CommandableFragmentGenerator(ps),
  _run_controller(), _receiver(), _timeout(), _stop_requested(false), _error_state(false),
  _fragment_buffer( ps.get<unsigned int>( "data_buffer_depth_fragments", 1000 ) ),
  throw_exception_(ps.get<bool>("throw_exception",true) )
{

  //get options for printing information (isVerbose)
  isVerbose = ps.get<bool>("isVerbose", false);
	
  //get board address and control port from the fhicl file
  const unsigned int control_port = ps.get<uint16_t>("control_port", 8991 ) ;
  const std::string address = ps.get<std::string>( "board_address", "sbnd-ptbmk2-01" );
  TLOG_INFO(TNAME) << "PTB at " << control_port << "@" << address << TLOG_ENDL;

  // get options for fragment creation
  _group_size = ps.get<unsigned int>( "group_size", 1 ) ;
  TLOG_INFO(TNAME) << "Creating fragments with " << _group_size << TLOG_ENDL;

  unsigned int word_buffer_size = ps.get<unsigned int>( "word_buffer_size", 5000 ) ;
  _max_words_per_frag = word_buffer_size / 5 * 4 ;  // 80 % of the buffer size


  //build the ptb_controller
  _run_controller.reset( new PTB_Controller( address, control_port ) ) ;


  // prepare the receiver

  // get the json configuration string
  std::stringstream json_stream( ps.get<std::string>( "board_config" ) ) ;

  Json::Value jblob;
  json_stream >> jblob ;

  // get the receiver port from the json
  const unsigned int receiver_port = jblob["ctb"]["sockets"]["receiver"]["port"].asUInt() ;

  _rollover = jblob["ctb"]["sockets"]["receiver"]["rollover"].asUInt() ;

  const unsigned int timeout_scaling = ps.get<uint16_t>("receiver_timeout_scaling", 2 ) ;

  const unsigned int timeout = _rollover / 50 / timeout_scaling; //microseconds
  //                                      |-> this is the board clock frequency which is 50 MHz

  _timeout = std::chrono::microseconds( timeout ) ;

  //build the ptb receiver and set its connecting port
  _receiver.reset( new PTB_Receiver( receiver_port, timeout, word_buffer_size ) ) ;

  // if necessary, set the calibration stream
  if ( ps.has_key( "calibration_stream_output") ) {
    _has_calibration_stream = true ; 
    _calibration_dir = ps.get<std::string>( "calibration_stream_output") ;
    _calibration_update = std::chrono::minutes( ps.get<unsigned int>( "calibration_update", 5 )  ) ; 
  }

  if ( ps.has_key( "run_trigger_output") ) {
    _has_run_trigger_report = true ; 
    _run_trigger_dir = ps.get<std::string>( "run_trigger_output") ;
    if ( _run_trigger_dir.back() != '/' ) _run_trigger_dir += '/' ;

  }

  // Receive hostname or IP address from config
  boost::asio::io_service io_service;
  boost::asio::ip::tcp::resolver resolver(io_service);  

  const std::string receiver_address = ps.get<std::string>("boardreader_address");
  // require the private hostname
  std::string priv ("-daq");

  TLOG_INFO(TNAME) << "Host name is " << receiver_address << TLOG_ENDL;
  std::for_each(resolver.resolve({receiver_address, ""}), {}, [](const auto& re) {
      TLOG_INFO(TNAME) << "Host IP address: " << re.endpoint().address() << TLOG_ENDL;
  });

  jblob["ctb"]["sockets"]["receiver"]["host"] = receiver_address ;

  TLOG_INFO(TNAME) << "Board packages recieved at " << receiver_address << ", port:" << receiver_port << TLOG_ENDL;

  // create the json string
  json_stream.str("");
  json_stream.clear() ;

  json_stream << jblob ;
  std::string config = json_stream.str() ;

  // send the configuration
  bool config_status = _run_controller -> send_config( config ) ;

 if ( ! config_status ) {
   TLOG_ERROR(TNAME) << "PTB failed to configure" << TLOG_ENDL;

   if ( throw_exception_ ) {
     throw std::runtime_error("PTB failed to configure") ;
    }
  }

 // metric parameters configuration
 _metric_TS_max = ps.get<unsigned int>( "metric_TS_interval", (unsigned int) (1. * TriggerBoardReader::PTB_Clock() / _rollover )  ) ; // number of TS words in which integrate the frequencies of the words. Default value the number of TS word in a second

 _cherenkov_coincidence = ps.get<unsigned int>( "cherenkov_coincidence", 25  ) ;

}

sbndaq::TriggerBoardReader::~TriggerBoardReader() {

  stop() ;

}

bool sbndaq::TriggerBoardReader::getNext_(artdaq::FragmentPtrs & frags) {

  if ( should_stop() ) {
    return false;
  }

  if ( _error_state.load() ||  _receiver -> ErrorState() ) {
    if ( throw_exception_ ) {
      TLOG_ERROR(TNAME) << "PTB in error state, shutting down" << TLOG_ENDL;
      stop() ;
      throw std::runtime_error("PTB sent a feedback word") ;
    }
  }

  std::size_t n_fragments = _fragment_buffer.read_available() ;
  if ( n_fragments == 0 ) {
    TLOG( 20, "TriggerBoardReader") << "getNext_ returns as no fragments are available" << std::endl ;
    return ! _error_state.load() ;
  }

  long unsigned int sent_bytes = 0 ;

  artdaq::Fragment* temp_frag = nullptr ;

  for ( std::size_t i = 0; i < n_fragments ; ++i ) {

    _fragment_buffer.pop( temp_frag ) ;

    if ( ! temp_frag ) continue ;

    sent_bytes += temp_frag -> dataSizeBytes() ;

    frags.emplace_back( temp_frag ) ;

  }

  TLOG( 20, "TriggerBoardReader") << "Sending " << frags.size() <<  " fragments" << std::endl ;

  if( artdaq::Globals::metricMan_ ) {
    artdaq::Globals::metricMan_->sendMetric("PTB_Fragments_Sent", (double) frags.size(), "Fragments", 11, artdaq::MetricMode::Average) ;
    artdaq::Globals::metricMan_->sendMetric("PTB_Bytes_Sent",     (double) sent_bytes,   "Bytes",     11, artdaq::MetricMode::Average) ;
  }

  return ! _error_state.load() ;
}


void print_fragment_words(artdaq::Fragment& frag, size_t wordNum, size_t bitsPerWord ) {
  
  const __uint8_t* data_ptr = reinterpret_cast<const __uint8_t*>(frag.dataBegin());
  size_t wordCount = frag.dataSizeBytes() / (bitsPerWord / 8);
  for (size_t w = 0; w < wordCount; w++) {
    // Check if the current word index matches the specified word index
    if (w == wordNum) {
        // Print the bits for the specified 128-bit word
      for (int i = (bitsPerWord-1); i >= 0; --i) {
            // Calculate the byte index and bit index
	size_t byteIndex = w * (bitsPerWord / 8) + (i / 8); // Each 128-bit word is 16 bytes
	int bitIndex = (i % 8); // Get the correct bit position in the byte
	// Print the bit
	std::cout << static_cast<int>((data_ptr[byteIndex] >> bitIndex) & 1);
	if (i % 8 == 0) {
	  std::cout << " "; 
	}
      }
      std::cout << std::endl; // New line after printing the word
    }
  }      
}


artdaq::Fragment* sbndaq::TriggerBoardReader::CreateFragment() {
//sbndaq::Fragment* sbndaq::TriggerBoardReader::CreateFragment() {

  static ptb::content::word::word_t temp_word ;
  static ptb::content::word::PTBBoardReaderWord_t temp_PTBBR_word ;

  static const constexpr std::size_t word_bytes = sizeof( ptb::content::word::word_t ) ;
  static const std::size_t word_BR_bytes = sizeof(ptb::content::word::PTBBoardReaderWord_t );

  const std::size_t n_words = _receiver -> Buffer().read_available() ;

  std::size_t initial_bytes = n_words * word_bytes ;
  std::size_t initial_BR_bytes = n_words * word_BR_bytes ;

  artdaq::Fragment::timestamp_t timestamp = artdaq::Fragment::InvalidTimestamp ;
  bool has_TS = false ;

  if ( _has_last_TS ) {
    has_TS = true ;
    timestamp = _last_timestamp ;
  }

  //From PR 155
  //borrowed from the CAEN code
  boost::posix_time::ptime fTimeServer = boost::posix_time::microsec_clock::universal_time();
 //get the server time so we can use the second part
  boost::posix_time::ptime fTimeEpoch = boost::posix_time::ptime(boost::gregorian::date(1970,1,1));
  boost::posix_time::time_duration fTimeDiff= fTimeServer-fTimeEpoch;//current time since last epoch
  uint64_t fServerStartLoopTime=fTimeDiff.total_nanoseconds();//put it in ns
  uint64_t fServerStartLoopTimeNS=fServerStartLoopTime%(1000000000);//take just the ns (subsecond) part

  unsigned int word_counter = 0 ;
  unsigned int group_counter = 0 ;
  

  //artdaq::Fragment* fragptr = artdaq::Fragment::FragmentBytes( initial_bytes ).release() ;
  artdaq::Fragment* BRfragptr = artdaq::Fragment::FragmentBytes( initial_BR_bytes ).release() ;

  for ( word_counter = 0 ; word_counter < n_words ; ) {

    _receiver -> Buffer().pop( temp_word ) ;

    temp_PTBBR_word.setPrevTimestamp(0); //set default previous timestamp for exposure accounting

    //memcpy( fragptr->dataBeginBytes() + word_counter * word_bytes, & temp_word, word_bytes ) ;
    memcpy( BRfragptr->dataBeginBytes() + word_counter * word_BR_bytes+8, & temp_word, word_bytes ) ; //save in the correct location but don't save the last 64b timestamp yet 

    ++word_counter ;
    ++_metric_Word_counter ;

    if ( _close_to_good_part ) {
      if ( temp_word.timestamp_ns() != 0 ) {
	update_cherenkov_counter( temp_word.timestamp_ns() ) ;
      }
    }

    if ( _is_beam_spill ) {
    
      if ( temp_word.timestamp_ns() != 0 ) {
	
	if ( temp_word.timestamp_ns() > _spill_start + _spill_width ) {
	  TLOG( 20, "TriggerBoardReader") << "End of a beam spill at: " <<  temp_word.timestamp_ns() << " => Started at " << _spill_start << std::endl ;
	  _is_beam_spill = false ; 
	  
	  // publish the dedicated metrics
	  if ( artdaq::Globals::metricMan_ ) {
	    
	    artdaq::Globals::metricMan_->sendMetric("PTB_Spill_H0L0", (double) _metric_spill_h0l0_counter, "Particles", 11, artdaq::MetricMode::Accumulate ) ;
	    artdaq::Globals::metricMan_->sendMetric("PTB_Spill_H0L1", (double) _metric_spill_h0l1_counter, "Particles", 11, artdaq::MetricMode::Accumulate ) ;
	    artdaq::Globals::metricMan_->sendMetric("PTB_Spill_H1L0", (double) _metric_spill_h1l0_counter, "Particles", 11, artdaq::MetricMode::Accumulate ) ;
	    artdaq::Globals::metricMan_->sendMetric("PTB_Spill_H1L1", (double) _metric_spill_h1l1_counter, "Particles", 11, artdaq::MetricMode::Accumulate ) ;
	    
	  } // if there is a metric manager      
	  
	}  // if the beam is over
	
      }  // if valid timestamp
      
    } // if we were in a beam spill
    
    if ( PTB_Receiver::IsTSWord( temp_word ) ) {
      
      --(_receiver -> N_TS_Words()) ;
      ++group_counter ;
      
      if ( temp_word.timestamp_ns() != 0 ) {
	_last_timestamp = temp_word.timestamp_ns() ;
	_has_last_TS = true ;
	
	if ( ! has_TS ) {
	  has_TS = true ;
	  timestamp = _last_timestamp ;
	}
      }

      // increment _metric TS counters
      ++ _metric_TS_counter ;
      
      if ( _metric_TS_counter == _metric_TS_max ) {
	
	if( artdaq::Globals::metricMan_ ) {
	  // evaluate word rates
	  double word_rate = _metric_Word_counter * TriggerBoardReader::PTB_Clock() / _metric_TS_max / _rollover ;
	  double hlt_rate  = _metric_HLT_counter * TriggerBoardReader::PTB_Clock() / _metric_TS_max / _rollover ;
	  double llt_rate  = _metric_LLT_counter * TriggerBoardReader::PTB_Clock() / _metric_TS_max / _rollover ;

	  double beam_rate = _metric_beam_trigger_counter * TriggerBoardReader::PTB_Clock() / _metric_TS_max / _rollover ;

	  double cs_rate   = _metric_CS_counter  * TriggerBoardReader::PTB_Clock() / _metric_TS_max / _rollover ;

	  // publish metrics
	  artdaq::Globals::metricMan_->sendMetric("PTB_Word_rate", word_rate, "Hz", 11, artdaq::MetricMode::Average) ;

	  artdaq::Globals::metricMan_->sendMetric("PTB_HLT_rate", hlt_rate, "Hz", 11, artdaq::MetricMode::Average) ;
	  artdaq::Globals::metricMan_->sendMetric("PTB_LLT_rate", llt_rate, "Hz", 11, artdaq::MetricMode::Average) ;

	  artdaq::Globals::metricMan_->sendMetric("PTB_Beam_Trig_rate", beam_rate, "Hz", 11, artdaq::MetricMode::Average) ;

	  artdaq::Globals::metricMan_->sendMetric("PTB_CS_rate",  cs_rate,  "Hz", 20, artdaq::MetricMode::Average) ;

	  for ( unsigned short i = 0 ; i < _metric_HLT_names.size() ; ++i ) {
	    double temp_rate = _metric_HLT_counters[i] * TriggerBoardReader::PTB_Clock() / _metric_TS_max / _rollover ;
            if(i < 5 || i >= 29 || i == 20 || i == 21) { //is event or flash trigger (excl calibration) or crt reset
	      artdaq::Globals::metricMan_->sendMetric( _metric_HLT_names[i], temp_rate,  "Hz", 11, artdaq::MetricMode::Average) ;
            }
            else if(i == 5) { //calibration
	      artdaq::Globals::metricMan_->sendMetric( _metric_HLT_names[i], temp_rate,  "Hz", 12, artdaq::MetricMode::Average) ;
            }
            else { //spare level is 20
	      artdaq::Globals::metricMan_->sendMetric( _metric_HLT_names[i], temp_rate,  "Hz", 20, artdaq::MetricMode::Average) ;
            }
	  }
	  for ( unsigned short i = 0 ; i < _metric_LLT_names.size() ; ++i ) {
	    double temp_rate = _metric_LLT_counters[i] * TriggerBoardReader::PTB_Clock() / _metric_TS_max / _rollover ;
	    artdaq::Globals::metricMan_->sendMetric( _metric_LLT_names[i], temp_rate,  "Hz", 11, artdaq::MetricMode::Average) ;
	  }


	}  // if there is a metric manager

	// transfer HLT counters to run counters
	//<--_run_gool_part_counter += _metric_good_particle_counter ;
	_run_HLT_counter += _metric_HLT_counter ; 
	_run_LLT_counter += _metric_LLT_counter ; 
	
	// reset counters
	_metric_TS_counter =
	  _metric_Word_counter =
	  _metric_HLT_counter =
	  _metric_LLT_counter =
	  _metric_beam_trigger_counter =
	  _metric_CS_counter  = 0 ;

	for ( unsigned short i = 0 ; i < _metric_HLT_names.size() ; ++i ) {
	  _run_HLT_counters[i] += _metric_HLT_counters[i] ;
	  _metric_HLT_counters[i] = 0 ;
	}
	for ( unsigned short i = 0 ; i < _metric_LLT_names.size() ; ++i ) {
	  _run_LLT_counters[i] += _metric_LLT_counters[i] ;
	  _metric_LLT_counters[i] = 0 ;
	}

      }  // if it is necessary to publish the metric

      if ( _group_size > 0 ) {
	if ( group_counter >= _group_size ) {
	  TLOG( 20, "TriggerBoardReader") << "Final group_counter " << group_counter << std::endl ;
	  break ;
	}
      }

    } // if this was a TS word


    else if ( temp_word.word_type == ptb::content::word::t_lt ) {
      ++ _metric_LLT_counter ;

      const ptb::content::word::trigger_t * t = reinterpret_cast<const ptb::content::word::trigger_t *>( & temp_word  ) ;

      if (t ->IsTrigger(30) ) {
	numGates[0]++;
	numGates[1]++;
	numGates[4]++;
	TRACE(TLVL_INFO, "Beam acceptance window (LLT 30) started at time: %lu", t->timestamp);
	if (isVerbose) TRACE(TLVL_INFO, "LLT 30 occurred at timestamp: %lu and incrementing number of gates by 1 so numGates[0]: %d , numGates[1]: %d , numGates[4]: %d ", t->timestamp, numGates[0], numGates[1], numGates[4]); 
      }
      
      std::set<unsigned short> trigs = t -> Triggers(32) ;
      for ( auto it = trigs.begin(); it != trigs.end() ; ++it ) {
	++ _metric_LLT_counters[*it] ;
      }
      if ( ! _is_beam_spill ) {

	if ( t -> IsTrigger(6) )  {

	  TLOG( 20, "TriggerBoardReader") << "Start of a beam spill at: " <<  t -> timestamp << std::endl ;

	  _is_beam_spill = true ; 
	  _spill_start = t -> timestamp ;
	
	  // so reset spill counters 
	  _metric_spill_h0l0_counter = 
	    _metric_spill_h0l1_counter = 
	    _metric_spill_h1l0_counter = 
	    _metric_spill_h1l1_counter = 0 ;
	  
	}  // is trigger 6 
      }  // we were not in beam spill
      
    }  // if this was a LLT

    else if ( temp_word.word_type == ptb::content::word::t_gt ) {
      ++ _metric_HLT_counter ;

      

      //const ptb::content::word::trigger_t * t = reinterpret_cast<const ptb::content::word::trigger_t *>( & temp_word  ) ;
      ptb::content::word::trigger_t * t = reinterpret_cast<ptb::content::word::trigger_t *>( & temp_word  ) ;
      
      // Incrementing gate counter for off-beam gates using HLT 27, which should be issued when there is an off-beam gate that has *not* been inhibited by the beam protection logic
      if (t -> IsTrigger(27)){
	numGates[2]++;
	numGates[3]++;
	if (isVerbose) TRACE(TLVL_INFO, "HLT 27 occurred at timestamp: %lu and incrementing number of gates by 1 so numGates[2]: %d , numGates[3]: %d", t->timestamp, numGates[2], numGates[3]);
      }

      //Setting the previous HLT timestamp and adding it to the new HLT word
      if (t -> IsTrigger(1)){
	t -> setGateCounter(numGates[0]);      //Setting the gate Counters for HLT 1
	numGates[0]=0; //reset counter
	temp_PTBBR_word.setPrevTimestamp(prevHLT01TS);
	//std::cout << "Previous HLT 1 TS: " << prevHLT01TS << std::endl; 
	prevHLT01TS = t->timestamp;
      }

      if (t -> IsTrigger(2)){
	t -> setGateCounter(numGates[1]);      //Setting the gate Counters for HLT 2
	numGates[1]=0; //reset counter
	temp_PTBBR_word.setPrevTimestamp(prevHLT02TS);
	//std::cout << "Previous HLT 2 TS: " << prevHLT02TS << std::endl; 
	prevHLT02TS = t->timestamp;
      }
      
      if (t -> IsTrigger(3)){
	t -> setGateCounter(numGates[2]);      //Setting the gate Counters for HLT 3
	numGates[2]=0; //reset counter
	temp_PTBBR_word.setPrevTimestamp(prevHLT03TS);
	//std::cout << "Previous HLT 3 TS: " << prevHLT03TS << std::endl; 
	prevHLT03TS = t->timestamp;
      }
      
      if (t -> IsTrigger(4)){
	t -> setGateCounter(numGates[3]);
	numGates[3]=0;
	temp_PTBBR_word.setPrevTimestamp(prevHLT04TS);
	//std::cout << "Previous HLT 4 TS: " << prevHLT04TS << std::endl; 
	prevHLT04TS = t->timestamp;
      }

      if(t -> IsTrigger(6)){
	t -> setGateCounter(numGates[4]);      //Setting the gate Counters for HLT 6
	numGates[4]=0; //reset counter
	temp_PTBBR_word.setPrevTimestamp(prevHLT06TS);
	prevHLT06TS = t->timestamp;
      }
      
      //Adding the gate count to the HLT words
      memcpy( BRfragptr->dataBeginBytes() + (word_counter-1) * word_BR_bytes + 8, &temp_word, word_bytes );    
      //memcpy( fragptr->dataBeginBytes() + (word_counter-1) * word_bytes, &temp_word, word_bytes ); 

      if ( t -> trigger_word & 0xEE )  // request at least a trigger not cosmic trigger nor random triggers
	++ _metric_beam_trigger_counter ;    // count beam related HLT

      std::set<unsigned short> trigs = t -> Triggers(32) ;
      for ( auto it = trigs.begin(); it != trigs.end() ; ++it ) {
	++ _metric_HLT_counters[*it] ;
      }
    }  // if this was a HLT

    else if (  temp_word.word_type == ptb::content::word::t_ch )
      ++ _metric_CS_counter ;

    else if ( PTB_Receiver::IsFeedbackWord( temp_word ) ) {
      TLOG_ERROR(TNAME) << "PTB issued a feedback word" << TLOG_ENDL;
      const ptb::content::word::feedback_t * f = reinterpret_cast<const ptb::content::word::feedback_t *>( & temp_word  ) ;
      TLOG_ERROR(TNAME) << "PTB issued a feedback word!"<< TLOG_ENDL;
      TLOG_ERROR(TNAME) << "Feedback word Code: "       << f -> code      << TLOG_ENDL;
      TLOG_ERROR(TNAME) << "Feedback word Source: "     << f -> source    << TLOG_ENDL;
      TLOG_ERROR(TNAME) << "Feedback word Payload: "    << f -> payload   << TLOG_ENDL;
      TLOG_ERROR(TNAME) << "Feedback word Timestamp: "  << f -> timestamp << TLOG_ENDL;
      _error_state.store( true ) ;
    }

    //add 64b to all of the words
    uint64_t BRprevTS = temp_PTBBR_word.prevTS;
    memcpy( BRfragptr->dataBeginBytes() + (word_counter-1) * word_BR_bytes, &BRprevTS, 8 );  

  }//End of word counter


  //Make a hybrid timestamp that uses the PTB server time for the second part of the ptb fragment timestamp and info from the PTB itself for the sub-second part

  long timestampPTB_ns= timestamp%(1000000000);//take just hte ns part of the timestamp from the PTB
  artdaq::Fragment::timestamp_t timestampPTB = artdaq::Fragment::InvalidTimestamp ;
  timestampPTB=timestamp;
  // Scheme borrowed from what Antoni developed for CRT.                                                          
  // See https://sbn-docdb.fnal.gov/cgi-bin/private/DisplayMeeting?sessionid=7783

  timestamp= fServerStartLoopTime - fServerStartLoopTimeNS + timestampPTB_ns
    + (timestampPTB_ns - (long)fServerStartLoopTimeNS < -500000000) * 1000000000
    - (timestampPTB_ns - (long)fServerStartLoopTimeNS >  500000000) * 1000000000;  

  //more code from the CAENs to check for time drifts
  artdaq::Fragment::timestamp_t ts_now; //server time
  {
    using namespace boost::gregorian;
    using namespace boost::posix_time;

    ptime t_now(microsec_clock::universal_time());
    ptime time_t_epoch(date(1970,1,1));
    time_duration diff = t_now - time_t_epoch;

    ts_now = diff.total_nanoseconds();
  }


  //timestamp=the timestamp from the fragment I think
  if( timestamp>(ts_now+1e6) ){
    TLOG(TLVL_WARNING) << "Fragment assigned timestamp is after timestamp from fragment creation! Something funky is happening with the clocks/timing."
		       << "ts_frag (PTB hybrid timestamp) - ts_now (Server NTP timestamp) = " << timestamp - ts_now << " ns! ts_frag= "<<timestamp<<"  ts_now= "<<ts_now
      <<"\n ptb_self_timestamp = "<<timestampPTB<<"  ptb_self_timestamp-hybrid_ts=  "<< timestampPTB-timestamp
		       << TLOG_ENDL;

  }
  else if( ts_now>timestamp+5e9 ){
    TLOG(TLVL_ERROR) << "Fragment being packged more than 5 seconds after timestamp: "
		     << "ts_now - ts_frag = " << (long long)ts_now-(long long)timestamp << " ns!"
		     << TLOG_ENDL;
  }
  else if( ts_now>timestamp+1e9 ){
    TLOG(TLVL_WARNING) << "Fragment being packged more than 1 second after timestamp: "
		       << "ts_now - ts_frag = " << ts_now-timestamp << " ns!"
		       << TLOG_ENDL;
  }

  BRfragptr -> resizeBytes( word_counter * word_BR_bytes ) ;
  BRfragptr -> setUserType( detail::FragmentType::PTB ) ;
  BRfragptr -> setSequenceID( ev_counter_inc() ) ;
  BRfragptr -> setFragmentID( fragment_id() ) ; 
  BRfragptr -> setTimestamp( timestamp ) ;
  BRfragptr -> setMetadata(ptb::content::version);

  if (isVerbose) TRACE(TLVL_INFO, "Has Metadata: %d   Metadata: %d", BRfragptr ->hasMetadata(), *BRfragptr->metadata<int>() );
	
  //How to print out the metadata to check the versioning of PTB_content.h
  //std::cout << "Has Metadata: " << BRfragptr -> hasMetadata() << "   Metadata: " << *BRfragptr->metadata<int>() << std::endl; 

  TLOG( 20, "TriggerBoardReader") << "fragment created with TS " << timestamp << " containing " << word_counter << " words" << std::endl ;

  if ( ! has_TS ) {
    TLOG( 20, "TriggerBoardReader") << "Created fragment with invalid TimeStamp" << std::endl ;
  }

  return BRfragptr ;

}


int sbndaq::TriggerBoardReader::_FragmentGenerator() {

  int frag_counter = 0 ;
  

  artdaq::Fragment* temp_ptr ;

  while ( ! _stop_requested.load() ) {

    if ( CanCreateFragments() || NeedToEmptyBuffer() ) {

      temp_ptr = CreateFragment() ;

      while ( ! _fragment_buffer.push( temp_ptr ) ) {
	TLOG_WARNING(TNAME) << "Fragment Buffer full: does not accept more fragments" << TLOG_ENDL;
	std::this_thread::sleep_for( _timeout ) ;
      }

      ++frag_counter ;

    }
    else {
      std::this_thread::sleep_for( _timeout ) ;
    }

    if ( should_stop() ) break ;

  }

  return frag_counter ;
}



void sbndaq::TriggerBoardReader::start() {

  _stop_requested = false ;

  _is_beam_spill = false ; 

  _close_to_good_part = false ; 

  _hp_TSs.clear() ;
  _lp_TSs.clear() ;

  //<--_run_gool_part_counter = 0 ;
  _run_HLT_counter = 0 ;
  for ( unsigned int i = 0 ; i < _metric_HLT_names.size() ; ++i ) {
    _run_HLT_counters[i] = 0 ; 
  }
  _run_LLT_counter = 0 ;
  for ( unsigned int i = 0 ; i < _metric_LLT_names.size() ; ++i ) {
    _run_LLT_counters[i] = 0 ; 
  }

  if ( _has_calibration_stream ) {
    std::stringstream run;
    run << "run" << run_number();
    _receiver -> SetCalibrationStream( _calibration_dir ,
                                       _calibration_update, 
				       run.str() ) ;
  }

  _frag_future =  std::async( std::launch::async, & TriggerBoardReader::_FragmentGenerator,  this ) ;

  _receiver -> start() ;

  _run_controller -> send_start() ;

}

void sbndaq::TriggerBoardReader::stop() {

  _stop_requested = true ;

  //note the order here is crucial!
  // for efficiency reasons, the receiver is locking the stop until the
  // Board closes the connection.
  // This won't happen until you send the stop to the board first.
  // so, first, send the stop to the board, only then, stop the receiver and the fragment creator

  _run_controller -> send_stop() ;

  _receiver -> stop() ;

  if ( _frag_future.valid() ) {
    _frag_future.wait() ;
    TLOG_INFO(TNAME) << "Created " << _frag_future.get() << " fragments" << TLOG_ENDL;
  }

  ResetBuffer() ;

  store_run_trigger_counters( run_number() ) ; 

}


void sbndaq::TriggerBoardReader::ResetBuffer() {

  _fragment_buffer.consume_all( [](auto* p){ delete p ; } ) ;

}


void sbndaq::TriggerBoardReader::update_cherenkov_buffer( std::set<artdaq::Fragment::timestamp_t> & buffer ) {

  // remove old stuff
  
  while ( buffer.size() > 0 ) {
    if ( _latest_part_TS > _cherenkov_coincidence + *buffer.begin() ) {
      buffer.erase( buffer.begin() ) ;
    }
    else break ; 
  } 
 
}


void sbndaq::TriggerBoardReader::update_cherenkov_counter( const artdaq::Fragment::timestamp_t & latest ) {


  update_cherenkov_buffer( _hp_TSs ) ;
  update_cherenkov_buffer( _lp_TSs ) ;

  if ( latest > _cherenkov_coincidence + _latest_part_TS ) {
    _close_to_good_part = false ;

    if ( _hp_TSs.size() > 0 && _lp_TSs.size() > 0 ) {
      ++ _metric_spill_h1l1_counter ; 
    }
    else if ( _hp_TSs.size() == 0 && _lp_TSs.size() == 0 ) {
      ++ _metric_spill_h0l0_counter ;
    }
    else if ( _hp_TSs.size() > 0 && _lp_TSs.size() == 0 ) {
      ++ _metric_spill_h1l0_counter ;
    }
    else if ( _hp_TSs.size() == 0 && _lp_TSs.size() > 0 ) {
      ++ _metric_spill_h0l1_counter ;
    }

  }

  
}


bool sbndaq::TriggerBoardReader::store_run_trigger_counters( unsigned int run_number, const std::string & prefix ) const {

  if ( ! _has_run_trigger_report ) {
    return false ;
  }

  std::stringstream out_name ;
  out_name << _run_trigger_dir << prefix << "run_" << run_number << "_triggers.txt";
  std::ofstream out( out_name.str() ) ;

  out << "Total \t " << _run_HLT_counter << std::endl ;
  for ( unsigned int i = 0; i < _metric_HLT_names.size() ; ++i ) {
    out << "HLT " << i << " \t " << _run_HLT_counters[i] << std::endl ;
  }
  out << "Total \t " << _run_LLT_counter << std::endl ;
  for ( unsigned int i = 0; i < _metric_LLT_names.size() ; ++i ) {
    out << "LLT " << i << " \t " << _run_LLT_counters[i] << std::endl ;
  }


  return true ; 
}


// The following macro is defined in artdaq's GeneratorMacros.hh header
DEFINE_ARTDAQ_COMMANDABLE_GENERATOR(sbndaq::TriggerBoardReader)
