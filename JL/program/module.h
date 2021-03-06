/*
 * module.h
 *
 *  Created on: Jan 1, 2017
 *      Author: Dad
 */

#ifndef PROGRAM_MODULE_H_
#define PROGRAM_MODULE_H_

#include <stdint.h>

#include "../codegen/tokenizer.h"
#include "../datastructure/map.h"
#include "../error.h"
#include "instruction.h"
#include "tape.h"

typedef struct Module_ Module;

Module *module_create(FileInfo *fi);
Module *module_create_tape(FileInfo *fi, Tape *tape);
const char *module_filename(const Module const *m);
void module_set_filename(Module *m, const char fn[]);

// const char *module_name(const Module const *m);
DEB_FN(const char *, module_name, const Module const *m);
#define module_name(...) CALL_FN(module_name__, __VA_ARGS__)

FileInfo *module_fileinfo(const Module const *m);
// void module_load(Module *m);
Ins module_ins(const Module *m, uint32_t index);
const InsContainer *module_insc(const Module *m, uint32_t index);
const Map *module_refs(const Module *m);
// Set *module_literals(const Module *m);
// Set *module_vars(const Module *m);
// Map *module_refs(const Module *m);
const Map *module_classes(const Module *m);
const Map *module_class_parents(const Module *m);
int32_t module_ref(const Module *m, const char ref_name[]);
const Map *module_fn_args(const Module *m);
uint32_t module_size(const Module *m);
const Tape *module_tape(const Module *m);
void module_set_tape(Module *m, Tape *tape);

typedef void (*ClassAction)(const char *class_name, const Map *methods);
void module_iterate_classes(const Module *m, ClassAction action);

void module_delete(Module *module);

#endif /* PROGRAM_MODULE_H_ */
