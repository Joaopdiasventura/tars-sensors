#ifndef TARS_STATE_TRACKER_H
#define TARS_STATE_TRACKER_H

#include <stdint.h>

// Transicoes logicas 0/1 com histerese e debounce.
// Sem dependencias Arduino: a maquina de estados pode ser testada no computador.
class TarsStateTracker {
public:
  static const uint16_t ADC_LIMIAR_SUBIDA = 2500;
  static const uint16_t ADC_LIMIAR_DESCIDA = 1600;
  static const uint16_t ADC_LIMIAR_INICIAL = 2048;
  static const uint32_t DEBOUNCE_MS = 200;

  TarsStateTracker() : initialized_(false), stable_(0), candidate_(0), candidateSince_(0) {}

  void begin(uint16_t adc, uint32_t nowMs) {
    stable_ = adc >= ADC_LIMIAR_INICIAL ? 1 : 0;
    candidate_ = stable_;
    candidateSince_ = nowMs;
    initialized_ = true;
  }

  // Retorna true enquanto houver uma transicao estavel pendente de persistencia.
  // A alteracao so e confirmada apos o arquivo CSV ser gravado com sucesso.
  bool transitionReady(uint16_t adc, uint32_t nowMs) {
    if (!initialized_) {
      begin(adc, nowMs);
      return false;
    }

    int observed = stable_;
    if (stable_ == 0 && adc >= ADC_LIMIAR_SUBIDA) {
      observed = 1;
    } else if (stable_ == 1 && adc <= ADC_LIMIAR_DESCIDA) {
      observed = 0;
    }

    if (observed != candidate_) {
      candidate_ = observed;
      candidateSince_ = nowMs;
    }

    return candidate_ != stable_ &&
           static_cast<uint32_t>(nowMs - candidateSince_) >= DEBOUNCE_MS;
  }

  void confirmPersisted() {
    if (initialized_) {
      stable_ = candidate_;
    }
  }

  int state() const { return stable_; }
  int pendingState() const { return candidate_; }
  bool initialized() const { return initialized_; }

private:
  bool initialized_;
  int stable_;
  int candidate_;
  uint32_t candidateSince_;
};

#endif
