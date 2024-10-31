#pragma once
#include <set>
#include <map>
#include <any>
#include <deque>
#include <stdio.h>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>

namespace state_machine {
    template <class ...TStateParams>
    class FSM;
    class State {
        template <class ...TStateParams>
        friend class FSM;
    protected:
        bool                  reuse = false;
        std::recursive_mutex  mtx;
        std::string           name;
        std::string           nextstate;
        std::set<std::string> whitelist;
        std::set<std::string> blacklist;
        std::set<std::string> deferlist;
        std::map<std::string,std::any> evts;
        template <typename T, typename = void>
        struct has_member_evt : std::false_type {};
        template <typename T>
        struct has_member_evt<T, std::void_t<decltype(std::declval<T>().on_fsm_event())>> : std::true_type {};
    public:
        virtual ~State() {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            evts.clear();
        }
        bool has_event() {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            return evts.size();
        }
        template <class T>
        bool get_event(T& evt) {
            std::string key = typeid(T).name();
            std::lock_guard<std::recursive_mutex> lock(mtx);
            if (!evts.count(key))
                return false;
            try {
                evt = std::any_cast<T>(evts[key]);
                evts.erase(key);
            } catch(...) {
                return false;
            }
            return true;
        }
    protected:
        virtual void onevt() = 0;
        virtual void enter() = 0;
        virtual void leave() = 0;
        virtual void set_params(const std::vector<std::any>& param) = 0;
    private:
        int interrupt(const std::string& evtname) {
            if (whitelist.size() && !whitelist.count(evtname))
                return 0;
            if (blacklist.count(evtname))
                return 0;
            if (deferlist.count(evtname))
                return -1;
            return 1;
        }
        template <class E>
        void add_event(const E& evt) {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            evts[typeid(E).name()] = std::any(evt);
        }
        template <class TList, class T1, class ...Tn>
        void add_list(TList& list) {
            list.insert(typeid(T1).name());
            (list.insert(typeid(Tn).name()), ...);
        }
        template <class ...En>
        void add_white_event() {
            add_list<decltype(whitelist), En...>(whitelist);
        }
        template <class ...En>
        void add_black_event() {
            add_list<decltype(blacklist), En...>(blacklist);
        }
        template <class ...En>
        void add_defer_event() {
            add_list<decltype(deferlist), En...>(deferlist);
        }
    };

    template <class T, class ...TParams>
    class StateImpl : public State {
        T* ta = 0;
        std::tuple<TParams...> param;
    public:
        void onevt() {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            if (!ta)
                return;
            if constexpr (has_member_evt<T>::value)
                ta->on_fsm_event();
            else
                evts.clear();
        }
        void enter() {
            if (ta && !reuse)
                leave();
            if (!ta || !reuse) {
                std::lock_guard<std::recursive_mutex> lock(mtx);
                if constexpr (sizeof...(TParams) == 0)
                    ta = new T();
                else
                    ta = std::apply([](TParams... args) {return new T(args...);}, param);
            }
            if (evts.size())
                onevt();
        }
        void leave() {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            evts.clear();
            if (reuse) return;
            if (ta)
                delete ta;
            ta = 0;
        }
        T* get() {
            return ta;
        }
        ~StateImpl() {
            leave();
        }
    protected:
        template <std::size_t... Is>
        std::tuple<TParams...> set_params_helper(const std::vector<std::any>& params, std::index_sequence<Is...>) {
            return std::make_tuple(std::any_cast<TParams>(params[Is])...);
        }
        void set_params(const std::vector<std::any>& params) override {
            if (sizeof...(TParams) != params.size())
                return;
            param = set_params_helper(params, std::index_sequence_for<TParams...>{});
        }
    };

