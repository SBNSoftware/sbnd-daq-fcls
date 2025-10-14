#Run this command when you have made changes to standard and need those changes to propogate to all of the different trigger configurations


index=1
HOMEDIR=$PWD
ALLCONFS=$(cat list_database_configs.txt)
DATE=$(date '+%Y-%m-%d')
for CONFIG in $ALLCONFS; do
    cd $CONFIG
    cp ../schema.fcl .
    rm *~
    echo "conftool.py importConfiguration ${CONFIG}${index}-"
    echo $PWD
    conftool.py importConfiguration ${CONFIG}- |& tee $HOMEDIR/logs/${CONFIG}.log
    cd $HOMEDIR
done
echo "Done importing"
ALLNAMES=""
for CONFIG in $ALLCONFS; do
    tail logs/${CONFIG}.log -n 2
    NAME=$(grep -nF -B 1 "None" logs/${CONFIG}.log | grep "New configuration" | awk -F ' ' '{print $3}')
    ALLNAMES+=$NAME+", "

done
read -p 'What changes were made? (git commit message): ' message
echo $message
read -p 'What are the new configurations/numbers? ' ALLNAMES
echo $ALLNAMES 
echo "${message}"
echo 'git commit -a -m "'${message}'
Associated database configurations: '${ALLNAMES}'"'
git commit -a -m "${message} Associated database configurations: ${ALLNAMES}"
