For fcl_configs_safe: If you can't do things because you don't have edit permissions in this folder, you can just check out a new copy of sbnd-daq-fcls from git 

===========================
list_default_fcls.txt
===========================
List of all of the fcls that are actually included in a standard run (i.e. does not include things like any of the PUSH configs for the pmts, or other non conventional configurations, though some of those fcls can be found in the `standard` directory.)

===========================
newConfig.sh
===========================
source newConfig.sh <configName>
 --configName = the base name of the configuration for the databse. (e.g. if its going to be use to make a config bnbZeroBias0-00022, the configName should be bnbZeroBias)

===========================
newIndividualFcl.sh
===========================
source newIndividualFcl.sh <fclName>
 --fclName = the base name of the fcl (minus .fcl) that you need to get added to each new configuration directory (e.g. feb055) 

