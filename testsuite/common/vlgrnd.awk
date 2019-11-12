#
# this is an awk program which will attempt to parse
# valgrind output
#
#    bld -- bytes lost definitely
#    blp -- bytes lost probably
#    bytes_lost
#    numm -- number of mallocs
#    numf -- number of frees
BEGIN {     # do something here
}
/definitely lost:/ { bld += $4 * $7; }
/possibly lost:/   { blp += $4 * $7; }
/ERROR SUMMARY:/   { errs += $4; }
END { 
   printf("Valgrind summary:\n");
   printf("   Total number of errors: %d\n", errs);  
   printf("   Bytes definitely lost:  %d\n", bld);  
   printf("   Bytes possibly lost:    %d\n", blp);  

   if ( errs != 0 ) 
   {
      exit 1; 
   }
   else {
     if ( bld!=0 || blp!=0)
     {
        exit 2;
     }
   }


}

