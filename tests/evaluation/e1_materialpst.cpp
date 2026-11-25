#include <chess_lab/chess_core.hpp>
#include <chess_lab/evaluation.hpp>
#include "q_pruning_policy.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace chess_lab;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void policy(const EvaluationIdentity &id, unsigned expected) {
  auto p=detail::q_pruning_policy(id);
  check(p.id==expected && p.see_enabled==(expected!=0) && p.delta_enabled==(expected!=0)
        && p.see_cutoff_cp==0 && p.delta_margin_cp==0, "quiescence policy mismatch");
}
int main(int argc,char **argv) {
  try {
#ifdef __EMSCRIPTEN__
    check(argc==2,"expected encompassing WASM hash");
    const std::string sha=argv[1];
    auto material=EvaluatorModule::bundled("material-v2",sha);
    auto pst=EvaluatorModule::bundled("material-pst-v1",sha);
#else
    check(argc==3,"expected material and PST module paths");
    const std::string sha(64,'a'); // Native independently linked bundled-provider test fixture.
    auto material=EvaluatorModule::native(argv[1]);
    auto pst=EvaluatorModule::native(argv[2]);
#endif
    auto a=material.create("{}"); auto c=pst.create("{}");
    auto default_module=EvaluatorModule::bundled(sha);
    auto default_instance=default_module.create("{}");
    auto named=EvaluatorModule::bundled("material-pst-v1",sha).create("{}");
    auto legacy=EvaluatorModule::bundled("material-v2",sha).create("{}");
    check(default_instance->identity().fingerprint==legacy->identity().fingerprint,"default bundled alias identity");
    check(default_instance->identity().fingerprint!=named->identity().fingerprint,"material provider distinction");
    policy(default_instance->identity(),17); policy(legacy->identity(),17); policy(named->identity(),18);
    policy(a->identity(),17); policy(c->identity(),18);
    policy(material.create("{\"pawn_cp\":101}")->identity(),0);
    auto id=c->identity(); id.module_version="2.0.0"; policy(id,0);
    id=c->identity(); id.abi_major=1; policy(id,0);
    id=c->identity(); id.canonical_configuration="{\"x\":1}"; policy(id,0);
    id=c->identity(); id.module_id="other"; policy(id,0);
    bool rejected=false; try { (void)pst.create("{\"unknown\":1}"); } catch (const std::exception &) { rejected=true; }
    check(rejected,"unknown parameter accepted");
    auto core=create_chess_core();
    struct Fixture {const char *fen; int score;};
    for(auto f : {Fixture{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",0},
                   Fixture{"4k3/8/8/8/4P3/8/8/4K3 w - - 0 1",120},
                   Fixture{"4k3/8/8/4p3/8/8/8/4K3 b - - 0 1",-120},
                   Fixture{"4k3/8/8/8/8/8/4P3/4K3 b - - 0 1",80}}) {
      auto pos=core->load_fen(f.fen);
      auto snap=evaluation_snapshot(*core->snapshot(*pos));
      auto original=snap;
      check(c->evaluate(snap)==f.score,"golden PST score");
      check(named->evaluate(snap)==f.score,"named bundled PST parity");
      check(default_instance->evaluate(snap)==a->evaluate(snap),"default bundled material parity");
      check(legacy->evaluate(snap)==a->evaluate(snap),"explicit material provider parity");
      check(std::memcmp(&original,&snap,sizeof snap)==0,"snapshot mutation");
    }
    std::cout<<"E1 C1 registry, ABI module, golden scores and fail-open cases passed\n";
    return 0;
  } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
