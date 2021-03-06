#include <algorithm>

#include "3AC_emitter.h"
#include "lyutils.h"
#include "auxlib.h"

using namespace std;

int reg_count, while_count, if_count, err_count = 0;

namespace three_address_code {
  unordered_map <symbol_table*, statement_table*> *table_lookup = nullptr;
  statement_table *all_globals = nullptr; 
  vector<const string*> *all_strings = nullptr;
  vector<pair<const string*,statement_table*>> *all_functions = nullptr;
}

void free_3ac(){
  delete three_address_code::table_lookup;
  if (three_address_code::all_functions){
    for (auto itor : *(three_address_code::all_functions)){
      for (auto itor2: *itor.second) {
        delete itor2; 
      }
      delete itor.second;
    }
  }
  if (three_address_code::all_globals){
    for (auto itor: *(three_address_code::all_globals)){
      delete itor;
    }
  }
  delete three_address_code::all_globals;
  delete three_address_code::all_functions;
  delete three_address_code::all_strings;
}

// parse types of either symbol or astree
template <class Type> string parse_typesize(const Type &o){ 
  attr_bitset a = o->attributes;
  const string *sname = o->sname;
  string st = "";
  if (a[static_cast<int>(attr::TYPEID)]){
    st.append ("struct ");
    if (sname != nullptr){
      st.append (sname->c_str());
      st.append (" ");
    }
    return st;
  }
  if (a[static_cast<int>(attr::STRING)] || a[static_cast<int>(attr::ARRAY)]){
    st.append ("ptr ");
    return st;
  }
  if (a[static_cast<int>(attr::INT)]){
    st.append("int ");
    return st;
  }
  // void return
  return st;
}

statement *p_expr(astree *expr, statement_table *current);
// parse expression, and return a register that it is stored in
reg *expr_reg (astree *expr, statement_table *current){
  statement *parsed_expr  = p_expr(expr,current);
  if (parsed_expr && parsed_expr->t0){
    return parsed_expr->t0->deep_copy(); 
  }
  else {
    errprintf("3ac: invalid expression passed in loop\n");
    ++err_count;
    return nullptr;
  }
  return nullptr;
}

// recurse down until you find a non-equals symbol,return it
astree *recurse_non_equal(astree *expr){
  while (expr->symbol != '=' && expr->children.size() == 2){
    expr = expr->children[1];
  }
  return expr;
}

// special helper function for astree_stride for index only
template <class Type> string *astree_stride_symbol_index(const Type &current){
  if (current->attributes[static_cast<int>(attr::STRING)]){
    return new string (":c");
  }
  else if (current->attributes[static_cast<int>(attr::TYPEID)]){
    return new string (":p");
  }
  else if (current->attributes[static_cast<int>(attr::INT)]){
    return new string (":i");
  }
  ++err_count;
  errprintf ("3ac: Invalid symbol parsed for astree_stride_symbol_index");
  return nullptr;
}

//helper function for astree_stride
template <class Type> string *astree_stride_symbol(const Type &current){
  if ( current->attributes[static_cast<int>(attr::ARRAY)]
      || current->attributes[static_cast<int>(attr::STRING)]
      || current->attributes[static_cast<int>(attr::TYPEID)]){
      return new string(":p");
  } 
  else if (current->attributes[static_cast<int>(attr::CHAR)]){
    return new string (":c");
  } 
  else if (current->attributes[static_cast<int>(attr::INT)]){
    return new string(":i");
  }
  ++err_count; 
  errprintf ("3ac: Invalid symbol parsed for astree_stride_symbol\n");
  return nullptr;
}

// parse an astree expression for a expr stride
string *astree_stride(statement_table *current,astree *expr){
  symbol *parse = nullptr;
  switch (expr->symbol){
    case TOK_CHARCON:
      return new string(":c");
    case TOK_STRINGCON:
    case TOK_NULLPTR:
    case TOK_ALLOC:
      return new string(":p");
    case TOK_INTCON:
    case TOK_LT:
    case TOK_LE:
    case TOK_GT:
    case TOK_GE:
    case '*':
    case '/':
    case '%':
    case '+':
    case '-':
    case TOK_NOT:
    case TOK_EQ:
    case TOK_NE:
      return new string(":i");
    case TOK_IDENT:
      return astree_stride_symbol(expr);
    case TOK_CALL:
      // find function definition in global table
      parse = symtable::global->find(expr->children[0]->lexinfo)->second;
      return astree_stride_symbol(parse);
    case TOK_INDEX:
      return astree_stride_symbol_index(expr);
    case TOK_ARROW:
      // get symbol of struct
      parse = symtable::struct_t->find(expr->sname)->second;
      // get symbol of struct parameter
      parse = parse->fields->find(expr->children[1]->lexinfo)->second;
      return astree_stride_symbol(parse);
    case '=':
      astree *noneq = recurse_non_equal(expr);
      return astree_stride(current,noneq);
  }
  errprintf ("3AC: INVALID EXPR parsed in astree_stride\n");
  ++err_count;
  return nullptr;
}

