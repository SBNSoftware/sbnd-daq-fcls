# sbnd-daq-fcls
Collection of SBND's DAQ fcls for different run configurations 

## Using `standard` for direct running out of a Dev area 
If you are trying to directly use a copy of `standard` to run out of a personal dev area, you need to make a change to uncomment this line of `ptb01PULL.fcl` (as well as including the sacred config you want to use)
`daq.fragment_receiver.board_config: @local::board_config`

## Bash scripts
### newConfig.sh
`source newConfig.sh <configName>`
This will create a new directory with soft links to standard, and skeleton fcls that can override the default fcl settings.

### newIndividualFcl.sh
`source newIndividualFcl.sh <fclName>  `                                                          
 --fclName = the base name of the fcl (minus .fcl) that you need to get added to each new configuration directory (e.g. feb055) 
Will go add a soft link in the <config>/default directory of all present configuration directoires, and a skeleton fcl in each. You need to do this if you add an entirely new fcl to the `standard` directory, one common case would be when theres a new FEB for the CRTs. 

### setupdatabase.sh
Just sources the `~/DAQ_Prod/DAQ_SHIFTER_CURRENT/DAQInterface/setup_daqinterface.sh` script so that you can import configurations to the database. (Look here if you forget how to do that https://sbnsoftware.github.io/sbn_online_wiki/ConfigDB.html)

### importNewDatabaseConfigsAll.sh
`source importNewDatabaseConfigsAll.sh`
For permanent changes that have been pull requested already. If there was a change to 'standard' that needs to get propogated to all the frequently used trigger configs for the shifter, this will import all of those configuration to the database. Will import all of the configurations listed in `list_database_configs.txt`. Currently imports bnbZeroBias, offBeamZeroBias, nsCrossingMuon, and ewCrossingMuon. Also commits any local changes you have and asks for what the config numbers are you just added to the database.


