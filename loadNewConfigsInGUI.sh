#Run this command when you have made changes to standard and need those changes to propogate to all of the different trigger configurations


HOMEDIR=/tmp/artdaq-runcontrol-gui/db/
ALLCONFS=("fullCommTriggerMenuEW-NS2-00005" "fullCommTriggerMenuNS2-00005" "nsCrossingMuonFlash2-00005" "ewCrossingMuonFlash2-00005" "bnbZeroBias2-00005" "ewCrossingMuon2-00005" "nsCrossingMuon2-00005" "offbeamZeroBias2-00005" "offBeamZeroBiasFlash2-00005" "caenPUSHbnbZeroBias2-00005") #

#$(cat list_database_configs.txt)
for CONFIG in ${ALLCONFS[@]}; do
    echo "Making directory and exporting" $CONFIG
    mkdir $CONFIG
    cd $CONFIG
    conftool.py exportConfiguration ${CONFIG}
    #cd $HOMEDIR
done
echo "Done exporting"
