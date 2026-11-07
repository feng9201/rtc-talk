#include "rtc_client/media/worker.h"
#include "test.h"
#include <future>
#include <thread>
using namespace rtc_client;
using namespace rtc_client::media;
using namespace std::chrono_literals;
namespace {
class Fake final : public Audio {
 public:
  explicit Fake(std::shared_ptr<MicrophoneGate> gate) : gate_(std::move(gate)) {}
  ~Fake() override { gate_->close(); }
  Json offer() override { return {{"offer",true}}; }
  void accept(const std::string&) override {}
  Json answer(const std::string&) override { throw Error(488); }
  void mute(bool muted) override { if(muted)gate_->close(); }
  Json devices() override { return {{"devices",true}}; }
  Json stats() override { return {{"stats",true}}; }
 private:
  std::shared_ptr<MicrophoneGate> gate_;
};
void run() {
  asio::io_context io;
  auto guard = asio::make_work_guard(io);
  std::thread control([&] { io.run(); });
  auto gate = std::make_shared<MicrophoneGate>();
  std::atomic<int> calls{0}, cancelled{0};
  auto factory = [](const AudioOptions&,const Json&,auto gate,Audio::Event) {
    return std::make_unique<Fake>(std::move(gate));
  };
  // Ensure the control runner is joined even if a CHECK throws.
  struct Cleanup {
    asio::io_context& io; std::thread& thread;
    ~Cleanup(){io.stop();thread.join();}
  } cleanup{io,control};
  {
    AudioWorker worker(io.get_executor(),gate,{},factory,2);
    std::promise<Result> initialized;
    CHECK(worker.initialize({},Json::array(),[&](Result r){initialized.set_value(std::move(r));}));
    auto future=initialized.get_future();CHECK(future.wait_for(3s)==std::future_status::ready);
    CHECK(future.get().ok());
    const auto ticket=gate->begin_open_request();CHECK(gate->open_if_current(ticket));
    std::promise<void> entered, release;
    auto barrier=release.get_future().share();
    CHECK(worker.submit([&](Audio&){entered.set_value();barrier.wait();return Json::object();},
      [&](Result r){CHECK(r.ok());++calls;}));
    CHECK(entered.get_future().wait_for(3s)==std::future_status::ready);
    CHECK(worker.submit([](Audio& m){return m.offer();},[&](Result r){if(r.code==487)++cancelled;++calls;}));
    CHECK(!worker.submit([](Audio& m){return m.stats();},{}));
    worker.stop();CHECK(!gate->is_open());CHECK(!gate->open_if_current(ticket));
    CHECK(!worker.submit([](Audio& m){return m.stats();},{}));
    release.set_value();
  }
  std::promise<void> drained;
  asio::post(io,[&]{drained.set_value();});
  CHECK(drained.get_future().wait_for(3s)==std::future_status::ready);
  CHECK(calls==2);CHECK(cancelled==1);
  {
    AudioWorker worker(io.get_executor(),gate,{},factory);
    std::promise<Result> result;
    CHECK(worker.submit([](Audio& m){return m.stats();},[&](Result r){result.set_value(std::move(r));}));
    CHECK(result.get_future().get().code==409);
    std::promise<Result> initialized;
    CHECK(worker.initialize({},Json::array(),[&](Result r){initialized.set_value(std::move(r));}));
    CHECK(initialized.get_future().get().ok());
    std::promise<Result> rejected;
    CHECK(worker.submit([](Audio& m){return m.answer("video");},[&](Result r){rejected.set_value(std::move(r));}));
    CHECK(rejected.get_future().get().code==488);CHECK(!gate->is_open());
  }
}
}
int main(){return test_main(run);}