reg *parse_variable(astree *expr, statement_table *current){
  reg *selection = nullptr;
  if (expr->children.size()){
    if (expr->symbol == TOK_ARROW){
      astree *ident = expr->children[0];
      astree *field = expr->children[1];
      reg *ret; 
      ident->children.size() ? ret = expr_reg(ident,current) : ret = new reg_ident(ident->lexinfo);
      selection = new reg_selection(ret,expr->sname,field->lexinfo);
    }
    else if (expr->symbol == TOK_INDEX){
      astree *ident = expr->children[0];
      astree *index = expr->children[1];
      reg *select = ident->children.size() ? expr_reg(ident,current) : new reg_ident(ident->lexinfo);
      reg *number = index->children.size() ? expr_reg(index,current) : new reg_ident(index->lexinfo);
      selection = new reg_array_index(select,number,astree_stride(current,expr));
    }
  }
  else
    selection = new reg_ident(expr->lexinfo);
  return selection;
}

// assign int to ident
statement *asg_int(astree *expr, statement_table *current, string *label){
  // bot of statements
  statement *bot;
  // get index of top of statements
  size_t top = current->size();

  astree *ident = expr->symbol == TOK_TYPE_ID ? expr->children[1] : expr->children[0];
  astree *parse = expr->symbol == TOK_TYPE_ID ? expr->children[2] : expr->children[1];

  if (parse->children.size()){  
    bot = new statement(expr);
    reg *bot_reg = expr_reg(parse,current);
    if (bot_reg){
      bot->t0 = parse_variable(ident,current);
      bot->t1 = bot_reg;
      bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
      current->push_back(bot);
      current->at(top)->label = label; 
    }
    else {
      errprintf ("3ac: parsing expr failed: %s \n",expr->lexinfo->c_str());
      ++err_count;
      return nullptr;
    }
  }
  else {
    bot = new statement(expr);
    bot->t0 = parse_variable(ident,current);
    bot->t1 = new reg_ident(parse->lexinfo);
    bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
    current->push_back(bot);
    current->at(top)->label = label; 
  }
  return bot;
}

// gets the typesize for an ALLOC array call
// and allocs a string representation
string *alloc_array_typesize(astree *expr){
  string *ret = new string();
  astree *type = expr->children[0]->children[0];
  switch (type->symbol){
    case TOK_INT:
      ret->append("int");
      break;
    case TOK_STRING:
      ret->append("ptr");
      break;
    case TOK_PTR:
      ret->append("struct ");
      ret->append(*(type->children[0]->lexinfo));
      break;
  }
  return ret;
}

statement *alloc_array(astree *expr,statement_table *current, reg *init){
  statement *bot = new statement(expr);
  statement *parsed_sz = new statement(expr);
  astree *alloc_sz = expr->children[1];
  /* the size to malloc (stored in a reg)  */
  reg *ret = new reg_temp(astree_stride(current,expr),reg_count);
  ++reg_count;
  parsed_sz->t0 = ret;  
  // if the number is an expression, parse it, otherwise assign it directly
  parsed_sz->t1 = alloc_sz->children.size() ? expr_reg(alloc_sz,current) : new reg_ident(alloc_sz->lexinfo);
  parsed_sz->op = new string("*");
  parsed_sz->t2 = new reg_typesize(alloc_array_typesize(expr)); 
  parsed_sz->t2->set_unop(new string("sizeof"));
  parsed_sz->itype.set(static_cast<int>(instruction::ASSIGNMENT));
  current->push_back(parsed_sz);

  /* the actual malloc call */
  vector<reg*> *malloc_params = new vector <reg*>();
  malloc_params->push_back(ret->deep_copy());
  bot->t0 = init;
  bot->t1 = new reg_function_call(new string ("malloc"),malloc_params);
  bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
  current->push_back(bot);
  return bot;
}

statement *alloc_struct(astree *expr,statement_table *current, reg *init){
  statement *bot = new statement(expr);
  vector <reg*> *malloc_params = new vector<reg*>();
  string *struct_name = new string("struct " + *(expr->children[0]->lexinfo));
  reg *malloc_typesz = new reg_typesize(struct_name); 
  malloc_typesz->set_unop(new string("sizeof"));
  malloc_params->push_back(malloc_typesz);  
  bot->t0 = init;
  bot->t1 = new reg_function_call(new string("malloc"),malloc_params);
  bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
  current->push_back(bot);
  return bot;
}

statement *alloc_string(astree *expr,statement_table *current, reg *init){
  statement *bot = new statement(expr);
  astree *alloc_sz = expr->children[1];
  vector <reg*> *malloc_params = new vector<reg*>();
  // if number is an expression, parse it, otherwise just assign the single number.
  reg *malloc_number = alloc_sz->children.size() ? expr_reg(alloc_sz,current) : new reg_ident (alloc_sz->lexinfo);
  malloc_params->push_back(malloc_number);
  bot->t0 = init;
  bot->t1 = new reg_function_call(new string("malloc"),malloc_params);
  bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
  current->push_back(bot);
  return bot;
}

