#include "lower.h"

#include "storage.h"
#include "ir_builder.h"
#include "ir_rewriter.h"
#include "ir_codegen.h"
#include "inline.h"

using namespace std;

namespace simit {
namespace ir {

inline bool hasStorage(std::vector<Var> vars, const Storage &storage) {
  for (auto &var : vars) {
    if (!storage.hasStorage(var)) return false;
  }
  return true;
}

inline bool hasSameStorage(std::vector<Var> vars, const Storage &storage) {
  if (vars.size() == 0) return true;
  TensorStorage::Kind firstStorage = storage.get(vars[0]).getKind();
  for (auto &var : vars) {
    if (storage.get(var).getKind() != firstStorage) return false;
  }
  return true;
}

class LowerMapFunctionRewriter : public MapFunctionRewriter {
  using MapFunctionRewriter::visit;

  virtual void visit(const TensorWrite *op) {
    // Rewrites the tensor write and assigns the result to stmt
    IRRewriter::visit(op);
    iassert(isa<TensorWrite>(stmt));
    if (isa<VarExpr>(op->tensor) && isResult(to<VarExpr>(op->tensor)->var)) {
      const TensorWrite *tensorWrite = to<TensorWrite>(stmt);
      iassert(tensorWrite->value.type().isTensor());

      // Change assignments to result to compound assignments, using the map
      // reduction operator.
      stmt = TensorWrite::make(tensorWrite->tensor, tensorWrite->indices,
                               tensorWrite->value, CompoundOperator::Add);
    }
  }
};

class LowerMaps : public IRRewriter {
public:
  LowerMaps(const Storage &storage) : storage(storage) {}

private:
  Storage storage;

  void visit(const Map *op) {
    iassert(hasStorage(op->vars, storage))
        << "Every assembled tensor should have a storage descriptor";
    tassert(hasSameStorage(op->vars, storage))
        << "All assembled tensors in the same Map must have the same storage.";

    TensorStorage::Kind tensorStorage = storage.get(op->vars[0]).getKind();
    if (tensorStorage != TensorStorage::SystemNone || op->vars.size() == 0) {
      if (tensorStorage == TensorStorage::SystemReduced ||
          tensorStorage == TensorStorage::DenseRowMajor) {
        LowerMapFunctionRewriter mapFunctionRewriter;
        stmt = inlineMap(op, mapFunctionRewriter);
      }
      else {
        ierror << "Unsupported tensor storage lowering";
      }
    }
    else {
      stmt = op;
    }
  }
};

Func lowerMaps(Func func) {
  Stmt body = LowerMaps(func.getStorage()).rewrite(func.getBody());
  return Func(func, body);
}

}}
