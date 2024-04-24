#pragma once

#include <iostream>
#include <thread>
#include <chrono>
#include <functional>
#include <caf/all.hpp>
#include <memory>
#include <concepts>

#if __has_include(<coroutine>)
#  include <coroutine>
namespace stdcoro = std;
#else
#  include <experimental/coroutine>
namespace stdcoro = std::experimental;
#endif

template<class R>
struct co_result {
    struct promise_type {
		std::unique_ptr<caf::expected<R>> m_result;
		bool has_rp = false;

        stdcoro::suspend_always initial_suspend() { return {}; }
        stdcoro::suspend_never final_suspend() noexcept { return {}; }
        // void return_void() {}
        void unhandled_exception() {
            std::exit(1);
        }
        co_result get_return_object() {
            return co_result{stdcoro::coroutine_handle<promise_type>::from_promise(*this)};
        }

		void on_completed(stdcoro::function<void(caf::expected<R>)> &&func) {
			if (m_result) {
				func(*m_result);
			} else {
				completion_callbacks.push_back(func);
			}
		}

		void notify_callbacks() {
			auto value = *m_result;
			for (auto &callback : completion_callbacks) {
				callback(value);
			}
			completion_callbacks.clear();
		}
		void return_value(caf::expected<R> v) {
			// std::cout <<"co_result return value: " <<  *v << std::endl;
			m_result = make_unique<caf::expected<R>>(v);
			notify_callbacks();
			if (has_rp)
				this->rp.deliver(v);
        }
		void set_rp(caf::typed_response_promise<R> rp) {
			this->rp = rp;
			has_rp = true;
		}
		caf::typed_response_promise<R> rp;
		std::list<std::function<void(caf::expected<R>)>> completion_callbacks;
    };

	bool await_ready() const { return false; }
	stdcoro::suspend_always initial_suspend() { return {}; }
	void await_suspend(stdcoro::coroutine_handle<> handle) {
		std::cout << "co_result await_suspend" << std::endl;
        coro.promise().on_completed([handle](caf::expected<R> result) mutable {
            handle.resume();  // Resume the awaiting coroutine once the result is ready
        });
		coro.resume();
	}
	caf::expected<R> await_resume() {
		std::cout << "co_result await_resume" << std::endl;
		return *coro.promise().m_result;
	}

    stdcoro::coroutine_handle<promise_type> coro;

    co_result(stdcoro::coroutine_handle<promise_type> h) : coro(h) {
	}
    ~co_result() {
        // if (coro) coro.destroy();
    }
	void run(caf::typed_response_promise<R> rp) {
		coro.promise().set_rp(rp);
		coro.resume();
	}
	void run() {
		coro.resume();
	}
};

template<class R, class Requester, class Dest, typename ... Ts>
struct request_awaiter {
	std::unique_ptr<caf::expected<R>> r;

	request_awaiter(Requester *self, const Dest& dest, std::chrono::duration<long long> dur, Ts... xs):
	self(self), dest(dest), dur(dur), args(std::forward<Ts>(xs)...)
	{ }

	bool await_ready() const { return false; }
	stdcoro::suspend_always initial_suspend() { return {}; }
    void await_suspend(stdcoro::coroutine_handle<> handle) {
		std::cout <<"before awaiter request" <<std::endl;
		std::apply([this, handle](auto&&... args) {
            self->request(dest, dur, std::forward<Ts>(args)...).then(
				[this, handle](R x){
					// std::cout << "request result: " << x << std::endl;
					r = std::make_unique<caf::expected<R>>(x);
					handle.resume();
				},
				[this, handle](caf::error& e){
					r = std::make_unique<caf::expected<R>>(e);
					handle.resume();
				}
			);
        }, args);
    }
    caf::expected<R> await_resume() {
		// std::cout << "await_resume" << *r <<std::endl;
		return *r;
	}
	Requester * self;
	const Dest& dest;
	std::tuple<Ts...> args;
	std::chrono::duration<long long> dur;
};

template<typename R, typename Requester, class Dest, typename... Args>
auto co_req(Requester* self, const Dest& dest, std::chrono::duration<long long> dur, Args&&... args) {
	return request_awaiter<R, Requester, Dest, Args...>(self, dest, dur, std::forward<Args>(args)...);
}