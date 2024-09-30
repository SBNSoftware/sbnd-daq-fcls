if [ $# -eq 0 ] #check if there were no arguments
then                                                                                            
    echo "Please give the new fcl name (without .fcl at the end) as an argument, and make sure it exists in standard already. eg. feb123"
    return 1
fi                                                                                              




ALLDIRS=$(ls -d */)
FNAME=$1
echo "Add fcl to list_default_fcls.txt"
echo $FNAME >> list_default_fcls.txt
PWD1=$PWD #remember where we started so we can return there when done

for DIR in $ALLDIRS; do 
    echo $DIR
    if [ $DIR == "standard/" ]
    then
	continue
    fi

    if [ $DIR == "logs/" ]
    then
	continue
    fi

    if [ $DIR == "makeOldConfigsInactive/" ]
    then
	continue
    fi

    cd $DIR
    if [[ $FNAME == "feb"* ]]; then       
	ln -s ../../standard/${FNAME}.fcl febs/${FNAME}.fcl #create softlinks to all the feb fcls directly
    else
	ln -s ../../standard/${FNAME}.fcl defaults/${FNAME}_default.fcl #create softlinks to all the fcls in standard 
	#(two up directories since its relative to the <configname>/defaults directory not the current one)
	
	INCLUDE='#include "'${FNAME}'_default.fcl"'
	echo $INCLUDE > ${FNAME}.fcl  #create a skeleton file that just includes the _default.fcl soft link
	echo "#======Place override parameters below=========" >>${FNAME}.fcl
    fi
    cd $PWD1
done

echo "Symbolic links to configs/standard and editable skeleton fcl added to each directory"


