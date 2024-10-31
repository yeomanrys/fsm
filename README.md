# state machine FSM
编译器需要支持C++17

## 涉及到的特性
不定参数模板
折叠表达式
decltype自动类型推导
if constexpr
原子变量
RTTI
std::tuple
std::apply
std::index_sequence_for

## FSM不定参数模板，初始化指定构造参数
state_machine::FSM<Param1, Param2, ...> fsm;

## feature
whitelist指定了state只可以被该列表里的事件打断;
blacklist指定了state不可以被该列表里的事件打断;
deferlist指定了state结束前不会被该列表事件打断;
option_reuse可以复用旧的状态，否则每次状态切换都会创建新的状态对象;
chainstate在当前state中调用next_state切换到下一个状态;
post_event:
    如果事件触发的状态就是当前状态，事件被添加到当前状态事件列表，并通过on_fsm_event通知当前状态;
    否则，根据不同的list规则指定当前状态是否可以被中断;
状态一旦被中断离开就不可恢复;

## 示例程序
test_fsm.cc

## 编译
g++ -o test_fsm test_fsm.cc -std=c++17

## reference
部分参考自GPT-4o