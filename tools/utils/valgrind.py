# vim: filetype=python
# Copyright 2022 Autodesk, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import os, shutil, string, platform, subprocess

## Used for XTML/HTML parsing/modifying
from .contrib.beautiful_soup import BeautifulStoneSoup

## Parses error records of a valgrind XML logfile  
def vg_errors(file):
   with open(file, 'r') as f:
      vgXML = BeautifulStoneSoup(f)
   vgErrorTags = vgXML.findAll('error')
   return vgErrorTags

## Counts the number of errors of a valgrind XML logfile 
def vg_count_errors(file):
   vgErrorTags = vg_errors(file)
   nMemLeaksDL = 0
   nMemLeaksPL = 0
   nMemLeaksIL = 0
   nMemLeaksSR = 0
   nMemErrors = 0
   for vgErrorTag in vgErrorTags:
      vgKind = str(vgErrorTag.kind.string)
      if vgKind == "Leak_DefinitelyLost":
         nMemLeaksDL += 1
      elif vgKind == "Leak_PossiblyLost":
         nMemLeaksPL += 1
      elif vgKind == "Leak_IndirectlyLost":
         nMemLeaksIL += 1
      elif vgKind == "Leak_StillReachable":
         nMemLeaksSR += 1
      else:
         nMemErrors += 1
   return nMemErrors, nMemLeaksDL, nMemLeaksPL, nMemLeaksIL, nMemLeaksSR

## Generate a suppresion record for valgrind for a given error
def vg_supp_string(error):
   error_stack = error.suppression.findAll('sframe')
   supp_string = '{<br/>&nbsp;&nbsp;&nbsp;%s<br/>&nbsp;&nbsp;&nbsp;%s<br/>' % (error.suppression.sname,error.suppression.skind) 
   for frame in error_stack:
       supp_string += '&nbsp;&nbsp;&nbsp;%s:%s<br/>' % ( frame.fun != None and 'fun' or 'obj', frame.fun != None and frame.fun or frame.obj )
   supp_string += '}'
   return supp_string

## Create a HTML table of a kind of valgrind error
def vg_write_table(f, errors, title, tag):
    if len(errors) > 0:
       f.write('''<tr><td height="75" bgcolor="#b9b9b9" colspan="7"><font face="Courier New" size="4"><b>&nbsp;%s&nbsp;<a name="%s"></a></b></font></td></tr>'''%(title,tag))
    error_idx = 0
    for error in errors:
       error_stack = error.stack.findAll('frame')
       error_idx += 1
       f.write('''<tr><td bgcolor="#dbdbdb" rowspan="%d"><font face="Courier New" size="2">&nbsp;%d&nbsp;</font></td><td bgcolor="#dbdbdb" colspan="6"><font face="Courier New" size="2"><b>&nbsp;%s&nbsp;</b></font></td></tr>
       '''%(len(error_stack)+1,error_idx,error.xwhat != None and str(error.xwhat.text) or str(error.what)))
       frame_idx = 0
       for frame in error_stack:
          frame_idx += 1
          f.write('''
              <tr>
                  <td bgcolor="#ececec">
                  <font face="Courier New" size="2">
                      &nbsp;%s&nbsp;
                  </font>
                  </td>
                  <td bgcolor="#ececec">
                  <font face="Courier New" size="2">
                      &nbsp;%s&nbsp;
                  </font>
                  </td>
                  <td bgcolor="#ececec">
                  <font face="Courier New" size="2">
                      &nbsp;%s&nbsp;
                  </font>
                  </td>
                  <td bgcolor="#ececec">
                  <font face="Courier New" size="2">
                      &nbsp;%s:%s&nbsp;
                  </font>
                  </td>
                  <td bgcolor="#ececec">
                  <font face="Courier New" size="2">
                      &nbsp;%s&nbsp;
                  </font>
                  </td>
                  %s
              </tr>
          ''' % (
                 frame_idx == 1 and 'at' or 'by',
                 frame.ip   == None and '????' or frame.ip, 
                 frame.fn   == None and '????' or frame.fn, 
                 frame.file == None and '????' or os.path.basename(frame.file.string),
                 frame.line == None and '????' or frame.line,
                 frame.obj  == None and '????' or os.path.basename(frame.obj.string),
                 frame_idx == 1 and ('<td bgcolor="#ffffff" rowspan="%d"><font face="Courier New" size="2">%s</font></td>'%(len(error_stack),vg_supp_string(error))) or ''
                 ))