statement *asg_array(astree *expr, statement_table *current, string *label){
  statement *bot;
  size_t top = current->size();

  astree *ident = expr->symbol == TOK_TYPE_ID ? expr->children[1] : expr->children[0];
  astree *parse = expr->symbol == TOK_TYPE_ID ? expr->children[2] : expr->children[1];

  if (parse->children.size()){
    if (parse->symbol == TOK_ALLOC){
      reg *t0 = parse_variable(ident,current);
      bot = alloc_array(parse,current,t0);
      current->at(top)->label = label;
    }
    else {
      reg *ret_reg = expr_reg(parse,current);
      bot = new statement(expr);
      bot->t0 = parse_variable(ident,current);
      bot->t1 = ret_reg;
      bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
      current->push_back(bot);
      current->at(top)->label = label;
    }
  }
  else {
    bot = new statement(expr);
    bot->t0 = parse_variable(ident,current);
    bot->t1 = new reg_ident(parse->lexinfo);
    bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
    current->push_back(bot);
    current->at(top)->label = label;
  }
  return bot;
}

statement *asg_struct(astree *expr, statement_table *current, string *label){
  statement *bot;
  size_t top = current->size();

  astree *ident = expr->symbol == TOK_TYPE_ID ? expr->children[1] : expr->children[0];
  astree *parse = expr->symbol == TOK_TYPE_ID ? expr->children[2] : expr->children[1];

  if (parse->children.size()){
    if (parse->symbol == TOK_ALLOC){
      reg *t0 = parse_variable(ident,current);
      bot = alloc_struct(parse,current,t0);
      current->at(top)->label = label;
    }
    else {
      reg *ret_reg = expr_reg(parse,current);
      bot = new statement(expr);
      bot->t0 = parse_variable(ident,current);
      bot->t1 = ret_reg;
      bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
      current->push_back(bot);
      current->at(top)->label = label;
    }
  }
  else {
    bot = new statement(expr);
    bot->t0 = parse_variable(ident,current);
    bot->t1 = new reg_ident(parse->lexinfo);
    bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
    current->push_back(bot);
    current->at(top)->label = label;
  }
  return bot;
}

reg *p_string_reg(astree *expr){
  reg *t1;
  vector<const string*>::iterator it;
  it = find(three_address_code::all_strings->begin(),three_address_code::all_strings->end(),expr->lexinfo);
  if (it != three_address_code::all_strings->end()){
    t1 = new reg_string_pointer(it - three_address_code::all_strings->begin()); 
  }
  else {
    t1 = new reg_string_pointer(three_address_code::all_strings->size());
    three_address_code::all_strings->push_back(expr->lexinfo);
  }
  return t1;
}

statement *p_string (astree *expr, statement_table *current, reg *init){
  statement *bot = new statement(expr);
  bot->t0 = init;
  bot->t1 = p_string_reg(expr);
  bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
  current->push_back(bot);
  return bot;
}

statement *asg_string(astree *expr, statement_table *current, string *label){
  statement *bot;
  size_t top = current->size();

  astree *ident = expr->symbol == TOK_TYPE_ID ? expr->children[1] : expr->children[0];
  astree *parse = expr->symbol == TOK_TYPE_ID ? expr->children[2] : expr->children[1];

  if (parse->children.size()){
    if (parse->symbol == TOK_ALLOC){
      reg *t0 = parse_variable(ident,current);
      bot = alloc_string(parse,current,t0);
      current->at(top)->label = label;
    }
    else {
      reg *ret_reg = expr_reg(parse,current);
      bot = new statement(expr);
      bot->t0 = parse_variable(ident,current);
      bot->t1 = ret_reg;
      bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
      current->push_back(bot);
      current->at(top)->label = label;
    }
  }
  else {
    if (parse->symbol == TOK_STRINGCON){
      reg *t0 = parse_variable(ident,current);
      bot = p_string(parse,current,t0);
      current->at(top)->label = label;
    }
    else {
      bot = new statement(expr);
      bot->t0 = parse_variable(ident,current);
      bot->t1 = new reg_ident(parse->lexinfo);
      bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
      current->push_back(bot);
      current->at(top)->label = label;
    }
  }
  return bot;
}

// check type, and call approriate parsing function
statement *asg_check_type(astree *expr, statement_table *current, string *label){
  attr_bitset a = expr->attributes;
  if (a[static_cast<int>(attr::ARRAY)]){
    return asg_array(expr,current,label);
  }
  if (a[static_cast<int>(attr::TYPEID)]){
    return asg_struct(expr,current,label);
  }
  if (a[static_cast<int>(attr::STRING)]){
    return asg_string(expr,current,label);
  }
  if (a[static_cast<int>(attr::INT)]){
    return asg_int(expr,current,label);
  }
  errprintf ("3ac: Type not found for assignment \n");
  ++err_count;
  return nullptr;
}

// if 'tok_type_id'  it's an variable declaration
// else if '=' it's an assignment
statement *p_assignment(astree *expr, statement_table *current, string *label){
  statement *ret = nullptr;
  if (expr->children.size() > (expr->symbol == TOK_TYPE_ID ? 2 : 1)){
    ret = asg_check_type(expr,current,label);
    return ret;
  }  
  // lone declaration e.g. int x;
  // only for global variables: 
  // we won't process local definitions (already done by the symbol table)
  else {
    astree *ident;
    expr->symbol == TOK_TYPE_ID ? ident = expr->children[1] : ident = expr->children[0]; 
    statement *ac =new statement(expr);
    ac->t0 = new reg_ident(ident->lexinfo);
    ac->itype.set(static_cast<int>(instruction::ASSIGNMENT));
    current->push_back(ac);
  }
  return nullptr;
}

