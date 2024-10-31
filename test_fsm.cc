#include "fsm.h"
#include <fstream>
#include <ostream>
#include <unistd.h>

static state_machine::FSM<std::ofstream*> fsm;

struct Point {
    int x = 0;
    int y = 0;
};
struct CleanEvent {
    int id = 0;
    Point point;
};
class ReadyState {
public:
    ReadyState(std::ofstream* ofs) {
        std::string str = "ready state\n";
        ofs->write(str.c_str(), str.size());
        ofs->flush();
        fsm.next_state(this, ofs);
    }
};

class BuildMapState {
public:
    BuildMapState(std::ofstream* ofs) {
        std::string str = "build map state\n";
        ofs->write(str.c_str(), str.size());
        ofs->flush();
        CleanEvent evt {1, {10, 20}};
        fsm.post_event(evt, ofs);
    }
};

class CleanState {
    std::ofstream* m_ofs;
public:
    CleanState(std::ofstream* ofs) : m_ofs(ofs) {
        std::string str = "clean state\n";
        ofs->write(str.c_str(), str.size());
        ofs->flush();
    }
    void on_fsm_event() {
        CleanEvent evt;
        fsm.get(this)->get_event(evt);
        printf("clean id:%d x:%d y:%d\n", evt.id, evt.point.x, evt.point.y);
        // cleaning
        fsm.next_state(this, m_ofs);
    }
};

class RechargeState {
public:
    RechargeState(std::ofstream* ofs) {
        std::string str = "recharge state\n";
        ofs->write(str.c_str(), str.size());
        ofs->flush();
    }
};

int main(int argc, char* argv[]) {
    fsm.regist_chain_state<ReadyState, BuildMapState>();        // 准备完成后自动建图
    fsm.regist_chain_state<CleanState, RechargeState>();        // 清扫完自动回充
    fsm.regist_trans_event<CleanState, CleanEvent>();           // 触发cleanEvent时自动清扫
    fsm.regist_black_event<RechargeState, CleanEvent>();        // 充电中屏蔽cleanEvent事件
    std::ofstream ofs("./clean.log");
    fsm.enter_state<ReadyState>(&ofs);
    while(!fsm.in_state<RechargeState>()) {
        sleep(1);
    }
    CleanEvent evt {1, {0, 0}};
    fsm.post_event(evt, &ofs);  // fsm处于充电状态，不会对该事件响应
    getchar();
    return 0;
}