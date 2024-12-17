oldconfs=$(cat listoldconfigs.txt)
cd activeflag;
for CONFIG in $oldconfs; do
    conftool.py updateConfigurationFlags $CONFIG
done
cd -