statement *p_binop(astree *expr, statement_table *current){
  statement *ac = new statement(expr); 
  ac->op = new string(*(expr->lexinfo));
  ac->t0 = new reg_temp(astree_stride(current,expr),reg_count);
  ++reg_count;
  // parse the expressions
  // check the two children
  astree *left = expr->children[0];
  if (left->children.size()) {
    ac->t1 = new reg_temp(astree_stride(current,left),reg_count);
    p_expr(left,current);
  } 
  else {
    ac->t1 =new reg_ident(left->lexinfo);
  }
  astree *right = expr->children[1];
  if (right->children.size()){
    ac->t2 = new reg_temp(astree_stride(current,right),reg_count);
    p_expr(right,current);
  } 
  else {
    ac->t2 =new reg_ident(right->lexinfo);
  }
  ac->itype.set(static_cast<int>(instruction::ASSIGNMENT));
  current->push_back(ac);
  return ac;
}

statement *p_unop(astree *expr, statement_table *current){
  statement *ac =new statement(expr);
  ac->t0 = new reg_temp(astree_stride(current,expr),reg_count);
  ++reg_count;

  astree *assignment = expr->children[0];
  if (assignment->children.size()){
    reg *unary = new reg_temp(astree_stride(current,assignment),reg_count);
    unary->set_unop(new string(*(expr->lexinfo)));
    ac->t1 = unary;
    p_expr(assignment,current);
  }
  else {
    reg *unary = new reg_ident(assignment->lexinfo);
    unary->set_unop(new string(*(expr->lexinfo)));
    ac->t1 = unary;
  }
  ac->itype.set(static_cast<int>(instruction::ASSIGNMENT));
  current->push_back(ac);
  return ac;
}

statement *p_polymorphic(astree *expr, statement_table *current){
  if (expr->children.size() > 1){
    return p_binop(expr,current);
  }
  else if (expr->children.size() == 1) {
    return p_unop(expr,current);
  }
  // should never happen
  errprintf("3ac :p_polymorphic called on expr with size 0\n");
  ++err_count;
  return nullptr;
}

statement *p_static_val(astree *expr, statement_table *current){
  statement *ac = new statement(expr); 
  ac->t0 = new reg_temp(astree_stride(current,expr),reg_count);
  ++reg_count;
  ac->t1 = new reg_ident(expr->lexinfo);  
  current->push_back(ac);
  ac->itype.set(static_cast<int>(instruction::ASSIGNMENT));
  return ac;
}

statement *p_call(astree *expr, statement_table *current, string *label){
  size_t index = current->size();
  statement *bot = new statement(expr);
  vector <reg*> *args = new vector <reg*>();
  if (expr->children.size() > 1){
    for (size_t i = 1; i < expr->children.size(); i++){
      reg *push;
      if (expr->children[i]->children.size()){
        reg *stored = expr_reg(expr->children[i],current);
        if (stored){
          push = stored;
        }
        else {
          errprintf ("3ac: return expression incorrectly parsed!\n");
          ++err_count;
          return nullptr;
        }
      } 
      else {
        push = new reg_ident(expr->children[i]->lexinfo);  
      }
      args->push_back(push);
    }
  } 
  reg *fname = new reg_function_call(new string (*(expr->children[0]->lexinfo)),args);
  bot->t1 = fname;
  bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
  current->push_back(bot);
  current->at(index)->label = label;
  return bot;
}

statement *p_field(astree *expr, statement_table *current){
  statement *bot = new statement(expr);
  astree *ident = expr->children[0];
  astree *field = expr->children[1];
  bot->t0 = new reg_temp(astree_stride(current,expr),reg_count);
  ++reg_count;
  reg *ret = ident->children.size() ? expr_reg(ident,current) : new reg_ident(ident->lexinfo);
  reg *selection = new reg_selection(ret,expr->sname,field->lexinfo);
  bot->t1 = selection;
  bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
  current->push_back(bot);
  return bot;
}

// extra arguments for string as an expression
statement *p_string_expr (astree *expr, statement_table *current){
  // store function call in register
  reg *ret = new reg_temp(astree_stride(current,expr),reg_count);
  ++reg_count;
  statement *str = p_string(expr,current,ret);  
  return str;
}

// extra arguments for function call as an expression
statement *p_call_expr (astree *expr, statement_table *current){
  // store function call in register
  reg *ret = new reg_temp(astree_stride(current,expr),reg_count);
  ++reg_count;
  statement *call = p_call(expr,current,nullptr);  
  if (call)
    call->t0 = ret;
  else{
    errprintf("3ac: call not parsed correctly\n");
    ++err_count;
  }
  return call;
}

statement *p_loop_condition_relop(string *relop, string *goto_label, astree *expr, statement_table *current){
  astree *left = expr->children[0];
  astree *right = expr->children[1];
  statement *ret = new statement(expr);
  ret->condition = goto_label;
  left->children.size() ? ret->t1 = expr_reg(left,current) : ret->t1 = new reg_ident(left->lexinfo); 
  ret->op = relop;
  right->children.size() ? ret->t2 = expr_reg(right,current) : ret->t2 = new reg_ident(right->lexinfo); 
  ret->itype.set(static_cast<int>(instruction::GOTO));
  return ret;
}