    template <class ...TStateParams>
    class FSM {
        bool                                eof = false;
        std::atomic<int>                    waitsize = 0;
        std::mutex                          statmtx;
        State*                              curstate = 0;
        std::deque<std::string>             waitstates;
        std::map<std::string, std::string>  events;
        std::map<std::string, State*>       states;
    private:
        template <class Tlist, class T1, class ...Tn>
        bool _exist(const Tlist& tlist) {
            std::string name = typeid(T1).name();
            if (!tlist.count(name))
                return false;
            if constexpr (sizeof...(Tn) > 0) {
                if (!_exist<Tlist, Tn...>(tlist))
                    return false;
            }
            return true;
        }
        template <class T1, class ...Tn>
        void _bind_chain(State* prev = 0) {
            if (prev)
                prev->nextstate = typeid(T1).name();
            if constexpr (sizeof...(Tn) > 0)
                _bind_chain<Tn...>(get<T1>());
        }
        template <class E1, class ...En>
        void _bind_event(const std::string& state_name) {
            events[(typeid(E1).name())] = state_name;
             ((events[typeid(En).name()] = state_name), ...);
        }
        void _enter_state(const std::string& name, const std::vector<std::any>& paramlist, State* oldstate) {
            if (!oldstate)
                oldstate = curstate;

            State* newstate = 0;
            {
                std::unique_lock<std::mutex> lock(statmtx);
                curstate = states.count(name) ? states[name] : 0;
                newstate = curstate;
                if (oldstate == newstate)
                    return;
            }

            if (oldstate)
                oldstate->leave();

            if (newstate && paramlist.size())
                newstate->set_params(paramlist);

            if (newstate)
                newstate->enter();
        }
    public:
        ~FSM() {
            eof = true;
            std::unique_lock<std::mutex> lock(statmtx);
            events.clear();

            int size = 0;
            while(!waitsize.compare_exchange_strong(size, 0))
                size = 0;

            for (auto& state : states)
                delete state.second;
            states.clear();
        }
        template <class T>
        bool in_state() {
            return curstate ?
                curstate->name.compare(typeid(T).name()) == 0 : false;
        }
        template <class ...Tn>
        bool exist_event() {
            return _exist<decltype(events), Tn...>(events);
        }
        template <class ...Tn>
        bool exist_state() {
            return _exist<decltype(states), Tn...>(states);
        }
        template <class T>
        State* get() {
            if (!exist_state<T>())
                return 0;
            return states[typeid(T).name()];
        }
        template <class T>
        State* get(T* t) {
            return get<T>();
        }
        template <class T>
        T* get_state() {
            if (!exist_state<T>())
                return 0;
            return dynamic_cast<StateImpl<T, TStateParams...>*>(get<T>())->get();
        }
        template <class T, class ...Tn>
        void option_reuse() {
            State* state = get<T>();
            state->reuse = true;
            if constexpr(sizeof...(Tn) > 0)
                option_reuse<Tn...>();
        }
        template <class T, class ...Tn>
        void regist_state() {
            std::string name = typeid(T).name();
            if (states.count(name))
                return;
            State *state = new StateImpl<T, TStateParams...>();
            state->name = name;
            states[name] = state;
            if constexpr(sizeof...(Tn) > 0)
                regist_state<Tn...>();
        }
        template <class ...Tn>
        void regist_chain_state() {
            regist_state<Tn...>();
            _bind_chain<Tn...>();
        }
        template <class TState, class ...TEvent>
        void regist_trans_event() {
            regist_state<TState>();
            _bind_event<TEvent...>(typeid(TState).name());
        }
        template <class TState, class ...TEvent>
        void regist_white_event() {
            regist_state<TState>();
            State* state = get<TState>();
            state->add_white_event<TEvent...>();
        }
        template <class TState, class ...TEvent>
        void regist_black_event() {
            regist_state<TState>();
            State* state = get<TState>();
            state->add_black_event<TEvent...>();
        }
        template <class TState, class ...TEvent>
        void regist_defer_event() {
            regist_state<TState>();
            State* state = get<TState>();
            state->add_defer_event<TEvent...>();
        }
        template <class T>
        void enter_state(TStateParams... params) {
            State* s = get<T>();
            _enter_state(typeid(T).name(), { std::any(params)... }, nullptr);
        }

        template <class T>
        void next_state(const T* mine, TStateParams... params) {
            if (eof || !curstate)
                return;
            std::string staname;
            State* oldstate = 0;
            std::unique_lock<std::mutex> lock(statmtx);
            if (!curstate)
                return;
            if (curstate->name.compare(typeid(T).name()) != 0)
                return;
            oldstate = curstate;
            if (waitstates.size()) {
                staname = waitstates.front();
                waitstates.pop_front();
            } else {
                staname = curstate->nextstate;
            }
            waitsize++;
            lock.unlock();
            _enter_state(staname, { std::any(params)... }, oldstate);
            waitsize--;
        }
        template <class E>
        void post_event(const E& evt, TStateParams... params) {
            if (eof)
                return;
            State* evtstate = 0;
            std::string evtname = typeid(E).name();
            std::unique_lock<std::mutex> lock(statmtx);
            bool exist = events.count(evtname);
            if (!exist && !curstate)
                return;
            std::string staname = exist ? events[evtname] : "";

            if (curstate && curstate->name == staname || !exist) {
                curstate->add_event(evt);
                evtstate = curstate;
            }
            else {
                int code = curstate ? curstate->interrupt(evtname) : 1;
                if (code)
                    states[staname]->add_event(evt);
                if (code == -1)
                    waitstates.push_back(staname);
                if (code < 1)
                    return;
            }
   
            waitsize++;
            lock.unlock();

            if (evtstate)
                evtstate->onevt();
            else
                _enter_state(staname, { std::any(params)... }, curstate);

            waitsize--;
        }
    };
}