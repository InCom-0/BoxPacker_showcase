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
template <typename CORO, typename... Tail>
auto spawn_uptr(CORO, Tail &&...tail);


namespace detail {
template <typename... Tail>
inline constexpr std::size_t separator_count_v =
    (0u + ... + (std::is_same_v<std::remove_cvref_t<Tail>, Separator> ? 1u : 0u));

template <typename... Tail>
inline constexpr std::size_t separator_index_v = []() -> std::size_t {
    constexpr bool is_sep[] = {std::is_same_v<std::remove_cvref_t<Tail>, Separator>...};
    for (std::size_t i = 0; i < sizeof...(Tail); ++i) {
        if (is_sep[i]) { return i; }
    }
    return sizeof...(Tail);
}();

template <typename... Tail>
inline constexpr std::size_t queue_arg_count_v =
    separator_count_v<Tail...> == 0 ? 0 : sizeof...(Tail) - separator_index_v<Tail...> - 1;

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
    template <typename CORO, typename... Tail>
    friend auto incom::standard::async::spawn_uptr(CORO, Tail &&...tail);

    template <typename CORO, typename... Tail>
    explicit Job_PRE(CORO, Tail &&...tail)
        : Job_PRE(CORO{}, std::make_index_sequence<separator_index_v<Tail...>>{},
                  std::make_index_sequence<queue_arg_count_v<Tail...>>{}, std::forward<Tail>(tail)...) {
        static_assert(separator_count_v<Tail...> < 2, "Pass one or zero Job_PRE::Separator{}.");
    }


private:
    template <typename CORO, typename... Tail, std::size_t... I1, std::size_t... I2>
    explicit Job_PRE(CORO, std::index_sequence<I1...>, std::index_sequence<I2...>, Tail &&...tail)
        : m_qs(std::forward<Tail...[sizeof...(I1) + 1 + I2]>(tail...[sizeof...(I1) + 1 + I2])...),
          m_ret(m_ascope.spawn_future(CORO{}(tail...[I1]..., std::get<I2>(m_qs)...))) {}
};

// ############################################
// Deduction and helper machinery
// ############################################
template <class F, class... Args>
using lambda_call_return_t = std::invoke_result_t<F &, Args...>;

template <class Sender>
using scope_future_t = decltype(std::declval<exec::async_scope &>().spawn_future(std::declval<Sender>()));

template <typename CORO, typename I1Seq, typename I2Seq, typename... Tail>
struct Job_PRE_Deduction;

template <typename CORO, std::size_t... I1, std::size_t... I2, typename... Tail>
struct Job_PRE_Deduction<CORO, std::index_sequence<I1...>, std::index_sequence<I2...>, Tail...> {
    using type =
        Job_PRE<scope_future_t<lambda_call_return_t<CORO, Tail...[I1]...,
                                                    std::remove_cvref_t<Tail...[sizeof...(I1) + 1 + I2]> &...>>,
                std::remove_cvref_t<Tail...[sizeof...(I1) + 1 + I2]>...>;
};

template <typename CORO, typename... Tail>
using job_pre_from_factory =
    typename Job_PRE_Deduction<CORO, std::make_index_sequence<separator_index_v<Tail...>>,
                               std::make_index_sequence<queue_arg_count_v<Tail...>>, Tail...>::type;

// Deduction guide
template <typename CORO, typename... Tail, std::size_t... I1, std::size_t... I2>
Job_PRE(CORO, std::index_sequence<I1...>, std::index_sequence<I2...>, Tail &&...tail)
    -> Job_PRE<scope_future_t<lambda_call_return_t<CORO, Tail...[I1]...,
                                                   std::remove_cvref_t<Tail...[sizeof...(I1) + 1 + I2]> &...>>,
               std::remove_cvref_t<Tail...[sizeof...(I1) + 1 + I2]>...>;

} // namespace detail


template <typename CORO, typename... Tail>
auto spawn(CORO, Tail &&...tail) {
    return detail::Job_PRE(CORO{}, std::make_index_sequence<detail::separator_index_v<Tail...>>{},
                           std::make_index_sequence<detail::queue_arg_count_v<Tail...>>{}, std::forward<Tail>(tail)...);
}
template <typename CORO, typename... Tail>
auto spawn_uptr(CORO, Tail &&...tail) {
    using JobT = detail::job_pre_from_factory<CORO, Tail...>;
    return std::unique_ptr<JobT>(new JobT(CORO{}, std::make_index_sequence<detail::separator_index_v<Tail...>>{},
                                          std::make_index_sequence<detail::queue_arg_count_v<Tail...>>{},
                                          std::forward<Tail>(tail)...));
}
} // namespace incom::standard::async