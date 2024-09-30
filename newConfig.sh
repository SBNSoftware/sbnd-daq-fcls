if [ $# -eq 0 ] #check if there were no arguments
then                                                                                            
    echo "Please give the base configuration name as an argument"
    return 1
fi                                                                                              

#if standard directory doesn't exist give a message and return an error


echo "Creating config directory for ${1}"                                                                         
mkdir ${1}
mkdir ${1}/defaults

ALLFCLS=$(cat list_default_fcls.txt)
ALLEDITABLEFCLS=$(cat list_overrideable_fcls.txt)
PWD1=$PWD #remember where we started so we can return there when done
cd ${1}


for FNAME in $ALLFCLS; do 
    if [[ $FNAME == "feb"* ]]; then  
	 ln -s ../../standard/${FNAME}.fcl ${FNAME}.fcl #create softlinks to all the fcls in standard, don't make a skeleton for the febs to try and cut down the number of total fcls (database seems to dislike it)
    else
	ln -s ../../standard/${FNAME}.fcl defaults/${FNAME}.fcl #_default.fcl #create softlinks to all the fcls in standard 
	#(two up directories since its relative to the "<configname>/defaults directory not the current one)
	
	#INCLUDE='#include "'${FNAME}'_default.fcl"'
	#echo $INCLUDE > ${FNAME}.fcl  #create a skeleton file that just includes the _default.fcl soft link
	#echo "#======Place override parameters below=========" >>${FNAME}.fcl
    fi
done

for FNAME in $ALLEDITABLEFCLS; do
    ln -s ../../standard/${FNAME}.fcl defaults/${FNAME}_default.fcl #create softlinks to all the fcls in standard 
    INCLUDE='#include "'${FNAME}'_default.fcl"'
    echo $INCLUDE > ${FNAME}.fcl  #create a skeleton file that just includes the _default.fcl soft link
    echo "#======Place override parameters below=========" >>${FNAME}.fcl
done
    

mkdir febs
mv feb*.fcl febs #clean up the list of fcls a bit

cp ../ptb01PULL.fcl . #specially copy the ptb01PULL.fcl which is also just an include of ptb01PULL_default.fcl but has many possible trigger configs listed commented out.

cd $PWD1
cp standard/flags.fcl ${1}
cp standard/schema.fcl ${1}


echo "Directory created and filled with symbolic links to configs/standard and editable skeleton fcls"
