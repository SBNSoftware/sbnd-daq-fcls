#Run this command when you have made changes to standard and need those changes to propogate to all of the different trigger configurations


index=1
for config in list; do
    conftool.py importConfiguration ${config}${index}-
    #git commit -me "Added new configurations: " 
done
