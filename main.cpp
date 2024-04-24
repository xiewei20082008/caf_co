#include "caf_co.h"
#include <__coroutine/trivial_awaitables.h>
#include <caf/event_based_actor.hpp>
#include <caf/function_view.hpp>
#include <caf/typed_response_promise.hpp>
#include <future>
#include <memory>
#include <thread>

using inc_actor_t = typed_actor<
	result<int>(int)
>;

inc_actor_t::behavior_type inc_impl(inc_actor_t::pointer self, actor add_actor) {
	return {
		[=](int x) -> result<int> {
			return x+1;
		}
	};
}

class AddActor : public event_based_actor {
public:
	AddActor(actor_config& cfg) : event_based_actor(cfg) {
	}
	behavior make_behavior() override {
		return {
			[=](int a, int b) {
				return a+b;
			}
		};
	}
};

template<class T, class Handle, class ...Ts>
void get(T* t, const Handle& dest, Ts&&... xs) {
	t->request(dest, 1s, std::forward<Ts>(xs)...);
}

behavior test_impl(event_based_actor *self, inc_actor_t inc_actor) {
	return {
		[=](int x) -> result<int> {
			get(self, inc_actor, 1);
			// self->request(inc_actor, 1s, "f");
			return 1;
		}
	};
}

template<class R>
struct co_result {
    struct promise_type {
		unique_ptr<expected<R>> m_result;

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        // void return_void() {}
        void unhandled_exception() {
            std::exit(1);
        }
        co_result get_return_object() {
            return co_result{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

		void on_completed(std::function<void(expected<R>)> &&func) {
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
			cout <<"co_result return value: " <<  *v << endl;
			m_result = make_unique<expected<R>>(v);
			notify_callbacks();
			this->rp.deliver(v);
        }
		void set_rp(typed_response_promise<R> rp) {
			this->rp = rp;
		}
		typed_response_promise<R> rp;
		std::list<std::function<void(expected<R>)>> completion_callbacks;
    };

	bool await_ready() const { return false; }
	std::suspend_always initial_suspend() { return {}; }
	void await_suspend(std::coroutine_handle<> handle) {
		cout << "co_result await_suspend" << endl;
        coro.promise().on_completed([handle](expected<R> result) mutable {
            handle.resume();  // Resume the awaiting coroutine once the result is ready
        });
		coro.resume();
	}
	expected<R> await_resume() {
		cout << "co_result await_resume" << endl;
		return *coro.promise().m_result;
	}

    std::coroutine_handle<promise_type> coro;

    co_result(std::coroutine_handle<promise_type> h) : coro(h) {
	}
    ~co_result() {
        // if (coro) coro.destroy();
    }
	void run(typed_response_promise<R> rp) {
		coro.promise().set_rp(rp);
		coro.resume();
	}
};

template<class Requester, class Dest, typename ... Ts>
struct request_awaiter {
	int r;

	request_awaiter(Requester *self, const Dest& dest, Ts... xs):
	self(self), dest(dest), args(std::forward<Ts>(xs)...)
	{ }

	bool await_ready() const { return false; }
	std::suspend_always initial_suspend() { return {}; }
    void await_suspend(std::coroutine_handle<> handle) {
		cout <<"before awaiter request" <<endl;
		std::apply([this, handle](auto&&... args) {
            self->request(dest, std::chrono::seconds(2), std::forward<Ts>(args)...).then(
				[this, handle](int x){
					cout << "request result: " << x << endl;
					r = x;
					handle.resume();
				},
				[this](caf::error& e){
				}
			);
        }, args);
    }
    int await_resume() {
		cout << "await_resume" << r <<endl;
		return r;
	}
	Requester * self;
	const Dest& dest;
	std::tuple<Ts...> args;
};



co_result<int> inc_1(event_based_actor *self, inc_actor_t inc_actor, int x) {
	auto ra = request_awaiter(self, inc_actor, x);
	int t = co_await ra;
	co_return t;
}

co_result<int> inc_2(event_based_actor *self, inc_actor_t inc_actor) {
	auto t1 = co_await inc_1(self, inc_actor, 1);
	auto t2 = co_await inc_1(self, inc_actor , *t1);
	co_return t2;
}

behavior test_impl1(event_based_actor *self, inc_actor_t inc_actor) {
	return {
		[=](int x) -> result<int> {
			auto rp = self->make_response_promise<int>();
			auto co = inc_2(self, inc_actor);
			co.run(rp);
			return rp;
		}
	};
}

// /*
void caf_main(actor_system& system) {
	auto add_actor = system.spawn<AddActor>();
	inc_actor_t inc_actor = system.spawn(inc_impl, add_actor);
	auto test_actor = system.spawn(test_impl1, inc_actor);

	scoped_actor sc{system};
	sc->request(test_actor, 20s, 0).receive(
		[](int x){
			cout<< x << endl;
		},
		[](caf::error&e) {
			cout<< "error" << to_string(e)<<endl;
		}
	);
	getchar();
	anon_send_exit(inc_actor, caf::exit_reason::user_shutdown);
	getchar();
}

CAF_MAIN();

/*
int slow_func(int a) {
    // Simulate some computation
    return a * a;  // Just an example of a computation
}
future<void> t;
void request(int a, std::function<void(int)> f) {
	t = std::async([a, f]() {
		int t = slow_func(a);
		f(t);
	});
}

struct co_result {
    struct promise_type {
        int value;
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        // void return_void() {}
        void unhandled_exception() {
            std::exit(1);
        }
        co_result get_return_object() {
            return co_result{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
		void return_value(int v) {
			cout <<"co_result return value: " <<  v << endl;
            value = v;
        }

		std::promise<int>& r;
    };

    std::coroutine_handle<promise_type> coro;

    co_result(std::coroutine_handle<promise_type> h) : coro(h) {
	}
    ~co_result() {
        // if (coro) coro.destroy();
    }
    // Adding a function to retrieve the result easily
    future<int>& get() {
        return r_f;
    }
};

struct request_awaiter {
	int r;

	int in;
	request_awaiter(int x) {
		in = x;
	}

	bool await_ready() const { return false; }
	std::suspend_always initial_suspend() { return {}; }
    void await_suspend(std::coroutine_handle<> handle) {
		cout <<"before awaiter request" <<endl;
		request(in, [this, handle](int x){
			cout <<"call back res: " <<  x <<endl;
			this->r = x;
			handle.resume();
		});
    }
    int await_resume() {
		cout << "await_resume" << r <<endl;
		return r;
	}
};

co_result co_request(int a) {
	auto r_a = request_awaiter(a);
	int t = co_await r_a;
	cout << "t: " << t <<endl;
    // int t = slow_func(a);
    co_return t;
}

int main() {
    auto result = co_request(10); // Call coroutine function
    std::cout << "Result of co_request: " << result.get().get() << std::endl;
    return 0;
}
*/