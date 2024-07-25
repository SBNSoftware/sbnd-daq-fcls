#Run this command when you have made changes to standard and need those changes to propogate to all of the different trigger configurations


index=1
HOMEDIR=$PWD
ALLCONFS=$(cat list_default_fcls.txt)
for CONFIG in ALLCONFS; do
    #conftool.py importConfiguration ${CONFIG}${index}-
    echo hi
done
read -p 'What changes were made? (git commit message): ' message
#echo $message
read -p 'What are the new configurations/numbers? ' configlist
#echo $configlist 
echo "${message}"
echo 'git commit -a -m "'${message}'
Associated database configurations: '${configlist}'"'
git commit -a -m "${message} Associated database configurations: ${configlist}"
