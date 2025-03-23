#include <lix/libexpr/get-drvs.hh>
#include <lix/libexpr/eval.hh>
#include <string>
#include <map>
#include <set>
#include <string>
#include <optional>

#include "eval-args.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/json.hh"

class MyArgs;

namespace nix {
class EvalState;
struct DrvInfo;
} // namespace nix

struct Constituents {
    std::vector<std::string> constituents;
    std::vector<std::string> namedConstituents;
    Constituents(std::vector<std::string> constituents,
                 std::vector<std::string> namedConstituents)
        : constituents(constituents), namedConstituents(namedConstituents) {};
};

/* The fields of a derivation that are printed in json form */
struct Drv {
    std::string name;
    std::string system;
    std::string drvPath;

    enum class CacheStatus { Cached, Uncached, Unknown } cacheStatus;
    std::map<std::string, std::optional<std::string>> outputs;
    std::map<std::string, std::set<std::string>> inputDrvs;
    std::optional<nix::JSON> meta;
    std::optional<Constituents> constituents;

    Drv(std::string &attrPath, nix::EvalState &state, nix::DrvInfo &drvInfo,
        MyArgs &args, std::optional<Constituents> constituents);
};
void to_json(nix::JSON &json, const Drv &drv);

void register_gc_root(nix::Path &gcRootsDir, std::string &drvPath,
                      const nix::ref<nix::Store> &store, nix::AsyncIoRoot &aio);