statement *p_loop_condition(string *goto_label, astree *expr, statement_table *current){
  statement *ret = nullptr;
  if (expr->children.size()){
    if (expr->children.size() > 1){
      switch (expr->symbol){
        case TOK_EQ:
	  return p_loop_condition_relop(new string ("!="),goto_label,expr,current);
        case TOK_NE:
	  return p_loop_condition_relop(new string ("=="),goto_label,expr,current);
        case TOK_LT:          
	  return p_loop_condition_relop(new string (">"),goto_label,expr,current);
        case TOK_LE:          
	  return p_loop_condition_relop(new string (">="),goto_label,expr,current);
        case TOK_GT:          
	  return p_loop_condition_relop(new string ("<"),goto_label,expr,current);
	case TOK_GE:          
	  return p_loop_condition_relop(new string ("<="),goto_label,expr,current);
	default:
	  ret = new statement(expr);
	  reg *parse = expr_reg(expr,current);
	  parse->set_unop(new string ("not"));
          ret->condition = goto_label;
	  ret->t1 = parse;
	  ret->itype.set(static_cast<int>(instruction::GOTO));
      }
    } 
    else {
      ret = new statement(expr);
      reg *parse = expr_reg(expr,current);
      parse->set_unop(new string ("not"));
      ret->condition = goto_label;
      ret->t1 = parse;
      ret->itype.set(static_cast<int>(instruction::GOTO));
    }
  }
  else {
    ret = new statement(expr);
    ret->condition = goto_label;
    ret->t1 = new reg_ident(expr->lexinfo);
    ret->t1->set_unop(new string("not"));
    ret->itype.set(static_cast<int>(instruction::GOTO));
  }
  return ret;
}

void p_stmt(astree *expr, statement_table *current, string *label);

statement *p_if(astree *expr, statement_table *current, string *label){ 
  astree *condition = expr->children[0];
  astree *if_statement = expr->children[1];
  int orig_if = if_count;
  ++if_count;

  // if label exists, this if statement is enclosed within
  // another if or while statement. emit the label to encapsulate this if.
  if (label){
    statement *encapsulate = new statement(expr);
    encapsulate->label = label;
    encapsulate->itype.set(static_cast<int>(instruction::LABEL_ONLY));
    current->push_back(encapsulate);
  }
 
  /* if header */
  size_t top = current->size();
  string if_header = ".if" + to_string(orig_if) + ":";
  // if an else statement exists, then we must go to it.
  // otherwise, just go to the end of the if statment.
  string selection = expr->children.size() > 2 ? ".el" : ".fi"; 
  string goto_label = selection + to_string(orig_if);

  /* goto statement */
  statement *parsed = p_loop_condition(new string(goto_label),condition,current);
  if (parsed){
    current->push_back(parsed);
    current->at(top)->label = new string(if_header);
  } else {
    ++err_count;
    errprintf ("parsing of conditonal if failed!");
    return nullptr;
  }

  /* first statement */
  string *first_label = new string(".th" + to_string(orig_if) + ":");
  p_stmt(if_statement,current,first_label);

  /* else parsing */
  string exit_label = ".fi" + to_string(orig_if);
  // if there is an else block
  if (expr->children.size() > 2){
    // emit the goto for the case that the if statement is true
    statement *if_true = new statement (expr->children[1]);
    if_true->condition = new string(exit_label);
    if_true->itype.set(static_cast<int>(instruction::GOTO));
    current->push_back(if_true);
    // emit the else statement alternative
    string *else_label = new string (goto_label + ":");
    astree *else_statement = expr->children[2];
    p_stmt(else_statement,current,else_label);
  }

  /* emit ending label */
  statement *closing_stmt = new statement(expr);
  closing_stmt->label = new string(exit_label + ":");
  closing_stmt->itype.set(static_cast<int>(instruction::LABEL_ONLY));
  current->push_back(closing_stmt);

  return closing_stmt;
}


statement *p_while(astree *expr, statement_table *current, string *label){ 
  astree *condition = expr->children[0];
  astree *stmt = expr->children[1];
  int orig_while = while_count;
  ++while_count;

  // if label exists, this while statement is enclosed within
  // another if or while statement. emit the label to encapsulate this while.
  if (label){
    statement *encapsulate = new statement(expr);
    encapsulate->label = label;
    encapsulate->itype.set(static_cast<int>(instruction::LABEL_ONLY));
    current->push_back(encapsulate);
  }

  /* while header */
  string while_header = ".wh" + to_string(orig_while);
  string goto_label = ".od" + to_string(orig_while);

  // get next index
  size_t top = current->size();

  /* goto statement */
  statement *parsed = p_loop_condition(new string(goto_label),condition,current);
  if (parsed){
    current->push_back(parsed);
    current->at(top)->label = new string(while_header + ":");
  } else {
    ++err_count;
    errprintf ("parsing of conditonal while failed!");
    return nullptr;
  }

  /* first statement */
  string *first_label = new string(".do" + to_string(orig_while) + ":");
  p_stmt(stmt,current,first_label); 

  /* loop statment */
  statement *loop_stmt = new statement (stmt);
  loop_stmt->condition = new string(while_header);
  loop_stmt->itype.set(static_cast<int>(instruction::GOTO));
  current->push_back(loop_stmt);

  /* while end label */
  statement *closing_stmt = new statement(expr);
  closing_stmt->label = new string(goto_label + ":");
  closing_stmt->itype.set(static_cast<int>(instruction::LABEL_ONLY));
  current->push_back(closing_stmt);
  return closing_stmt;
}

