#pragma once

#include <iostream>
#include <thread>
#include <chrono>
#include <functional>
#include <caf/all.hpp>
#include <memory>
#include <concepts>

using namespace caf;
using namespace std;

#if __has_include(<coroutine>)
#  include <coroutine>
namespace stdcoro = std;
#else
#  include <experimental/coroutine>
namespace stdcoro = std::experimental;
#endif

namespace dual {
	template<typename T>
	concept is_void = std::is_same<T, void>::value;

	template<typename T>
	concept not_void = !std::is_same<T, void>::value;
}

template <typename T>
struct is_typed_event_based_actor : std::false_type{};

template <class... Sigs>
struct is_typed_event_based_actor<caf::typed_event_based_actor<Sigs...>>: std::true_type{};

template <typename T>
struct is_event_based_actor : std::false_type{};

template <>
struct is_event_based_actor<event_based_actor>: std::true_type{};

template <typename T>
struct is_stateful_actor : std::false_type{};

template <class State, class Base>
struct is_stateful_actor<stateful_actor<State, Base>>: std::true_type{};


template <class T>
concept actor_requester
	= is_typed_event_based_actor<T>::value || is_event_based_actor<T>::value || is_stateful_actor<T>::value;



template <class T>
struct [[nodiscard]] caf_co;
template <class T>
struct caf_co_promise;
template<typename R>
struct CafCoSelfAwaiter;

template <class T>
struct caf_co_promise
{
	// template<typename... Args>
	caf_co_promise()
	{
		wait_destroy = std::make_shared<bool>(true);
	}
	void link_lifespan_to_requester(actor_requester auto* self) {
		auto c = stdcoro::coroutine_handle<caf_co_promise<T>>::from_promise(*this);
		self->attach_functor([c, wait_destroy=this->wait_destroy]() mutable{
			if(*wait_destroy) {
				cout <<"Destroy a coroutine handle" << endl;
				c.destroy();
			}
		});
		c.resume();
	}

	auto get_return_object() noexcept { return stdcoro::coroutine_handle<caf_co_promise<T>>::from_promise(*this); }
	auto initial_suspend() {
		return stdcoro::suspend_always{};
	}
	auto final_suspend() noexcept {
		*wait_destroy = false;
		return stdcoro::suspend_never{};
	}
	void unhandled_exception() { std::terminate(); }

	void return_value(expected<T> const& v)
	{
		cout << "deliver once" << endl;
		m_value = make_unique<expected<T>>(v);
		notify_callbacks();
		cout << "notify_callbacks OK" << endl;
	}
	// For CAF, it seems to be safe not to use lock here.
	void on_completed(std::function<void(expected<T>)> &&func) {
		if (m_value) {
			func(*m_value);
		} else {
			completion_callbacks.push_back(func);
		}
	}
	void notify_callbacks() {
		auto value = *m_value;
		for (auto &callback : completion_callbacks) {
			callback(value);
		}
		completion_callbacks.clear();
	}
	expected<T> get_result() {
		return *m_value;
	}

	unique_ptr<expected<T>> m_value;
	std::list<std::function<void(expected<T>)>> completion_callbacks;
	std::shared_ptr<bool> wait_destroy;
};

template <class T>
struct [[nodiscard]] caf_co
{
	using promise_type =  caf_co_promise<T>;
	caf_co(stdcoro::coroutine_handle<promise_type> coroutine)
		: m_coroutine(std::move(coroutine))
	{
	}
	~caf_co() {
	}
	caf_co &finally(std::function<void()> &&func) {
		m_coroutine.promise().on_completed([func](auto result)
			{ func(); }
		);
		return *this;
	}

	void link_lifespan_to_requester(actor_requester auto* self) {
		m_coroutine.promise().link_lifespan_to_requester(self);
	}
	expected<T> get_result() {
		return m_coroutine.promise().get_result();
	}

	CafCoSelfAwaiter<T> to_awaiter(actor_requester auto* self) {
		return CafCoSelfAwaiter<T>(std::move(*this), self);
	}

	void set_rp(typed_response_promise<T> rp) {
		m_coroutine.promise().on_completed([rp](expected<T> result) mutable{
			rp.deliver(result);
		});
	}

	stdcoro::coroutine_handle<promise_type> m_coroutine;
};
template<typename R>
struct CafCoSelfAwaiter {
	explicit CafCoSelfAwaiter(caf_co<R>&& co, actor_requester auto* self) noexcept
		: co(std::move(co))
	{
		co.link_lifespan_to_requester(self);
	}

	CafCoSelfAwaiter(CafCoSelfAwaiter &) = delete;

	CafCoSelfAwaiter &operator=(CafCoSelfAwaiter &) = delete;

	constexpr bool await_ready() const noexcept {
		return false;
	}

	void await_suspend(stdcoro::coroutine_handle<> handle) noexcept {
		co.finally([handle]() mutable {
			handle.resume();
		});
	}

	expected<R> await_resume() noexcept {
		return co.get_result();
	}

