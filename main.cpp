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
#include "lyutils.h"
#include "astree.h"
#include "symbol_table.h"
#include "3AC_emitter.h"

const string CPP = "/usr/bin/cpp -nostdinc";
string command = CPP;
constexpr size_t LINESIZE = 1024;
int s_debug = 0;
int t_debug = 0;
int a_debug = 0;

// parse command line arguments
// based off of the example in 
// gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
void parseargs (int argc, char** argv){
   int opt;
   while ((opt = getopt(argc,argv,"@:D:ly")) != -1 ){  
     switch (opt){ 
       case '@':
         if (strcmp(optarg,"str")==0) 
           s_debug = 1; 
         if (strcmp(optarg,"tok")==0)
           t_debug = 1; 
         if (strcmp(optarg,"ast")==0)
           a_debug = 1; 
       break;
       case 'D':
         command = command + " -D" + std::string(optarg); 
         break;
       case 'l': 
         yy_flex_debug = 1;      
         break;
       case 'y':
         yydebug = 1;            
         break;
       case '?':
         errprintf("bad option (%c)\n",optopt); 
         break;
     }
   }
   if (optind > argc){
     errprintf("Usage: %s [-ly] [filename]\n",
                exec::execname.c_str());    
     exit (exec::exit_status);
   }
}

// calls yylex until yyin reaches EOF. (deprecated function)
// call yyparse() instead.
void lex_scan(){
  int chr;
  for (;;){
     chr = yylex();
     if (chr == YYEOF) break;
     if (t_debug) { errprintf ( "token:%s\ntoken code:%s\n",yytext,
                                parser::get_tname(yylval->symbol)); } 
     destroy(yylval);
  }
}

// call CPP onto yyin.if successful, start scanning using yylex.
void exec_cpp(string filename){
   // pass the file specified into the preprocessor
   command = command + " " + filename;
   if (s_debug) { errprintf("COMMAND:%s\n",command.c_str()); }
   yyin = popen (command.c_str(), "r");
   if (yyin == nullptr) {
     syserrprintf (command.c_str());
     exit (exec::exit_status);
   }else {
     if (yy_flex_debug){
       errprintf("-- popen (%s),fileno(yyin) = %d\n",
                 command.c_str(),fileno(yyin));
     }
   }
}

// append a file ending to the basename and open a file with that name
FILE* appendopen(string basename, string extension){
   string fn = basename + extension;
   FILE *s; 
   s = fopen (fn.c_str(),"w"); 
   if (s == NULL){
     errprintf ("FAILURE opening file %s for writing.",fn.c_str());
   }
   return s;
}

void e_close(FILE* f) {
  int close = pclose (f);
  eprint_status (command.c_str(), close);
  if (close !=0) exec::exit_status = EXIT_FAILURE;
}

// remove the file extension and path from a filename 
string strp(char* filename){
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

int main (int argc, char** argv) {
   yy_flex_debug = 0;
   yydebug = 0;
   exec::execname = basename (argv[0]);
   if (argc <= 1){
     errprintf ("please specify filename for program\n");
     return exec::exit_status;
   }
   char* filename = argv[argc-1];
   string input_stripped = strp(filename);
   string append;
   parseargs(argc,argv);

   // Open str,tok,ast files.
   FILE *strfp;
   append = ".str";
   strfp = appendopen (input_stripped,append);
   FILE *tokfp;
   append = ".tok";
   tokfp = appendopen (input_stripped,append);
   lexer::tokenfp(tokfp);
   FILE *astfp;
   append = ".ast";
   astfp = appendopen (input_stripped,append);
   FILE *symfp;
   append = ".sym";
   symfp = appendopen (input_stripped,append);
   FILE *oilfp;
   append = ".oil";
   oilfp = appendopen (input_stripped,append);

   // Run the c preprocessor and pipe the output to yyin.
   exec_cpp(filename);
   lexer::newfilename(string(basename(filename)));

   // run yylex against the piped output.
   // Generate the stringset and write lexical 
   // information to token file.
   int parse_rc = yyparse();

   // personal debug flag ,
   // draws astree to stderr
   if (a_debug){
     astree::draw_attrib (stderr,parser::root);
   }
  
   /* if the parse was sucessful */
   if (!parse_rc){
     // generate the symbol table
     int sym_rc = ssymgen(parser::root);
     // dump the hashed tokenset to file.
     string_set::dump(strfp);
     // dump the astree
     astree::draw(astfp,parser::root);

      /* if the symbol table generation was sucessful */
     if (!sym_rc){
       // dump all symbol tables
       dump_all_tables(symfp);
       // generate the 3ac tables
       generate_3ac();
       // dump the 3ac tables
       emit_all3ac(oilfp);
     }
   }

   // Close all files.
   e_close(yyin);
   e_close(tokfp);
   e_close(strfp);
   e_close(astfp);
   e_close(symfp);
   e_close(oilfp);

   // free symbol table memory
   free_symbol();
   // free 3ac memory
   free_3ac();
   if (parse_rc) {
      errprintf("parse failed (%d)\n", parse_rc);
   }else {
      destroy(parser::root);
   }
   yylex_destroy();
   return exec::exit_status;
}