// equals in the context of an expr
statement *p_equals(astree *expr, statement_table *current){
  statement *ac = new statement(expr);
  astree *left = expr->children[0];
  ac->t0 = parse_variable(left,current);
  astree *right = expr->children[1];
  // find first non-equal symbol
  astree *final_val = recurse_non_equal(right);
  if (right->children.size()){
    if (final_val->children.size()){
      ac->t1 = new reg_temp(astree_stride(current,final_val),reg_count);
      p_expr(right,current);
    } 
    else {
      if (final_val->symbol == TOK_STRINGCON){
        ac->t1 = p_string_reg(final_val);
        p_expr(right,current);
      } 
      else {
        ac->t1 = new reg_ident(final_val->lexinfo);
        p_expr(right,current);
      }
    }
  } 
  else {
    if (right->symbol == TOK_STRINGCON){
      ac->t1 = p_string_reg(right);
    }
    else {
      ac->t1 =new reg_ident(right->lexinfo);
    }
  }
  ac->itype.set(static_cast<int>(instruction::ASSIGNMENT));
  current->push_back(ac);
  return ac;
}

// helper function for p_alloc (for cases where alloc is part of an expression)
statement *p_alloc(astree *expr, statement_table *current){
  reg *ret = new reg_temp (astree_stride(current,expr),reg_count);
  ++reg_count;
  switch (expr->children[0]->symbol){
    case TOK_ARRAY:
      return alloc_array(expr,current,ret);
    case TOK_IDENT:
      return alloc_struct(expr,current,ret);
    case TOK_STRING:
      return alloc_string(expr,current,ret);
  }
  errprintf("3AC: p_alloc failed: SYMBOL:%s\n");
  ++err_count;
  return nullptr;
}

void p_block(astree *expr, statement_table *current, string *label){
  if (expr->children.size()){
    p_stmt(expr->children[0],current,label); 
    // parse the rest of the statements
    for (size_t i = 1; i < expr->children.size(); ++i){
      p_stmt(expr->children[i],current,nullptr);
    }
  }
  else{
    if (label){
      statement *vacuous_block = new statement(expr);
      vacuous_block->label = label;
      vacuous_block->itype.set(static_cast<int>(instruction::LABEL_ONLY));
      current->push_back(vacuous_block);
    }
  }
}

statement *p_return(astree *expr, statement_table *current, string *label){
  size_t top = current->size();
  statement *bot;
  if (expr->children.size()){
    astree *assignment = expr->children[0];
    bot = new statement(expr);
    if (assignment->children.size()){
      reg *stored = expr_reg(assignment,current);
      if (stored)
        bot->t0 = stored;
      else {
        errprintf ("3ac: return expression incorrectly parsed!");
        ++err_count;
        return nullptr;
      }
    }
    else {
      bot->t0 = new reg_ident(assignment->lexinfo);
    }
    bot->itype.set(static_cast<int>(instruction::RETURN));
    current->push_back(bot);
    current->at(top)->label = label;
  } 
  else {
    bot = new statement(expr);
    bot->itype.set(static_cast<int>(instruction::RETURN));
    current->push_back(bot);
    current->at(top)->label = label;
  }
  return bot;
}

statement *p_index(astree *expr, statement_table *current){
  statement *bot = new statement(expr);
  astree *ident = expr->children[0];
  astree *index = expr->children[1];
  bot->t0 = new reg_temp(astree_stride(current,expr),reg_count);
  ++reg_count;
  reg *ret;
  reg *select = ident->children.size() ? expr_reg(ident,current) : new reg_ident(ident->lexinfo);
  reg *number = index->children.size() ? expr_reg(index,current) : new reg_ident(index->lexinfo);
  ret = new reg_array_index(select,number,astree_stride(current,expr));
  bot->t1 = ret;
  bot->itype.set(static_cast<int>(instruction::ASSIGNMENT));
  current->push_back(bot);
  return bot;
}

statement *p_semicolon(astree *expr, statement_table *current,string *label){
  statement *ac = new statement(expr);
  current->push_back(ac);
  if (label){
    statement *encapsulate = new statement(expr);
    encapsulate->label = label;
    encapsulate->itype.set(static_cast<int>(instruction::LABEL_ONLY));
    current->push_back(encapsulate);
    return encapsulate;
  }
  return ac;
}