	private:
	caf_co<R> co;
};

template<typename ReturnType, typename Requester>
struct caf_co_env : public std::enable_shared_from_this<caf_co_env<ReturnType, Requester>> {
	caf_co_env(Requester* arg_self)
	{
		self = arg_self;
		m_rp = self->template make_response_promise<ReturnType>();
	}
	~caf_co_env() {
	}
	Requester *self;

	using r_t = caf_co<ReturnType>;
	using co_sig = std::function<caf_co<ReturnType>(shared_ptr<caf_co_env>)>;
	co_sig f;
	typed_response_promise<ReturnType> make_co(co_sig arg_f) {
		f = arg_f;
		r_t r = f(this->shared_from_this());
		r.link_lifespan_to_requester(self);
		r.set_rp(m_rp);
		return m_rp;
	}
	void make_co_void(co_sig arg_f) {
		f = arg_f;
		r_t r = f(this->shared_from_this());
		r.link_lifespan_to_requester(self);
	}
	typed_response_promise<ReturnType> m_rp;
};

template<typename ReturnType, typename Requester>
shared_ptr<caf_co_env<ReturnType, Requester>> make_co_env(Requester *self) {
	return make_shared<caf_co_env<ReturnType, Requester>>(self);
}

template<typename T, typename Requester, typename... Args>
struct CoRequestAwaiter
{
	CoRequestAwaiter(Requester* self, Args&&... args)
		: m_args(std::forward<Args>(args)...)
	{
		m_self= self;
	}
	bool await_ready() const { return false; }
	expected<T> await_resume() { return *m_result; }
	void await_suspend(stdcoro::coroutine_handle<> handle) noexcept
	{
		std::apply(
			[&handle, this](auto&&... args){
				m_self->request(std::forward<Args>(args)...).then(
					[this, handle](T x) mutable {
						m_result = make_unique<expected<T>>(x);
						handle.resume();
					},
					[this, handle](caf::error& e) mutable {
						m_result = make_unique<expected<T>>(expected<T>(e));
						handle.resume();
					}
				);
			}, m_args
		);
	}
	unique_ptr<expected<T>> m_result;
	std::tuple<Args...> m_args;
	Requester* m_self;
};

template<typename Requester, typename... Args>
struct CoRequestAwaiterVoid
{
	CoRequestAwaiterVoid(Requester* self, Args&&... args)
		: m_args(std::forward<Args>(args)...)
	{
		m_self= self;
	}
	bool await_ready() const { return false; }
	expected<void> await_resume() { return *m_result; }
	void await_suspend(stdcoro::coroutine_handle<> handle) noexcept
	{
		std::apply(
			[&handle, this](auto&&... args){
				m_self->request(std::forward<Args>(args)...).then(
					[this, handle]() mutable {
						m_result = make_unique<expected<void>>();
						handle.resume();
					},
					[this, handle](caf::error& e) mutable {
						m_result = make_unique<expected<void>>(expected<void>(e));
						handle.resume();
					}
				);
			}, m_args
		);
	}
	unique_ptr<expected<void>> m_result;
	std::tuple<Args...> m_args;
	Requester* m_self;
};

template<typename... Ts>
struct CoRequestAwaiterTuple {
	template<typename Requester, typename... Args>
	struct Awaiter
	{
		using R = std::tuple<Ts...>;
		Awaiter(Requester* self, Args&&... args)
			: m_args(std::forward<Args>(args)...)
		{
			m_self= self;
		}
		bool await_ready() const { return false; }
		expected<R> await_resume() { return *m_result; }
		void await_suspend(stdcoro::coroutine_handle<> handle) noexcept
		{
			std::apply(
				[&handle, this](auto&&... args){
					m_self->request(std::forward<Args>(args)...).then(
						[this, handle](Ts... xs) mutable {
							auto res = std::make_tuple(xs...);
							m_result = make_unique<expected<R>>(res);
							handle.resume();
						},
						[this, handle](caf::error& e) mutable {
							m_result = make_unique<expected<R>>(expected<R>(e));
							handle.resume();
						}
					);
				}, m_args
			);
		}
		unique_ptr<expected<R>> m_result;
		std::tuple<Args...> m_args;
		Requester* m_self;
	};
};

template<dual::not_void T, typename Requester, typename... Args>
auto co_request(Requester* self, Args&&... args) {
	return CoRequestAwaiter<T, Requester, Args...>(self, std::forward<Args>(args)...);
}

template<dual::is_void T, typename Requester, typename... Args>
auto co_request(Requester* self, Args&&... args) {
	return CoRequestAwaiterVoid<Requester, Args...>(self, std::forward<Args>(args)...);
}

template<typename... Ts, typename Requester, typename... Args>
auto co_request(Requester* self, Args&&... args) {
	// return CoRequestAwaiterVoid<Requester, Args...>(self, std::forward<Args>(args)...);
	return typename CoRequestAwaiterTuple<Ts...>::template Awaiter<Requester, Args...>(self, std::forward<Args>(args)...);
}


