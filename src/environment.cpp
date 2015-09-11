#include "environment.h"

#include "var.h"
#include "ir.h"
#include "tensor_index.h"
#include "util/collections.h"

using namespace std;

namespace simit {
namespace ir {

// class VarMapping
std::ostream& operator<<(std::ostream& os, const VarMapping& vm) {
  os << vm.getVar();
  if (vm.getMappings().size() > 0) {
    os << " -> " << util::join(vm.getMappings());
  }
  return os;
}

// class Environment
struct Environment::Content {
  vector<pair<Var, Expr>> constants;
  vector<VarMapping>      externs;
  vector<VarMapping>      temporaries;

  map<string, size_t>     externLocationByName;
  map<Var, size_t>        temporaryLocationByName;

  map<Var, TensorIndex>   tensorIndices;
};

Environment::Environment() : content(new Content) {
}

Environment::~Environment() {
  delete content;
}

Environment::Environment(const Environment& other) : Environment() {
  *content = *other.content;
}

Environment::Environment(Environment&& other) noexcept : content(other.content) {
  other.content = nullptr;
}

Environment& Environment::operator=(const Environment& other) {
  if (&other == this) {
    return *this;
  }
  Environment tmp(other);
  *this = std::move(tmp);
  return *this;
}

Environment& Environment::operator=(Environment&& other) noexcept {
  std::swap(content, other.content);
  return *this;
}

bool Environment::isEmpty() const {
  return content->constants.size() == 0 && content->externs.size() == 0 &&
         content->temporaries.size() == 0;
}

const std::vector<std::pair<Var, Expr>>& Environment::getConstants() const {
  return content->constants;
}

const std::vector<VarMapping>& Environment::getExterns() const {
  return content->externs;
}

bool Environment::hasExtern(const std::string& name) const {
  return util::contains(content->externLocationByName, name);
}

const VarMapping& Environment::getExtern(const std::string& name) const {
  iassert(hasExtern(name));
  return content->externs[content->externLocationByName.at(name)];
}

std::vector<Var> Environment::getExternVars() const {
  vector<Var> externVars;
  set<Var> included;
  for (const VarMapping& externMapping : getExterns()) {
    if (externMapping.getMappings().size() == 0) {
      const Var& ext = externMapping.getVar();
      iassert(!util::contains(included, ext));
      externVars.push_back(ext);
      included.insert(ext);
    }
    else {
      for (const Var& ext : externMapping.getMappings()) {
        if (!util::contains(included, ext)) {
          externVars.push_back(ext);
          included.insert(ext);
        }
      }
    }
  }
  return externVars;
}

const std::vector<VarMapping>& Environment::getTemporaries() const {
  return content->temporaries;
}

bool Environment::hasTemporary(const Var& var) const {
  return util::contains(content->temporaryLocationByName, var);
}

std::vector<Var> Environment::getTemporaryVars() const {
  vector<Var> temporaryVars;
  set<Var> included;
  for (const VarMapping& temporaryMapping : getTemporaries()) {
    if (temporaryMapping.getMappings().size() == 0) {
      const Var& ext = temporaryMapping.getVar();
      iassert(!util::contains(included, ext));
      temporaryVars.push_back(ext);
      included.insert(ext);
    }
    else {
      for (const Var& ext : temporaryMapping.getMappings()) {
        if (!util::contains(included, ext)) {
          temporaryVars.push_back(ext);
          included.insert(ext);
        }
      }
    }
  }
  return temporaryVars;
}

const TensorIndex& Environment::getTensorIndex(const Var& tensor,
                                               unsigned sourceDim,
                                               unsigned sinkDim) {
  iassert(tensor.getType().isTensor()) << "Only tensors have tensor indices";
  tassert(sourceDim == 0 && sinkDim == 1)
      << "Only currently support row->col indices";

  if (!util::contains(content->tensorIndices, tensor)) {
    string name = tensor.getName() + "_rows2cols";

    Var coordsArray(name+"_coords", ArrayType::make(ScalarType::Int));
    Var sinksArray(name+"_sinks",   ArrayType::make(ScalarType::Int));

    TensorIndex ti(tensor, coordsArray, sinksArray, sourceDim, sinkDim);
    addTensorIndex(tensor, ti);
  }
  return content->tensorIndices.at(tensor);
}

void Environment::addConstant(const Var& var, const Expr& initializer) {
  content->constants.push_back({var, initializer});
}

void Environment::addExtern(const Var& var) {
  iassert(!hasExtern(var.getName())) << var << " already in environment";

  content->externs.push_back(var);
  size_t loc = content->externs.size()-1;
  content->externLocationByName.insert({var.getName(), loc});

  // TODO: Change so that variables are not mapped to themselves. This means
  //       lowering must map (sparse/dense) tensor value storage to arrays.
  addExternMapping(var, var);
}

void Environment::addExternMapping(const Var& var, const Var& mapping) {
  iassert(hasExtern(var.getName()));
  size_t loc = content->externLocationByName.at(var.getName());
  content->externs.at(loc).addMapping(mapping);
}

void Environment::addTemporary(const Var& var) {
  iassert(!hasExtern(var.getName())) << var << " already in environment";

  content->temporaries.push_back(var);
  size_t loc = content->temporaries.size()-1 ;
  content->temporaryLocationByName.insert({var, loc});

  // TODO: Change so that variables are not mapped to themselves. This means
  //       lowering must map (sparse/dense) tensor value storage to arrays.
  addTemporaryMapping(var, var);
}

void Environment::addTemporaryMapping(const Var& var, const Var& mapping) {
  iassert(hasTemporary(var));
  size_t loc =content->temporaryLocationByName.at(var);
  content->temporaries.at(loc).addMapping(mapping);
}

void Environment::addTensorIndex(Var tensor, TensorIndex ti) {
  iassert(hasExtern(tensor.getName()) || hasTemporary(tensor))
      << tensor << " is not an extern or temporary";

  if (hasExtern(tensor.getName())) {
    addExternMapping(tensor, ti.getCoordsArray());
    addExternMapping(tensor, ti.getSinksArray());
  }
  else {
    addTemporary(ti.getCoordsArray());
    addTemporary(ti.getSinksArray());
  }

  content->tensorIndices.insert({tensor, ti});
}

std::ostream& operator<<(std::ostream& os, const Environment& env) {
  bool somethingPrinted = false;

  if (env.getConstants().size() > 0) {
    for (auto& con : env.getConstants()) {
      os << "const " << con.first.getName() << " : " << con.first.getType()
         << " = " << con.second << ";" << std::endl;
    }

    os << std::endl << "const " << env.getConstants().begin()->first << " : "
       << env.getConstants().begin()->first.getType()
       << " = " << env.getConstants().begin()->second << ";";
    for (auto& con : util::excludeFirst(env.getConstants())) {
      os << std::endl << "const " << con.first.getName() << " : "
         << con.first.getType() << " = " << con.second << ";";
    }
    somethingPrinted = true;
  }

  if (env.getExterns().size() > 0) {
    if (somethingPrinted) {
      os << std::endl;
    }
    auto externVars = env.getExternVars();
    os << "extern " << *externVars.begin()  << " : "
       << externVars.begin()->getType() << ";";
    for (auto& ext : util::excludeFirst(externVars)) {
      os << std::endl << "extern " << ext  << " : " << ext.getType() << ";";
    }
    somethingPrinted = true;
  }

  if (env.getTemporaries().size() > 0) {
    if (somethingPrinted) {
      os << std::endl;
    }
    auto temporaryVars = env.getTemporaryVars();
    os << "temp " << *temporaryVars.begin()  << " : "
       << temporaryVars.begin()->getType() << ";";
    for (auto& temp : util::excludeFirst(temporaryVars)) {
      os << std::endl << "temp " << temp  << " : " << temp.getType() << ";";
    }
  }
  return os;
}

}}