// large switch statement for expressions
statement *p_expr(astree *expr, statement_table *current){
  switch (expr->symbol){
    case '=':
      return p_equals(expr,current);
      break;
    case '*':
    case '/':
    case '%':
    case TOK_EQ:
    case TOK_NE:
    case TOK_LT:
    case TOK_LE:
    case TOK_GT:
    case TOK_GE:
      return p_binop(expr,current);
      break;
    case '+':
    case '-':
      return p_polymorphic(expr,current);
      break;
    case TOK_NOT:
      return p_unop(expr,current);
      break;
    case TOK_CHARCON:
    case TOK_INTCON:
    case TOK_NULLPTR:
    case TOK_IDENT:
      return p_static_val(expr,current);
      break;
    case TOK_STRINGCON:
      return p_string_expr(expr,current); 
      break;
    case TOK_CALL:
      return p_call_expr(expr,current);
      break;
    case TOK_ALLOC:
      return p_alloc(expr,current);
      break;
    case TOK_ARROW:
      return p_field(expr,current);
      break;
    case TOK_INDEX:
      return p_index(expr,current);
      break;
    case ';':
      return p_semicolon(expr,current,nullptr);
      break;
  }
  errprintf ("3ac: non-recognized expression parsed:%s \n "
             , parser::get_tname(expr->symbol));
  ++err_count;
  return nullptr;
}

//helper function for p_expr (for cases where expressions are parsed without assignment)
statement *p_expression(astree *expr, statement_table *current, string *label){
  size_t index = current->size();
  statement *ac = p_expr(expr,current);
  if(ac){
    if (expr->symbol != ';'){
      current->at(index)->label = label;
    } else {
      if (label){
        statement *encapsulate = new statement(expr);
        encapsulate->label = label;
        encapsulate->itype.set(static_cast<int>(instruction::LABEL_ONLY));
        current->push_back(encapsulate);
        return encapsulate;
      }
    }
  }
  fprintf (stderr, "3ac: warning: expr parsed without assignment: %s:\n",expr->lexinfo->c_str()); 
  return ac;
}

// parse a statement
void p_stmt(astree *expr, statement_table *current, string *label){
  statement *ac = nullptr;
  switch (expr->symbol){
    case TOK_BLOCK:
      p_block(expr,current,label);
      return;
    case '=':
      ac = p_assignment(expr,current,label);
      break;
    case TOK_TYPE_ID:
      // ignore non-init vardecl e.g. int x;
      if (expr->children.size() > 2)
        ac = p_assignment(expr,current,label);
      else if (expr->children.size() == 2)
        return;
      break;
    case TOK_IF:
      ac = p_if(expr,current,label);
      break;
    case TOK_WHILE:
      ac = p_while(expr,current,label);
      break;
    case TOK_RETURN:
      ac = p_return(expr,current,label);
      break;
    case TOK_CALL:
      ac = p_call(expr,current,label);
      break;
    case ';':
      ac = p_semicolon(expr,current,label);
      break;
    default:
      ac = p_expression(expr,current,label);
      break;
  }
  if (!ac){
    errprintf ("3ac: invalid statement passed :%s\n",expr->lexinfo->c_str());
    ++err_count;
    return;
  }
}

void ac_globalvar(astree *child){
  if (child->children.size() > 2){
    astree *assignment = child->children[2];
    if (assignment->symbol == TOK_ALLOC){
      if (child->children[0]->symbol != TOK_PTR){
        errprintf ("3ac: global variable may not have non-static value assigned to it\n");
        ++err_count;
        return;
      }
    }
    else if (assignment->children.size()){
      errprintf ("3ac: global variable may not have non-static value assigned to it\n");
      ++err_count;
      return;
    }
  }
  p_assignment(child,three_address_code::table_lookup->find(symtable::global)->second,nullptr);
}

// helper function for emit struct
void translate_struct (const string *name, symbol *sym, FILE *out){
  if (sym == nullptr ){
    errprintf ("3ac: invalid symbol for parsing structs\n");
    ++err_count;
    return;
  }
  fprintf(out, ".struct %s\n",name->c_str());
  if (sym->fields != nullptr){
    for (auto itor: *(sym->fields)){
      fprintf (out,".field %s%s\n", 
        parse_typesize(itor.second).c_str(),
        itor.first->c_str());
    }
  }
  fprintf (out, ".end \n");
}

// emit structs
void emit_struct(FILE *out){
  if (symtable::struct_t == nullptr){
    errprintf("3ac: ac_struct called on null struct_table\n");
    ++err_count;
    return;
  }
  for (auto itor: *(symtable::struct_t)){
    translate_struct(itor.first,itor.second,out);  
  }
}
 
// emit string constants here
void emit_string(FILE *out){
  int i = 0;
  for (auto itor: *(three_address_code::all_strings)){
    fprintf (out,".s%d%-7s %s\n",i,":",itor->c_str());
    ++i;
  }
}
 
// emit global definitions here
void emit_globaldef(FILE *out){
  for (auto itor: *(three_address_code::all_globals)){
    string name = itor->t0->str();
    name += ":";
    fprintf (out,"%-10s %s %s",
             name.c_str(),
             ".global",
             parse_typesize(itor->expr).c_str());
    if (itor->t1 !=nullptr){
      fprintf (out,"%s",itor->t1->str().c_str());
    }
    fprintf (out,"\n");
  }
}

