// Based off of cppstrtok.cpp in the class folder
// /Assignments/code/cppstrtok/cppstrtok.cpp
// $Id: cppstrtok.cpp,v 1.3 2019-04-05 14:28:09-07 - - $

// Use cpp to scan a file and print line numbers.
// Print out each input line read in, then strtok it for
// tokens.

#include <string>
using namespace std;

#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wait.h>
#include <unistd.h>
#include <stdio.h>

#include "string_set.h"
#include "auxlib.h"

const string CPP = "/usr/bin/cpp -nostdinc";
constexpr size_t LINESIZE = 1024;

// strip the file extension any string  
string strp (char* filename){
   string input_stripped = string(basename(filename));
   size_t lastindex = input_stripped.find_last_of(".");
   input_stripped = input_stripped.substr(0,lastindex);
   return input_stripped;
}

// Chomp the last character from a buffer if it is delim.
void chomp (char* string, char delim) {
   size_t len = strlen (string);
   if (len == 0) return;
   char* nlpos = string + len - 1;
   if (*nlpos == delim) *nlpos = '\0';
}


// Run cpp against the lines of the file.
// return the hashed stringset.
const string* cpplines (FILE* pipe) {
   int linenr = 1;
   const string* str;
   for (;;) {
      char buffer[LINESIZE];
      const char* fgets_rc = fgets (buffer, LINESIZE, pipe);
      if (fgets_rc == nullptr) break;
      chomp (buffer, '\n');
      // http://gcc.gnu.org/onlinedocs/cpp/Preprocessor-Output.html
      char inputname[LINESIZE];
      sscanf (buffer, "# %d \"%[^\"]\"",&linenr, inputname);
      char* savepos = nullptr;
      char* bufptr = buffer;
      for (int tokenct = 1;; ++tokenct) {
        char* token = strtok_r (bufptr, " \t\n", &savepos);
        bufptr = nullptr;
        if (token == nullptr) break;
        str = string_set::intern(token);
      }
      ++linenr;
   }
   return str;
}

int main (int argc, char** argv) {
   const char* execname = basename (argv[0]);
   int yy_flex_debug = 0;
   int yyparse = 0;
   char* filename = argv[argc-1];
   int exit_status = EXIT_SUCCESS;
   string command = CPP;
   string input_stripped = strp(filename);
   // where we'll store the stringset 
   const string* stringset; 

   // parse command line arguments
   // based off of the example in 
   // gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
   int opt;
   while ((opt = getopt(argc,argv,"@:D:ly")) != -1 ){  
     switch (opt){ 
       case '@':
         //filler, todo
         DEBUGF(*optarg,filename,116,filename,filename);
         break;
       case 'D':
         command = command + " -D" + std::string(optarg);
         break;
       case 'l': 
         yy_flex_debug = 1;
         break;
       case 'y':
         yyparse = 1;
         break;
       case '?':
         break;
     }
   }
   // pass the file specified into the preprocessor
   command = command + " " + filename;
   // printf ("command=\"%s\"\n", command.c_str());
   FILE* pipe = popen (command.c_str(), "r");
   if (pipe == nullptr) {
      exit_status = EXIT_FAILURE;
      fprintf (stderr, "%s: %s: %s\n",
               execname, command.c_str(), strerror (errno));
   }else {
      stringset = cpplines(pipe);
      int pclose_rc = pclose (pipe);
      eprint_status (command.c_str(), pclose_rc);
      if (pclose_rc != 0) exit_status = EXIT_FAILURE;
   }

   // dump the string table into *.str
   string append = ".str";
   FILE *strfp;
   string fn =(input_stripped+append);
   strfp = fopen (fn.c_str(),"w");
   if (strfp == NULL) {
      exit_status = EXIT_FAILURE;
      fprintf (stderr, "%s: %s: %s\n",
               execname, command.c_str(), strerror (errno)); 
      // filler to supress unused variable warnings
      fprintf (stderr, "flexdbg%d\nparsedbg:%d\nstringset size: %ld\n",
               yy_flex_debug,yyparse,stringset->size()); 
   }
   string_set::dump(strfp); 
   if (pclose(strfp) < 0 ) {
      exit_status = EXIT_FAILURE;
      fprintf (stderr, "%s: %s: %s\n",
               execname, command.c_str(), strerror (errno));
   }
   return exit_status;
}

