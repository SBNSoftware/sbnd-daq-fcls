oldconfs=$(cat listoldconfigs.txt)
cd inactiveflag;
for CONFIG in $oldconfs; do
    conftool.py updateConfigurationFlags $CONFIG
done
cd -
