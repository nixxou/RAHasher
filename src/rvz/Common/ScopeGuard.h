// Shim for Dolphin's Common/ScopeGuard.h — runs a callable at scope exit unless Dismiss()ed.
// Usage in ported code: Common::ScopeGuard guard([&]{ ... });  (relies on CTAD)
#pragma once
#include <utility>

namespace Common
{
template <typename F>
class ScopeGuard
{
public:
  explicit ScopeGuard(F func) : m_func(std::move(func)) {}
  ~ScopeGuard() { if (m_active) m_func(); }
  void Dismiss() { m_active = false; }

  ScopeGuard(ScopeGuard&& other) noexcept : m_func(std::move(other.m_func)), m_active(other.m_active)
  {
    other.m_active = false;
  }
  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
  ScopeGuard& operator=(ScopeGuard&&) = delete;

private:
  F m_func;
  bool m_active = true;
};

template <typename F>
ScopeGuard(F) -> ScopeGuard<F>;
}  // namespace Common
