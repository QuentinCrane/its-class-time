// 边界等待：后台线程按“下一个边界”睡眠，到点重新读取真实时钟再判断。
// 不使用定时通知队列，也不做 1 秒忙轮询。
#pragma once

namespace ClassTimeApp {

class BoundaryMonitor {
 public:
  struct FiredBatch {
    std::vector<classtime::BoundaryEvent> events;
    std::vector<classtime::Segment> plan;
    int revision = 1;
    std::string dateKey;
  };
  using FireCallback = std::function<void(const FiredBatch&)>;

  void Start(FireCallback onFire);
  void Stop();
  void RequestRefresh();

 private:
  void Run();

  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stop_ = false;
  bool refresh_ = false;
  FireCallback onFire_;
};

}  // namespace ClassTimeApp
