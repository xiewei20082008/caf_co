#include "caf_co.h"

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



caf_co<int> inc_once(actor_requester auto *self, actor add_actor, int base) {
	auto res = co_await co_request<int>(self, add_actor, caf::infinite, 1, base);
	cout << "inc_once get res: " << res << endl;
	co_return *res;
}

caf_co<int> inc_twice(actor_requester auto *self, actor add_actor, int base) {
	auto res1 = co_await inc_once(self, add_actor, base).to_awaiter(self);
	auto res2 = co_await inc_once(self, add_actor, *res1).to_awaiter(self);
	co_return *res2;
}


behavior test_impl(event_based_actor *self, actor add_actor) {
	return {
		[=](int x) -> result<int> {
			auto co_env = make_co_env<int>(self);
			return co_env->make_co( [self, x, add_actor](auto co) -> caf_co<int>{
				auto res = co_await inc_twice(self, add_actor, x).to_awaiter(self);
				cout << *res << endl;
				auto res1 = co_await co_request<int>(self, add_actor, caf::infinite, 1, *res);
				co_return *res1;
			});
		}
	};
}




void caf_main(actor_system& system) {
	auto add_actor = system.spawn<AddActor>();
	auto test_actor = system.spawn(test_impl, add_actor);

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
	anon_send_exit(add_actor, caf::exit_reason::user_shutdown);
	getchar();
}

CAF_MAIN();
