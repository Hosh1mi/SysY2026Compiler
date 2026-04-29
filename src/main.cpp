#include "../include/frontend/ast/ast.hpp"
#include "../include/frontend/ast/astPrinter.hpp"
#include "../include/mid/ir/irGen.hpp"
#include "../include/backend/arm/arm.hpp"
#include "../include/frontend/checker/checker.hpp"
#include <fstream>
#include <iostream>
#include <ostream>
#include <unistd.h>

#include "../build/parser.hpp"

extern unique_ptr<CompUnitAST> root;
extern int yyparse();
extern FILE *yyin;

int main(int argc, char **argv) {
  // Assert the number of arguments
  assert(argc >= 2);

  // TODO: advanced argument parser
  char *filename = nullptr;
  int print_ir = false;
  int print_asm = true;

  std::string output = "-";

  int opt;
  bool isO2 = false;
  while ((opt = getopt(argc, argv, "Sco:O::")) != -1) {
    switch (opt) {
    case 'S':
      print_asm = true;
      print_ir = false;
      break;
    case 'c':
      print_ir = true;
      print_asm = false;
      break;
    case 'o':
      output = optarg;
      break;
    case 'O':
      isO2 = true;
      break;
    default:
      break;
    }
  }
  filename = argv[optind];

  yyin = fopen(filename, "r");
  if (yyin == nullptr) {
    std::cout << "yyin open" << filename << "failed" << std::endl;
    return -1;
  }

  // Frontend parser
  yyparse();

#ifdef DEBUG_PARSER
  std::cerr << "========== Parser Result ==========" << std::endl;
  if (root) {
    Printer printer;
    std::cerr << printer.visit(*root) << std::endl;
  } else {
    std::cerr << "Parser output: failed to construct AST root." << std::endl;
  }
  std::cerr << "===================================" << std::endl;
#endif

  // Frontend checker
  ErrorReporter errorReporter(std::cerr);
  Checker checker(errorReporter);
  checker.visit(*root);
  // std::cout << "Check Completed." << std::endl;

  // Generate IR from AST
  GenIR genIR;
  root->accept(genIR);
  std::unique_ptr<Module> m = genIR.getModule();
#ifdef DEBUG_IRGEN
  std::cerr << "========== IRGenerator Result ==========" << std::endl;
  if (m) {
    std::cerr << m->print() << std::endl;
  } else {
    std::cerr << "IR generation output: module is empty." << std::endl;
  }
  std::cerr << "========================================" << std::endl;
#endif

//  // Run IR optimization
//  // TODO
//  if (isO2) {
//    std::vector<Optimization *> Opt;
//    Opt.push_back(new DeadCodeDeletion(m.get()));
//    Opt.push_back(new ConstSpread(m.get()));
//    Opt.push_back(new CombineInstr(m.get()));
//    Opt.push_back(new DomainTree(m.get()));
//    Opt.push_back(new SimplifyJump(m.get()));
//    Opt.push_back(new LoopInvariant(m.get()));
//    Opt.push_back(new SimplifyJump(m.get()));
//    for (auto x : Opt)
//      x->execute();
//  }
//
  std::ofstream fout;
  std::ostream *out = &std::cout;
  if (output != "-") {
    fout.open(output);
    if (!fout.is_open()) {
      std::cerr << "failed to open output file: " << output << std::endl;
      return -1;
    }
    out = &fout;
  }

  if (print_asm) {
    ArmBuilder builder;
    const std::string arm_code = builder.build(m.get());
    *out << arm_code << std::endl;
  } else if (print_ir) {
    *out << m->print() << std::endl;
  }
  return 0;
}
