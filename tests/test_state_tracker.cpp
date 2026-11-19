#include <cassert>
#include <cstdint>
#include <iostream>
#include "TarsStateTracker.h"

int main() {
  TarsStateTracker s;
  assert(!s.transitionReady(300, 100));
  assert(s.state() == 0);
  assert(!s.transitionReady(2499, 140));
  assert(!s.transitionReady(3500, 200));
  assert(!s.transitionReady(3500, 360));
  assert(s.transitionReady(3500, 400));
  assert(s.pendingState() == 1 && s.state() == 0);
  // A escrita do CSV pode falhar: nao avancamos estado sem confirmacao.
  assert(s.transitionReady(3500, 600));
  s.confirmPersisted();
  assert(s.state() == 1);
  // Histerese: valores intermediarios nao alternam a classificacao.
  assert(!s.transitionReady(1800, 650));
  assert(!s.transitionReady(1500, 700));
  assert(!s.transitionReady(2500, 800)); // rebote cancela a transicao
  assert(!s.transitionReady(1500, 900));
  assert(s.transitionReady(1500, 1100));
  s.confirmPersisted();
  assert(s.state() == 0);

  // Verifica overflow de millis(): diferenca sem sinal deve continuar valida.
  TarsStateTracker wrap;
  assert(!wrap.transitionReady(100, 0xFFFFFF00u));
  assert(!wrap.transitionReady(3500, 0xFFFFFF80u));
  assert(wrap.transitionReady(3500, 0x00000090u));
  wrap.confirmPersisted();
  assert(wrap.state() == 1);
  std::cout << "OK: transicoes 0->1->0, histerese, debounce, retry e overflow\n";
}
