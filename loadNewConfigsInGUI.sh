#Run this command when you have made changes to standard and need those changes to propogate to all of the different trigger configurations


HOMEDIR=/tmp/artdaq-runcontrol-gui/db/
ALLCONFS=$1 #("fullCommTriggerMenuEW-NS8Hz2-00002" "fullCommTriggerMenuNS2-00006" "nsCrossingMuonFlash2-00006" "ewCrossingMuonFlash2-00006" "bnbZeroBias2-00006" "ewCrossingMuon2-00006" "nsCrossingMuon2-00006" "offbeamZeroBias2-00006" "offBeamZeroBiasFlash2-00006" "caenPUSHbnbZeroBias2-00006") ##$1 #

#$(cat list_database_configs.txt)
echo "ALLCONFS list:" ${ALLCONFS[@]}
cd $HOMDIR
for CONFIG in ${ALLCONFS[@]}; do
    echo "Making directory and exporting" $CONFIG
    mkdir $CONFIG
    cd $CONFIG
    conftool.py exportConfiguration ${CONFIG}
    cd $HOMEDIR
done
echo "Done exporting"