void emit_functions(FILE *out){
  // print out all functions
  for (auto itor: *(three_address_code::all_functions)){
    // get symbol associated with function and
    // symbol table associated with functions block
    symbol *type = symtable::global->find(itor.first)->second;
    symbol_table *block = symtable::master->find(itor.first)->second;
    
    // function name
    string fname;
    fname.append(itor.first->c_str());
    fname.append(":");
     
    //print out the function header    
    fprintf(out,"%-10s .function %s\n",
            fname.c_str(), 
            parse_typesize(type).c_str()); 
     
    //print out the function params and variables
    string f_vlabel = ".param";
    for (size_t i = 0; i < block->size(); i++){
      for (auto itor2: *block){
         // params should come before variables by design, 
            // so only check for when the transition arrives
         if (itor2.second->sequence == i){
           if (itor2.second->attributes[static_cast<int>(attr::LOCAL)]){
             f_vlabel = ".local";
           }
           fprintf (out,"%-10s %s %s%s\n",
                    "",
                    f_vlabel.c_str(),
                    parse_typesize(itor2.second).c_str(),
                    itor2.first->c_str());
           continue;
         }
      }
    }
    // fetch statements associated with function
    statement_table *found;
    if (three_address_code::table_lookup->count(block))
      found = three_address_code::table_lookup->find(block)->second;
    else{
      errprintf ("3ac: statement table not found for function %s\n",fname.c_str());
      ++err_count;
      continue;
    }
    // print them
    for (auto stmt: *found){
      if (stmt->itype[static_cast<int>(instruction::ASSIGNMENT)]){
          // [LABEL] T0 = T1 OPERATOR T2
          // = only appears if t1 exists
          fprintf(out,"%-10s %s%s%s %s %s\n",
                  stmt->label ? stmt->label->c_str() : "",
                  stmt->t0 ? stmt->t0->str().c_str() : "" ,
                  stmt->t1 && stmt->t0 ? " = " : "",
                  stmt->t1 ? stmt->t1->str().c_str() : "",
                  stmt->op ? stmt->op->c_str() : "",
                  stmt->t2 ? stmt->t2->str().c_str() : "");
      }
      if (stmt->itype[static_cast<int>(instruction::GOTO)]){
        // [LABEL] goto condition if t1 op t2
        // if not appears only if t0 exists
        fprintf (out,"%-10s goto %s%s%s %s %s\n",
                 stmt->label ? stmt->label->c_str() : "",
		 stmt->condition ? stmt->condition->c_str() : "",
                 stmt->t1 && stmt->condition ? " if " : "",
                 stmt->t1 ? stmt->t1->str().c_str() : "", 
                 stmt->op ? stmt->op->c_str() : "", 
                 stmt->t2 ? stmt->t2->str().c_str() : "");
      }
      if (stmt->itype[static_cast<int>(instruction::LABEL_ONLY)]){
        // [LABEL]
        fprintf (out,"%s\n", stmt->label ? stmt->label->c_str() : "");
      }
      if (stmt->itype[static_cast<int>(instruction::RETURN)]){
        // return [EXPR]
        fprintf (out, "%-10s return %s\n",
             stmt->label ? stmt->label->c_str() : "",
             stmt->t0 ? stmt->t0->str().c_str(): "" );
      }
    }
    //print out the ending statements
    fprintf (out,"%-10s return\n","");
    fprintf (out,"%-10s .end\n","");
  }
}

void ac_traverse(astree *s){
  for (astree *child: s->children){
    if (child->symbol == TOK_TYPE_ID){
      // process the global variable
      ac_globalvar(child);
    }
    // else process the function
    // ensure the function is valid, and not a prototype
    else if (child->symbol == TOK_FUNCTION 
             && !(child->attributes[static_cast<int>(attr::PROTOTYPE)])
             && symtable::global->count(child->children[0]->children[1]->lexinfo)){
      astree *name = child->children[0]->children[1];
      symbol_table *found;
      // find symbol table associated with function
      if (symtable::master->find(name->lexinfo) != symtable::master->end()){
        found = symtable::master->find(name->lexinfo)->second;
      } 
      else {
        errprintf ("3ac: invalid function definition. Stopping address code generation \n");
        ++err_count;
        return;
      }
      statement_table *new_function = new statement_table;
      three_address_code::table_lookup->emplace(found,new_function);
      three_address_code::all_functions->push_back(make_pair(name->lexinfo,new_function));
      reg_count = 0;
      p_stmt(child->children[2],new_function,nullptr);
    }
  }
}

void emit_all3ac(FILE *out){
  emit_struct(out);
  emit_string(out);
  emit_globaldef(out);
  emit_functions(out);
}

int generate_3ac(){
  if (!parser::root || !symtable::master || !symtable::global || !symtable::struct_t){
    errprintf ("parse failed, or invalid table, stopping generation of assembly\n");
    ++err_count;
    return 1;
  }
  three_address_code::table_lookup = new unordered_map<symbol_table*, statement_table*>;
  three_address_code::all_strings  = new vector <const string*>();
  three_address_code::all_globals  = new statement_table; 
  three_address_code::all_functions = new vector<pair<const string*,statement_table*>>();
  // link global symbol table with global statement table
  three_address_code::table_lookup->emplace(symtable::global,three_address_code::all_globals);
  ac_traverse(parser::root);
  if (err_count){
    errprintf ("NUMBER OF ERRORS REPORTED: %d\n",err_count);
    return 1;
  }
  return 0;
}
