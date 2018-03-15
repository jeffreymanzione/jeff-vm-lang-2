/*
 * instruction.c
 *
 *  Created on: Dec 16, 2016
 *      Author: Jeff
 */

#include "instruction.h"

#include <stdlib.h>
#include <string.h>

#include "error.h"

const char *instructions[] = { "nop", "exit", "res", "tget", "set", "push",
    "peek", "psrs", "not", "notc", "gt", "lt", "eq", "neq", "gte", "lte", "and",
    "or", "xor", "if", "ifn", "jmp", "ret", "add", "sub", "mult", "div", "mod",
    "call", "tupl", "dup", "goto", "prnt", "rmdl", "mcll", "get", "gtsh", "fld",
    "is", "anew", "aidx", "aset", "mdst" };

Op op_type(const char word[]) {
  int i;
  for (i = 0; i < sizeof(instructions) / sizeof(instructions[0]); i++) {
    if (strlen(word) != strlen(instructions[i])) {
      continue;
    }
    if (0 == strncmp(word, instructions[i], 4)) {
      return i;
    }
  }
  ERROR("Unknown instruction type. Was '%s'.", word);
  return NOP;
}

Ins instruction(Op op) {
  Ins ins;
  ins.param = NO_PARAM;
  ins.op = op;
  return ins;
}

Ins instruction_val(Op op, Value val) {
  Ins ins;
  ins.param = VAL_PARAM;
  ins.op = op;
  ins.val = val;
  return ins;
}

Ins instruction_id(Op op, const char id[]) {
  Ins ins;
  ins.param = ID_PARAM;
  ins.op = op;
  ins.id = id;
  return ins;
}

//Ins instruction_goto(Op op, uint32_t go_to) {
//  Ins ins;
//  ins.param = GOTO_PARAM;
//  ins.op = op;
//  ins.go_to = go_to;
//  return ins;
//}

Ins instruction_str(Op op, const char *str) {
  Ins ins;
  ins.param = STR_PARAM;
  ins.op = op;
  ins.str = str;
  return ins;
}

Ins noop_instruction() {
  Ins ins =
      { .op = NOP, .param = NO_PARAM, .val = { .type = INT, .int_val = 0 } };
  return ins;
}

void ins_to_str(Ins ins, FILE *file) {
//  DEBUGF("A");
  fprintf(file, "%d,%d: %s", ins.row, ins.col, instructions[(int) ins.op]);
  fflush(stdout);
  if (ins.param == VAL_PARAM) {
    fprintf(file, " ");
    fflush(stdout);
    val_to_str(ins.val, file);
    fflush(stdout);
  } else if (ins.param == ID_PARAM) {
    fprintf(file, " %s", ins.id);
    fflush(stdout);
//  } else if (ins.param == GOTO_PARAM) {
//    fprintf(file, " adr(%d)", ins.go_to);
  } else if (ins.param == STR_PARAM) {
    fprintf(file, " '%s'", ins.str);
    fflush(stdout);
  }
}

Value token_to_val(Token *tok) {
  ASSERT_NOT_NULL(tok);
  Value val;
  switch (tok->type) {
  case INTEGER:
    val.type = INT;
    val.int_val = (int64_t) strtoll(tok->text, NULL, 10);
    break;
  case FLOATING:
    val.type = FLOAT;
    val.float_val = strtod(tok->text, NULL);
    break;
  default:
    ERROR("Attempted to create a Value from '%s'.", tok->text);
  }
  return val;
}
