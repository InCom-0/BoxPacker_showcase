#pragma once


#include <exec/async_scope.hpp>
#include <exec/execute.hpp>
#include <exec/repeat_n.hpp>
#include <exec/repeat_until.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/task.hpp>
#include <exec/unless_stop_requested.hpp>
#include <stdexec/execution.hpp>

namespace incom::standard::async {

struct Separator {};

// Tail should include the argument that will be passed to the CORO
// 1) The CORO (a Lambda type) should return an awaitable
// 2) CORO should take the argument in 'tail...' as arguments to its call operator
// 3) One can use 'Separator' tag type.
// 3a) The 'tail' arguments before separator get passed in directly
// 3b) The 'tail' argument after separator get forwarded into 'm_qs' and then those get 'unpacked' as CORO arguments
// 3c) This is good for 'shared state' between the caller context and the async coroutine (eg. the messaging queues)
template <typename CORO, typename... Tail>
auto spawn(CORO, Tail &&...tail);

namespace detail {
template <typename RT, typename... Qs>
class Job_PRE {
public:
    exec::async_scope m_ascope = {};
    std::tuple<Qs...> m_qs;
    RT                m_ret;

    Job_PRE()                          = delete;
    Job_PRE(Job_PRE &&)                = delete;
    Job_PRE(Job_PRE &)                 = delete;
    Job_PRE(Job_PRE const &)           = delete;
    Job_PRE operator=(Job_PRE &)       = delete;
    Job_PRE operator=(Job_PRE const &) = delete;

    ~Job_PRE() = default;

    template <typename CORO, typename... Tail>
    friend auto incom::standard::async::spawn(CORO, Tail &&...tail);

private:
    template <typename CORO, typename... Tail, std::size_t... I1, std::size_t... I2>
    explicit Job_PRE(CORO, std::index_sequence<I1...>, std::index_sequence<I2...>, Tail &&...tail)
        : m_qs(std::forward<Tail...[sizeof...(I1) + 1 + I2]>(tail...[sizeof...(I1) + 1 + I2])...),
          m_ret(m_ascope.spawn_future(CORO{}(tail...[I1]..., std::get<I2>(m_qs)...))) {}
};

template <class F, class... Args>
using lambda_call_return_t = std::invoke_result_t<F &, Args...>;

template <class Sender>
using scope_future_t = decltype(std::declval<exec::async_scope &>().spawn_future(std::declval<Sender>()));

// Deduction guide
template <typename CORO, typename... Tail, std::size_t... I1, std::size_t... I2>
Job_PRE(CORO, std::index_sequence<I1...>, std::index_sequence<I2...>, Tail &&...tail)
    -> Job_PRE<scope_future_t<lambda_call_return_t<CORO, Tail...[I1]...,
                                                   std::remove_cvref_t<Tail...[sizeof...(I1) + 1 + I2]> &...>>,
               std::remove_cvref_t<Tail...[sizeof...(I1) + 1 + I2]>...>;

} // namespace detail


template <typename CORO, typename... Tail>
auto spawn(CORO, Tail &&...tail) {

    constexpr std::size_t count = (0u + ... + (std::is_same_v<std::remove_cvref_t<Tail>, Separator> ? 1u : 0u));
    static_assert(count < 2, "Pass one or zero Job_PRE::Separator{}.");

    constexpr std::size_t sep = []() -> std::size_t {
        constexpr bool is_sep[] = {std::is_same_v<std::remove_cvref_t<Tail>, Separator>...};
        for (std::size_t i = 0; i < sizeof...(Tail); ++i) {
            if (is_sep[i]) { return i; }
        }
        return sizeof...(Tail);
    }();

    return detail::Job_PRE(CORO{}, std::make_index_sequence<sep>{},
                           std::make_index_sequence<count == 0 ? 0 : sizeof...(Tail) - sep - 1>{},
                           std::forward<Tail>(tail)...);
}

} // namespace incom::standard::async