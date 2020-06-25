#include <chimbuko/pserver/global_counter_stats.hpp>

using namespace chimbuko;

void GlobalCounterStats::add_data(const std::string& data){
  nlohmann::json j = nlohmann::json::parse(data);
  if(!j.count("counters")) throw std::runtime_error("Data string does not contain counters array");

  std::lock_guard<std::mutex> _(m_mutex);
  for(auto c: j["counters"]){
    if(!c.count("name") || !c.count("stats")) throw std::runtime_error("Data string counters array has unexpected format");    
    RunStats stats = RunStats::from_json_state(c["stats"]);
    m_counter_stats[c["name"]] += stats;
  }
}

std::unordered_map<std::string, RunStats> GlobalCounterStats::get_stats() const{
  std::lock_guard<std::mutex> _(m_mutex);
  std::unordered_map<std::string, RunStats> out = m_counter_stats;
  return out;
}

nlohmann::json GlobalCounterStats::get_json_state() const
{
    nlohmann::json jsonObjects = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> _(m_mutex);
        for (auto pair: m_counter_stats)
        {
            nlohmann::json object;
            object["counter"] = pair.first;
	    object["stats"] = pair.second.get_json();
            jsonObjects.push_back(object);
        }
    }
    return jsonObjects;
}