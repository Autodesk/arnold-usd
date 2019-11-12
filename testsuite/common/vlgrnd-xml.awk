#
# this is an awk program which will attempt to parse
# valgrind output
#
#    bytes_lost_def
#    bytes_lost_prob
#    bytes_lost
BEGIN {     } # do something here
/<leakedbytes>/,/<\/leakedbytes>/ { printf $1; }
END {  }

