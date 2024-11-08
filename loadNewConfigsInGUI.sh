#Run this command when you have made changes to standard and need those changes to propogate to all of the different trigger configurations


HOMEDIR=/tmp/artdaq-runcontrol-gui/db/
ALLCONFS=("fullCommTriggerMenuNS2-00001" "nsCrossingMuonFlash2-00001" "ewCrossingMuonFlash2-00001" "bnbZeroBias2-00001" "ewCrossingMuon2-00001" "nsCrossingMuon2-00001" "offbeamZeroBias2-00001" "offBeamZeroBiasFlash2-00001" "caenPUSHbnbZeroBias2-00001") #"fullCommTriggerMenuEW-NS2-00001"

#$(cat list_database_configs.txt)
for CONFIG in ${ALLCONFS[@]}; do
    echo "Making directory and exporting" $CONFIG
    mkdir $CONFIG
    cd $CONFIG
    conftool.py exportConfiguration ${CONFIG}
    #cd $HOMEDIR
done
echo "Done exporting"
