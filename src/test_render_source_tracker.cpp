#include "render_source_tracker.h"
#include <cassert>
#include <thread>
#include <atomic>
#include <cstdio>
int main(){
    render_source::Tracker<4> tracker;
    auto token=tracker.Reset();float m[16]{};render_cycle::Snapshot source{};
    source.command=7;source.serial=30;source.pose.tick=123;source.pose.rotation[0]=.5f;
    assert(tracker.Queue(token,100,m,source));
    // Same matrix at a different destination must not consume the source.
    assert(tracker.Consume(token,200,m,9).status==render_source::Missing);
    auto stamp=tracker.Consume(token,100,m,9);
    assert(stamp.status==render_source::Matched && stamp.source.command==7 && stamp.source.serial==30);
    assert(stamp.source.pose.tick==123 && stamp.source.pose.rotation[0]==.5f);
    assert(tracker.Read(8).status==render_source::WrongThread);
    // Unknown next consumer invalidates the latest known stamp.
    assert(tracker.Consume(token,100,m,9).status==render_source::Missing);
    assert(tracker.Read(9).source.command==0);
    assert(tracker.Queue(token,100,m,source));++source.command;++source.serial;
    assert(tracker.Queue(token,100,m,source));
    assert(tracker.Consume(token,100,m,9).status==render_source::Ambiguous);
    assert(tracker.Read(9).source.command==0);
    for(unsigned i=0;i<4;++i){m[0]=float(i);assert(tracker.Queue(token,100,m,source));}
    m[0]=4;assert(!tracker.Queue(token,100,m,source));
    assert(tracker.Overflows()==1);m[0]=0;
    assert(tracker.Consume(token,100,m,9).status==render_source::Missing);
    m[0]=4;assert(tracker.Consume(token,100,m,9).status==render_source::Missing);
    tracker.Reset();
    assert(!tracker.Queue(token,100,m,source));
    assert(tracker.Consume(token,100,m,9).source.command==0);
    token=tracker.Epoch();assert(tracker.Queue(token,100,m,source));
    tracker.Consume(token,100,nullptr,9);assert(tracker.Read(9).source.command==0);
    // Independent producers/consumers: exactly one outstanding entry per target.
    tracker.Reset();token=tracker.Epoch();std::atomic<bool> bad{false};
    std::thread workers[4];
    for(unsigned t=0;t<4;++t)workers[t]=std::thread([&,t]{
        for(unsigned i=0;i<10000;++i){
            float matrix[16]{};matrix[0]=float(i);
            render_cycle::Snapshot s{};s.command=i+1;s.serial=i+1;s.pose.tick=t+100;
            if(!tracker.Queue(token,t+1,matrix,s))bad=true;
            auto result=tracker.Consume(token,t+1,matrix,t+1);
            if(result.status!=render_source::Matched || result.source.command!=i+1 || result.source.pose.tick!=t+100)bad=true;
        }
    });
    for(auto& worker:workers)worker.join();
    assert(!bad && tracker.Overflows()==0);
    puts("PASS exact target/matrix matching; source pose retention; ambiguity/missing/reset/overflow; 40000 concurrent round trips");
}
