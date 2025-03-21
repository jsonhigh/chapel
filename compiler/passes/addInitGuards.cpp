/*
 * Copyright 2020-2025 Hewlett Packard Enterprise Development LP
 * Copyright 2004-2019 Cray Inc.
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//////////////////////////////////////////////////////////////////////////////////
// Add module initialization calls and  guards to the module init functions.
//
// Initially, the module initialization functions contain only the "loose"
// statements found within the body of the corresponding module.
// This pass adds calls to the module initialization functions for
// parent modules as well as modules mentioned in "use" statements within
// the module.  Then, it wraps the entire initialization function in a guard
// to ensure that it is run just once.
//
// This pass must be run after parallel().
//

#include "ModuleSymbol.h"
#include "baseAST.h"
#include "misc.h"
#include "passes.h"

#include "astutil.h"
#include "build.h"
#include "stmt.h"
#include "stringutil.h"
#include "wellknown.h"

#include "PassManager.h"

// ---------- AddInitGuards ----------
// This pass makes the initialization functions idempotent --
// meaning that they can be executed any number of times, but the net
// effect is as if they were only called once.  That is done using the
// idiom:
//
//      /* Assume flag_p is initially false. */
//      if (flag_p) return;
//      flag_p = true;
//      ...     // Rest of initialization code.
//
// The guard code is added only if the initialization function has a
// nontrivial body.
//
//
// This pass also creates the function "chpl__init_preInit()", which
// initializes all of the initialization flags to false.
//
// It also adds code that will print the module names as they are
// being initialized if the config const --printModuleInitOrder is set
// at run time.
//


// TODO GLOBALS theProgram and baseModule
// TODO IDEMPOTENT
// We need a function to drop the initializers into.
FnSymbol* AddInitGuards::getOrCreatePreInitFn() {
  SET_LINENO(baseModule);
  FnSymbol* preInitFn = new FnSymbol(astr("chpl__init_preInit"));
  preInitFn->retType = dtVoid;
  preInitFn->addFlag(FLAG_EXPORT);
  preInitFn->addFlag(FLAG_LOCAL_ARGS);
  preInitFn->addFlag(FLAG_INSERT_LINE_FILE_INFO);
  theProgram->block->insertAtTail(new DefExpr(preInitFn));
  normalize(preInitFn);

  return preInitFn;
}
AddInitGuards::AddInitGuards() {
  preInitFn = getOrCreatePreInitFn();
}

bool AddInitGuards::shouldProcess(ModuleSymbol* mod) {
  if (mod == rootModule)
    return false;

  FnSymbol* fn = toFnSymbol(mod->initFn);
  if (!fn)
    // Sometimes a module parsed on the command line
    // is not actually used, so its initializer is pruned.
    return false;

  // Test if this fn has a nontrivial body.
  BlockStmt* body = fn->body;
  if (body->length() < 1)
    return false;

  return true;
}

void AddInitGuards::process(ModuleSymbol* mod) {
  FnSymbol* init = toFnSymbol(mod->initFn);
  assert(init); // precondition from shouldProcess
  addInitGuard(init, preInitFn);
}

void AddInitGuards::addInitGuard(FnSymbol* fn, FnSymbol* preInitFn) {
    SET_LINENO(fn);
    // The declaration:
    //      var <init_fn_name>_p : bool;
    // is added to the end of the program block.
    const char* init_p = astr(fn->name, "_p"); // Add _p to make it a predicate.
    Symbol* var = new VarSymbol(init_p, dtBool);
    Expr* declExpr = new DefExpr(var);
    theProgram->block->insertAtTail(declExpr);

    // The assignment:
    //      <init_fn_name>_p = false;
    // is added at the end of chpl__init_preInit().
    // This means the module has not yet been initialized.
    Expr* asgnExprFalse = new CallExpr(PRIM_MOVE, var, new SymExpr(gFalse));
    preInitFn->insertBeforeEpilogue(asgnExprFalse);

    // The assignment:
    //      <init_fn_name>_p = true;
    // is added to the start of the module initialization function.
    // This means that the module has been initialized.
    Expr* asgnExprTrue = new CallExpr(PRIM_MOVE, var, new SymExpr(gTrue));
    fn->insertAtHead(asgnExprTrue);

    // Add debugging aid that prints module init order
    addPrintModInitOrder(fn);

    // The guard:
    //      if (<init_fn_name>_p) goto _exit_<init_fn_name>.
    // Precedes everything in the module initialization function,
    // including the assignment we just added.
    LabelSymbol* label = new LabelSymbol(astr("_exit_", fn->name));
    fn->insertIntoEpilogue(new DefExpr(label));
    Expr* gotoExit = new GotoStmt(GOTO_NORMAL, label);
    Expr* ifStmt = new CondStmt(new SymExpr(var), gotoExit);
    fn->insertAtHead(ifStmt);
}

