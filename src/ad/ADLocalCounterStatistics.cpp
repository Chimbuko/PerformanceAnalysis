#include <chimbuko/ad/ADLocalCounterStatistics.hpp>

#ifdef _PERF_METRIC
#include <chrono>
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MilliSec;
typedef std::chrono::microseconds MicroSec;
#endif

using namespace chimbuko;

void ADLocalCounterStatistics::gatherStatistics(const CountersByIndex_t &cntrs_by_idx){
  for(auto it : cntrs_by_idx){
    const std::list<CounterDataListIterator_t> &counters = it.second;
    if(counters.size() > 0){
      const std::string &counter_name = (*counters.begin())->get_countername(); //all counters with this index by definition have the same name
      if(m_which_counter->count(counter_name)){
	auto &stats = m_stats[counter_name];
	for(const auto &c : counters)
	  stats.push(c->get_value());
      }
    }
  }
}


nlohmann::json ADLocalCounterStatistics::get_json_state() const{
  nlohmann::json g_info;
  g_info["counters"] = nlohmann::json::array();
  for (auto it : m_stats) { //loop over counter name
    const std::string &name = it.first;

    nlohmann::json obj;
    obj["name"] = name;
    obj["stats"] = it.second.get_json_state();
    g_info["counters"].push_back(obj);
  }
  return g_info;
}


std::pair<size_t, size_t> ADLocalCounterStatistics::updateGlobalStatistics(ADNetClient &net_client) const{
  nlohmann::json state = get_json_state();
#ifndef _PERF_METRIC
  return updateGlobalStatistics(net_client, state.dump(), m_step);
#else

  Clock::time_point t0 = Clock::now();
  auto msgsz = updateGlobalStatistics(net_client, state.dump(), m_step);
  Clock::time_point t1 = Clock::now();

  MicroSec usec = std::chrono::duration_cast<MicroSec>(t1 - t0);
  
  if(m_perf != nullptr){
    m_perf->add("stream_update", (double)usec.count());
    m_perf->add("stream_sent", (double)msgsz.first / 1000000.0); // MB
    m_perf->add("stream_recv", (double)msgsz.second / 1000000.0); // MB
  }  
  return msgsz;
#endif
}

std::pair<size_t, size_t> ADLocalCounterStatistics::updateGlobalStatistics(ADNetClient &net_client, const std::string &l_stats, int step){
  if (!net_client.use_ps())
    return std::make_pair(0, 0);
  
  Message msg;
  msg.set_info(net_client.get_client_rank(), net_client.get_server_rank(), MessageType::REQ_ADD, MessageKind::COUNTER_STATS, step);
  msg.set_msg(l_stats);
  
  size_t sent_sz = msg.size();
  std::string strmsg = net_client.send_and_receive(msg);
  size_t recv_sz = strmsg.size();
  
  return std::make_pair(sent_sz, recv_sz);
}

void ADLocalCounterStatistics::setStats(const std::string &counter, const RunStats &to){
  if(!m_which_counter->count(counter)) throw std::runtime_error("Counter is not in the list of counters we are collecting");
  m_stats[counter] = to;
}