# Writes a HTML report for a given valgrind XML logfile
def vg_write_report(f, vgf, use_valgrind):
   if use_valgrind != 'False':
      vgErrorTags = vg_errors(vgf)
      MemErrors  = []
      MemErrors_Bytes   = 0
      MemErrors_Blocks  = 0
      MemLeaksDL = []
      MemLeaksDL_Bytes  = 0
      MemLeaksDL_Blocks = 0
      MemLeaksPL = []
      MemLeaksPL_Bytes  = 0
      MemLeaksPL_Blocks = 0
      MemLeaksIL = []
      MemLeaksIL_Bytes  = 0
      MemLeaksIL_Blocks = 0
      MemLeaksSR = []
      MemLeaksSR_Bytes  = 0
      MemLeaksSR_Blocks = 0
      for vgErrorTag in vgErrorTags:
          if str(vgErrorTag.kind.string) == "Leak_DefinitelyLost":
              MemLeaksDL        += [vgErrorTag]
              MemLeaksDL_Bytes  += int(vgErrorTag.xwhat.leakedbytes.string)
              MemLeaksDL_Blocks += int(vgErrorTag.xwhat.leakedblocks.string)
          elif str(vgErrorTag.kind.string) == "Leak_PossiblyLost":
              MemLeaksPL        += [vgErrorTag]
              MemLeaksPL_Bytes  += int(vgErrorTag.xwhat.leakedbytes.string)
              MemLeaksPL_Blocks += int(vgErrorTag.xwhat.leakedblocks.string)
          elif str(vgErrorTag.kind.string) == "Leak_IndirectlyLost":
              MemLeaksIL        += [vgErrorTag]
              MemLeaksIL_Bytes  += int(vgErrorTag.xwhat.leakedbytes.string)
              MemLeaksIL_Blocks += int(vgErrorTag.xwhat.leakedblocks.string)
          elif str(vgErrorTag.kind.string) == "Leak_StillReachable":
              MemLeaksSR        += [vgErrorTag]
              MemLeaksSR_Bytes  += int(vgErrorTag.xwhat.leakedbytes.string)
              MemLeaksSR_Blocks += int(vgErrorTag.xwhat.leakedblocks.string)
          else:
              MemErrors         += [vgErrorTag]
              MemErrors_Bytes   += 0
              MemErrors_Blocks  += 0
              
      if use_valgrind == 'Full':
         f.write('''
         <h1>Valgrind report</h1>
         <font face="Courier New" size="2">
         <table border="0" cellpadding="0">
           <tr>
               <td bgcolor="#ececec" align=right>&nbsp;<b>MEM. ERRORS/CORRUPTIONS<b/>&nbsp;</td>
               <td bgcolor="%s" align=right>&nbsp;<b>%d<b/>&nbsp;</td>
               <td bgcolor="#ececec" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ececec" align=right><a href="#memerrors" style="text-decoration: none">&nbsp;<b>errors/corruptions</b>&nbsp;</a></td>
               <td bgcolor="#ffffff" align=right></td>
               <td bgcolor="#ffffff" align=right></td>
               <td bgcolor="#ffffff" align=right></td>
               <td bgcolor="#ffffff" align=right></td>
           </tr>
           <tr>
               <td bgcolor="#ececec" align=right rowspan="2">&nbsp;<b>MAJOR MEM. LEAKS<b/>&nbsp;</td>
               <td bgcolor="%s" align=right rowspan="2">&nbsp;<b>%d<b/>&nbsp;</td>
               <td bgcolor="#ececec" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ececec" align=right><a href="#memleaksDL" style="text-decoration: none">&nbsp;<b>definitely lost:</b>&nbsp;</a></td>
               <td bgcolor="#ffffff" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ffffff" align=right>bytes in</td>
               <td bgcolor="#ffffff" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ffffff" align=right>blocks</td>
           </tr>
           <tr>
              <td bgcolor="#ececec" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ececec" align=right><a href="#memleaksPL" style="text-decoration: none">&nbsp;<b>possibly lost:</b>&nbsp;</a></td>
               <td bgcolor="#ffffff" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ffffff" align=right>bytes in</td>
               <td bgcolor="#ffffff" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ffffff" align=right>blocks</td>
           </tr>
           <tr>
               <td bgcolor="#ececec" align=right rowspan="2">&nbsp;<b>MINOR MEM. LEAKS<b/>&nbsp;</td>
               <td bgcolor="%s" align=right rowspan="2">&nbsp;<b>%d<b/>&nbsp;</td>
               <td bgcolor="#ececec" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ececec" align=right><a href="#memleaksIL" style="text-decoration: none">&nbsp;<b>indirectly lost:</b>&nbsp;</a></td>
               <td bgcolor="#ffffff" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ffffff" align=right>bytes in</td>
               <td bgcolor="#ffffff" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ffffff" align=right>blocks</td>
           <tr>
               <td bgcolor="#ececec" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ececec" align=right><a href="#memleaksSR" style="text-decoration: none">&nbsp;<b>still reachable:</b>&nbsp;</a></td>
               <td bgcolor="#ffffff" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ffffff" align=right>bytes in</td>
               <td bgcolor="#ffffff" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ffffff" align=right>blocks</td>
           </tr>
         </table>
         </font>
         <br/>
         ''' % (
                len(MemErrors)>0 and '#DD7E7E' or '#ececec',len(MemErrors),
                len(MemErrors),
                len(MemLeaksDL)+len(MemLeaksPL)>0 and '#ffa0a0' or '#ececec',len(MemLeaksDL)+len(MemLeaksPL),
                len(MemLeaksDL),MemLeaksDL_Bytes,MemLeaksDL_Blocks,
                len(MemLeaksPL),MemLeaksPL_Bytes,MemLeaksPL_Blocks,
                len(MemLeaksIL)+len(MemLeaksSR)>0 and '#ffc2c2' or '#ececec',len(MemLeaksIL)+len(MemLeaksSR),
                len(MemLeaksIL),MemLeaksIL_Bytes,MemLeaksIL_Blocks,
                len(MemLeaksSR),MemLeaksSR_Bytes,MemLeaksSR_Blocks
                )
         )
      else:
         f.write('''
         <h1>Valgrind report</h1>
         <font face="Courier New" size="2">
         <table border="0" cellpadding="0">
           <tr>
               <td bgcolor="#ececec" align=right>&nbsp;<b>MEM. ERRORS/CORRUPTIONS<b/>&nbsp;</td>
               <td bgcolor="%s" align=right>&nbsp;<b>%d<b/>&nbsp;</td>
               <td bgcolor="#ececec" align=right><a href="#memerrors" style="text-decoration: none">&nbsp;<b>errors/corruptions</b>&nbsp;</a></td>
               <td bgcolor="#ffffff" align=right></td>
               <td bgcolor="#ffffff" align=right></td>
               <td bgcolor="#ffffff" align=right></td>
               <td bgcolor="#ffffff" align=right></td>
           </tr>
           <tr>
               <td bgcolor="#ececec" align=right rowspan="2">&nbsp;<b>MAJOR MEM. LEAKS<b/>&nbsp;</td>
               <td bgcolor="%s" align=right>&nbsp;<b>%d<b/>&nbsp;</td>
               <td bgcolor="#ececec" align=right><a href="#memleaksDL" style="text-decoration: none">&nbsp;<b>definitely lost:</b>&nbsp;</a></td>
               <td bgcolor="#ffffff" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ffffff" align=right>bytes in</td>
               <td bgcolor="#ffffff" align=right>&nbsp;%d&nbsp;</td>
               <td bgcolor="#ffffff" align=right>blocks</td>
           </tr>
         </table>
         </font>
         <br/>
         ''' % (
                len(MemErrors)>0 and '#DD7E7E' or '#ececec',
                len(MemErrors),
                len(MemLeaksDL)>0 and '#ffa0a0' or '#ececec',
                len(MemLeaksDL),
                MemLeaksDL_Bytes,
                MemLeaksDL_Blocks
                )
         )
      f.write('''<table border="0" cellpadding="0">''')
      vg_write_table(f, MemErrors,  "Memory Errors/Corruptions", "memerrors")
      vg_write_table(f, MemLeaksDL, "Memory Definitely Lost"   , "memleaksDL")
      if use_valgrind == 'Full':
         vg_write_table(f, MemLeaksPL, "Memory Possibly Lost"     , "memleaksPL")
         vg_write_table(f, MemLeaksIL, "Memory Indirectly Lost"   , "memleaksIL")
         vg_write_table(f, MemLeaksSR, "Memory Still Reachable"   , "memleaksSR")
      f.write('''</table><br/>''')
