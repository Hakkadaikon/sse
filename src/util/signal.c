#include "./signal.h"

#include "../arch/sigaction.h"
#include "allocator.h"

static volatile bool rise_signal = false;

static void _signal_handler(int32_t signum);
bool        _is_rise_signal();
bool        _signal_init();

bool _signal_init()
{
  struct sigaction sa;
  _memset_s(&sa, sizeof(sa), 0x00, sizeof(sa));
  sa.sa_handler = _signal_handler;
  internal_sigemptyset(&sa.sa_mask);

  int32_t signals[] = {SIGHUP, SIGINT, SIGTERM};

  for (int32_t i = 0; i < (sizeof(signals) / sizeof(signals[0])); i++) {
    if (internal_sigaction(signals[i], &sa, (void*)0) == -1) {
      return false;
    }
  }

  return true;
}

static void _signal_handler(int32_t signum)
{
  rise_signal = true;
}

bool _is_rise_signal()
{
  return rise_signal;
}
