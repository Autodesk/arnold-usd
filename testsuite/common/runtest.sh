#!/bin/sh

#
# the command line we're executing should be in the
# environment variable, ARN_COMMANDLINE
#

#
# clean up leftover crap in this directory:
#    core files
#    valgrind-output files
#

#
# valgrind output/summary files
#
VALOUTFILE=valgrind.output
VALSUMFILE=valgrind.summary
#
# let's make sure they don't exist
#
rm $VALOUTFILE 2> /dev/null
rm $VALSUMFILE 2> /dev/null
#
# declare VALGRIND and flags
# (to make parsing easier, we're using --log-file-exactly
#  which will put all the thread-output in the same
#  file--this might make troubleshooting by hand a little more
#  difficult)
#
VALGRIND=/net/soft_scratch/apps/arnold/tools/rhel40m64/bin/valgrind 
VALGRIND_FLAGS="--tool=memcheck \
                --num-callers=15 \
                --leak-check=yes  \
                --show-reachable=yes \
                --log-file-exactly=$VALOUTFILE \
                --suppressions=../common/vlgrnd.supp \
                --xml=no"

#
# get timelimit (from file,
# if it exists)
#
timelimitfile=`pwd`/tlimit
if [ -e "$timelimitfile" ]; then
   timelimit=`cat $timelimitfile`
fi

#
# run the test and get the arnold status -- only
# run with timelimits if we're not valgrind'ing
#
if [ "$ARN_USE_VALGRIND" = "1" ]; then
   echo Command line: > $VALSUMFILE
   echo "   " $VALGRIND $VALGRIND_FLAGS $ARN_COMMANDLINE >> $VALSUMFILE
   $VALGRIND $VALGRIND_FLAGS $ARN_COMMANDLINE
else
 # only run with timelimit if the limit file exists
 # in that test directory, otherwise all the tests
 # run slowly (presumably because we have to load in
 # perl and then interpret the 'timelimit' script)
 if [ -e "$timelimitfile" ]; then
   ../timelimit $timelimit $ARN_COMMANDLINE
 else
   $ARN_COMMANDLINE
 fi 
fi
arn_status=$?
if [ "$arn_status" -eq "64" ]; then
   arn_status=1 # timeout
else
   if [ "$arn_status" -eq "65" ]; then
      arn_status=2 # keyboard interrupt
   else
      if [ "$arn_status" -ge "1" ]; then
         if [ "$arn_status" -le "15" ]; then
            arn_status=4 # arnold error
         fi
      else
         if [ "$arn_status" -eq "0" ]; then
            arn_status=0 # success
         fi
      fi
   fi
fi
#
# now get the valgrind status
#
if [ -e $VALOUTFILE ]; then
#export AWKSUMMARY=`awk -f ../common/vlgrnd.awk $VALOUTFILE`
   awk -f ../common/vlgrnd.awk $VALOUTFILE >> $VALSUMFILE
   vlg_status=$?
else
   export AWKSUMMARY=''
   vlg_status=0
fi
#
# figure out what we're returning to the calling
# shell.  We'll put the arnold status in the low 4-bits
# and the valgrind status in the high 4-bits
#
tmpval=`expr $vlg_status \* 16`
exit `expr $arn_status + $tmpval`