// TODO GLOBALS gPrintModuleInitFn, gModuleInitIndentLevel
//
// Insert code that prints out the name of the module as it is
// initialized.  We do this by inserting a call to printModInitOrder()
// and incrementing the indent level at the beginning of the function
// (but after the guard) and then decrementing the indent level at the
// end of the function.
//
void AddInitGuards::addPrintModInitOrder(FnSymbol* fn) {
  //
  // Only do this if gPrintModuleInitFn exists.  It won't exist when
  // compiling --minimal-modules
  //
  if (gPrintModuleInitFn == NULL)
    return;

  // The function printModuleInitFn() takes 3 arguments:
  //   s1:  the format string "%*s"
  //   s2:  string to be printed
  //   len: length of s2
  // Since no other modules are initialized prior to this
  // PrintModuleInitOrder, we'll be conservative and generate s1 and
  // len here.
  VarSymbol* s1tmp = newTemp("modFormatStr", dtStringC);
  VarSymbol* s2tmp = newTemp("modStr", dtStringC);
  const char* s1 = astr("%*s\\n");
  const char* s2 = astr(fn->getModule()->name);
  int myLen = strlen(s2);
  char lenStr[25];
  snprintf(lenStr, sizeof(lenStr), "%d", myLen);
  Expr *es1 = buildCStringLiteral(s1);
  Expr *es2 = buildCStringLiteral(s2);
  Expr *elen = buildIntLiteral(lenStr);
  CallExpr* s1Init = new CallExpr(PRIM_MOVE, new SymExpr(s1tmp), es1);
  CallExpr* s2Init = new CallExpr(PRIM_MOVE, new SymExpr(s2tmp), es2);
  CallExpr *printModInit = new CallExpr(gPrintModuleInitFn,
                                        new SymExpr(s1tmp),
                                        new SymExpr(s2tmp), elen);

  // += and -+ take ref args, so we must first get a reference to the
  // indent level variable
  VarSymbol* refIndentLevel = newTemp("refIndentLevel",gModuleInitIndentLevel->qualType().toRef());
  CallExpr *getAddr = new CallExpr(PRIM_MOVE,
                                   new SymExpr(refIndentLevel),
                                   new CallExpr(PRIM_ADDR_OF,
                                                new SymExpr(gModuleInitIndentLevel)));
  CallExpr *incIndentLevel = new CallExpr(PRIM_ADD_ASSIGN,
                                          new SymExpr(refIndentLevel),
                                          buildIntLiteral("1"));
  CallExpr *decIndentLevel = new CallExpr(PRIM_SUBTRACT_ASSIGN,
                                          new SymExpr(refIndentLevel),
                                          buildIntLiteral("1"));
  fn->insertAtHead(incIndentLevel);
  fn->insertAtHead(getAddr);
  fn->insertAtHead(printModInit);
  fn->insertAtHead(s2Init);
  fn->insertAtHead(s1Init);
  fn->insertAtHead(new DefExpr(refIndentLevel));
  fn->insertAtHead(new DefExpr(s2tmp));
  fn->insertAtHead(new DefExpr(s1tmp));
  fn->insertBeforeEpilogue(decIndentLevel);
}


// ---------- AddModuleInitBlocks ----------

bool AddModuleInitBlocks::shouldProcess(ModuleSymbol* mod) {
  if (mod == rootModule)
    return false;

  FnSymbol* fn = toFnSymbol(mod->initFn);
  if (!fn) {
    INT_ASSERT(!mod->deinitFn); // otherwise need to reinstate initFn
    // Sometimes a module parsed on the command line
    // is not actually used, so its initializer is pruned during resolution.
    return false;
  }
  return true;
}

// TODO GLOBALS standardModule gAddModuleFn
void AddModuleInitBlocks::process(ModuleSymbol* mod) {
  FnSymbol* fn = toFnSymbol(mod->initFn);
  INT_ASSERT(fn); // precondition from shouldProcess

  SET_LINENO(mod);

  // This needs to go after initBlock: we want addModule(mod)
  // to be called *after* addModule on modules used by mod.
  if (auto deinitFn = mod->deinitFn) {

    // Compute the type now since it may not have been set yet.
    deinitFn->computeAndSetType();

    // Manually cast to 'dtCFnPtr' since we are post-resolution.
    auto cast = new CallExpr(PRIM_CAST_TO_TYPE,
                             new SymExpr(mod->deinitFn),
                             new SymExpr(dtCFnPtr->symbol));

    fn->insertAtHead(new CallExpr(gAddModuleFn,
                                  buildCStringLiteral(mod->name),
                                  cast));
  }

  BlockStmt* initBlock = new BlockStmt();

  // If I have a parent, I need it initialized first,
  // since all of its symbols are visible to me.
  if (ModuleSymbol* parent = mod->defPoint->getModule())
    // The initializer for theProgram is called specially in main.c,
    // so we don't have to call it here.
    if (parent != theProgram && parent != rootModule)
      initBlock->insertAtTail(new CallExpr(parent->initFn));

  // Call the initializer for each module I use.
  for (ModuleSymbol* usedMod : mod->modUseList) {
    if (usedMod != standardModule) {
      initBlock->insertAtTail(new CallExpr(usedMod->initFn));
    }
  }

  // TODO REFACTOR Q can't we check this before and only create all the above if its true?
  if (initBlock->body.length > 0)
    fn->insertAtHead(initBlock);
}

#include "global-ast-vecs.h"

void addInitCalls() {
  PassManager pm;

  PassTList<ModuleSymbol*> passes;
  passes.push_back(std::make_unique<AddModuleInitBlocks>());
  passes.push_back(std::make_unique<AddInitGuards>());
  // TODO these are good candidates for runPassChained
  pm.runPass(std::move(passes), gModuleSymbols);
}
